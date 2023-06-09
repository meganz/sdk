#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Simple command line interface using the Python bindings."""

## Created: 15 Jan 2015 Javier Serrano <js@mega.co.nz>
##
## (c) 2015 by Mega Limited, Auckland, New Zealand
##     http://mega.co.nz/
##     Simplified (2-clause) BSD License.
##
## You should have received a copy of the license along with this
## program.
##
## This file is part of the multi-party chat encryption suite.
##
## This code is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

__author__ = 'Javier Serrano <js@mega.co.nz>'

import sys
import os
import cmd
import logging
import time

from mega import (MegaApi, MegaListener, MegaError, MegaRequest,
                  MegaUser, MegaNode)


class AppListener(MegaListener):
    def __init__(self, shell):
        self._shell = shell
        super(AppListener, self).__init__()


    def onRequestStart(self, api, request):
        logging.info('Request start ({})'.format(request))


    def onRequestFinish(self, api, request, error):
        logging.info('Request finished ({}); Result: {}'
                     .format(request, error))
        if error.getErrorCode() != MegaError.API_OK:
            return

        request_type = request.getType()
        if request_type == MegaRequest.TYPE_LOGIN:
            api.fetchNodes()
        elif request_type == MegaRequest.TYPE_EXPORT:
            logging.info('Exported link: {}'.format(request.getLink()))
        elif request_type == MegaRequest.TYPE_ACCOUNT_DETAILS:
            account_details = request.getMegaAccountDetails()
            logging.info('Account details received')
            logging.info('Account e-mail: {}'.format(api.getMyEmail()))
            logging.info('Storage: {} of {} ({} %)'
                         .format(account_details.getStorageUsed(),
                                 account_details.getStorageMax(),
                                 100 * account_details.getStorageUsed()
                                 / account_details.getStorageMax()))
            logging.info('Pro level: {}'.format(account_details.getProLevel()))


    def onRequestTemporaryError(self, api, request, error):
        logging.info('Request temporary error ({}); Error: {}'
                .format(request, error))


    def onTransferFinish(self, api, transfer, error):
        logging.info('Transfer finished ({}); Result: {}'
                .format(transfer, transfer.getFileName(), error))


    def onTransferUpdate(self, api, transfer):
        logging.info('Transfer update ({} {});'
                     ' Progress: {} KB of {} KB, {} KB/s'
                     .format(transfer,
                             transfer.getFileName(),
                             transfer.getTransferredBytes() / 1024,
                             transfer.getTotalBytes() / 1024,
                             transfer.getSpeed() / 1024))


    def onTransferTemporaryError(self, api, transfer, error):
        logging.info('Transfer temporary error ({} {}); Error: {}'
                     .format(transfer, transfer.getFileName(), error))


    def onUsersUpdate(self, api, users):
        if users != None:
            logging.info('Users updated ({})'.format(users.size()))

    def onNodesUpdate(self, api, nodes):
        if nodes != None:
            logging.info('Nodes updated ({})'.format(nodes.size()))
        else:
            self._shell.cwd = api.getRootNode()


class MegaShell(cmd.Cmd, MegaListener):
    intro = 'Mega sample app. Type help or ? to list commands.\n'
    PROMPT = '(MEGA)'

    def __init__(self, api):
        self._api = api
        self.cwd = None
        super(MegaShell, self).__init__()


    def emptyline(self):
        return


    def do_login(self, arg):
        """Usage: login email password"""
        args = arg.split()
        if len(args) != 2 or '@' not in args[0]:
            print(self.do_login.__doc__)
            return

        self._api.login(args[0], args[1])


    def do_logout(self, arg):
        """Usage: logout"""

        args = arg.split()
        if len(args) != 0:
            print(self.do_logout.__doc__)
            return

        if self.cwd == None:
            print('INFO: Not logged in')
            return

        self._api.logout()
        self.cwd = None


    def do_mount(self, arg):
        """Usage: mount"""
        args = arg.split()
        if len(args) != 0:
            print(self.do_mount.__doc__)
            return
        if not self._api.isLoggedIn():
            print('INFO: Not logged in')
            return

        print('INFO: INSHARES:')
        users = self._api.getContacts()
        for i in range(users.size()):
            user = users.get(i)
            if user.getVisibility() == MegaUser.VISIBILITY_VISIBLE:
                shares = self._api.getInShares(user)
                for j in range(shares.size()):
                    share = shares.get(j)
                    print('INFO: INSHARE on {} {} Access level: {}'
                          .format(users.get(i).getEmail(),
                                  are.getName(),
                                  self._api.getAccess(share)))


    def do_ls(self, arg):
        """Usage: ls [path]"""
        args = arg.split()
        if len(args) > 1:
            print(self.do_ls.__doc__)
            return

        if self.cwd == None:
            print('INFO: Not logged in')
            return

        path = None
        if len(args) == 0:
            path = self.cwd
        else:
            path = self._api.getNodeByPath(args[0], self.cwd)

        print('    .')
        if self._api.getParentNode(path) != None:
            print('    ..')

        nodes = self._api.getChildren(path)
        for i in range(nodes.size()):
            node = nodes.get(i)
            output = '    {}'.format(node.getName())
            if node.getType() == MegaNode.TYPE_FILE:
                output += '   ({} bytes)'.format(node.getSize())
            else:
                output += '   (folder)'
            print(output)


    def do_cd(self, arg):
        """Usage: cd [path]"""
        args = arg.split()
        if len(args) > 1:
            print(self.do_cd.__doc__)
            return
        if self.cwd == None:
            print('INFO: Not logged in')
            return

        if len(args) == 0:
            self.cwd = self._api.getRootNode()
            return

        node = self._api.getNodeByPath(args[0], self.cwd)
        if node == None:
            print('{}: No such file or directory'.format(args[0]))
            return
        if node.getType() == MegaNode.TYPE_FILE:
            print('{}: Not a directory'.format(args[0]))
            return
        self.cwd = node


    def do_get(self, arg):
        """Usage: get remotefile"""
        args = arg.split()
        if len(args) != 1:
            print(self.do_get.__doc__)
            return
        if self.cwd == None:
            print('INFO: Not logged in')
            return

        node = self._api.getNodeByPath(args[0], self.cwd)
        if node == None:
            print('Node not found: {}'.format(args[0]))
            return

        self._api.startDownload(node, './', None, None, False, None, MegaTransfer.COLLISION_CHECK_FINGERPRINT, MegaTransfer.COLLISION_RESOLUTION_NEW_WITH_N)


    def do_put(self, arg):
        """Usage: put localfile"""
        args = arg.split()
        if len(args) != 1:
            print(self.do_put.__doc__)
            return
        if self.cwd == None:
            print('INFO: Not logged in')
            return

        self._api.startUpload(args[0], self.cwd, None, 0, None, False, False, None)


    def do_mkdir(self, arg):
        """Usage: mkdir path"""
        args = arg.split()
        if len(args) != 1:
            print(self.do_mkdir.__doc__)
            return
        if self.cwd == None:
            print('INFO: Not logged in')
            return

        base = self.cwd
        name = args[0]
        if '/' in name or '\\' in name:
            index1 = name.rfind('/')
            index2 = name.rfind('\\')
            index = None
            if index1 > index2:
                index = index1
            else:
                index = index2
            path = name[:index + 1]
            base = self._api.getNodeByPath(path, self.cwd)
            name = name[index + 1:]

            if not name:
                print('{}: Path already exists'.format(path))
                return
            if base == None:
                print('{}: Target path not found'.format(path))
                return

        check = self._api.getNodeByPath(name, base)
        if check != None:
            print('{}: Path already exists'
                  .format(self._api.getNodePath(check)))
            return

        self._api.createFolder(name, base)


    def do_rm(self, arg):
        """Usage: rm path"""
        args = arg.split()
        if len(args) != 1:
            print(self.do_rm.__doc__)
            return
        if self.cwd == None:
            print('INFO: Not logged in')
            return

        node = self._api.getNodeByPath(args[0], self.cwd)
        if node == None:
            print('Node not found: {}'.format(args[0]))
            return

        self._api.remove(node)


    def do_mv(self, arg):
        """Usage: mv srcpath dstpath"""
        args = arg.split()
        if len(args) != 2:
            print(self.do_mv.__doc__)
            return
        if self.cwd == None:
            print('INFO: Not logged in')
            return

        src_node = self._api.getNodeByPath(args[0], self.cwd)
        if src_node == None:
            print('{}: No such file or directory'.format(args[0]))
            return

        name = args[1]
        dst_node = self._api.getNodeByPath(name, self.cwd)
        if (dst_node != None) and (dst_node.getType() == MegaNode.TYPE_FILE):
            print('{}: Not a directory'.format(name))
            return

        if dst_node != None:
            self._api.moveNode(src_node, dst_node)
            return

        if '/' in name or '\\' in name:
            index1 = name.rfind('/')
            index2 = name.rfind('\\')
            index = None
            if index1 > index2:
                index = index1
            else:
                index = index2
            path = name[:index + 1]
            base = self._api.getNodeByPath(path, self.cwd)
            name = name[index + 1:]

            if base == None:
                print('{}: No such directory'.format(path))
                return

            if base.getType() == MegaNode.TYPE_FILE:
                print('{}: Not a directory'.format(path))
                return

            self._api.moveNode(src_node, base)
            if len(name) != 0:
                self._api.renameNode(src_node, name)
            return

        if dst_node == None:
            self._api.renameNode(src_node, name)
            return


    def do_pwd(self, arg):
        """Usage: pwd"""
        args = arg.split()
        if len(args) != 0:
            print(self.do_pwd.__doc__)
            return
        if self.cwd == None:
            print('INFO: Not logged in')
            return

        print('{} INFO: Current working directory: {}'
              .format(self.PROMPT, self._api.getNodePath(self.cwd)))


    def do_export(self, arg):
        """Usage: export path"""
        args = arg.split()
        if len(args) != 1:
            print(self.do_export.__doc__)
            return
        if self.cwd == None:
            print('INFO: Not logged in')
            return

        node = self._api.getNodeByPath(args[0], self.cwd)
        self._api.exportNode(node)


    def do_import(self, arg):
        """Usage: import exportedfilelink#key"""
        args = arg.split()
        if len(args) != 1:
            print(self.do_import.__doc__)
            return
        if self.cwd == None:
            print('INFO: Not logged in')
            return

        self._api.importFileLink(args[0], self.cwd)


    def do_whoami(self, arg):
        """Usage: whoami"""
        args = arg.split()
        if len(args) != 0:
            print(self.do_whoami.__doc__)
            return
        if not self._api.isLoggedIn():
            print('INFO: Not logged in')
            return
        print(self._api.getMyEmail())
        self._api.getAccountDetails()


    def do_passwd(self, arg):
        """Usage: passwd <old password> <new password> <new password>"""
        args = arg.split()
        if len(args) != 3:
            print(self.do_passwd.__doc__)
            return
        if not self._api.isLoggedIn():
            print('INFO: Not logged in')
            return

        if args[1] != args[2]:
            print('Mismatch, please try again')
            return

        self._api.changePassword(args[0], args[1])


    def do_quit(self, arg):
        """Usage: quit"""
        del self._api
        print('Bye!')
        return True


    def do_exit(self, arg):
        """Usage: exit"""
        del self._api
        print('Bye!')
        return True


if __name__ == '__main__':
    # Set up logging.
    logging.basicConfig(level=logging.INFO,
                        #filename='runner.log',
                        format='%(levelname)s\t%(asctime)s %(message)s')
    # Do the work.
    api = MegaApi('ox8xnQZL', None, None, 'Python megacli')
    shell = MegaShell(api)
    listener = AppListener(shell)
    api.addListener(listener)
    api = None
    shell.cmdloop()
    
