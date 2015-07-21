#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Simple CRUD example for the Python bindings."""

## Created: 16 May 2015 Guy Kloss <gk@mega.co.nz>
##
## (c) 2015 by Mega Limited, Auckland, New Zealand
##     https://mega.nz/   
##     Simplified (2-clause) BSD License.
##
## You should have received a copy of the license along with this
## program.
##
## This file is part of the Mega SDK Python bindings example code.
##
## This code is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

__author__ = 'Guy Kloss <gk@mega.co.nz>'

import sys
import os
import logging
import time
import json
import getpass

_wrapper_dir = os.path.join(os.getcwd(), '..', '..', 'bindings', 'python')
_libs_dir = os.path.join(_wrapper_dir, '.libs')
_shared_lib = os.path.join(_libs_dir, '_mega.so')
if os.path.isdir(_wrapper_dir) and os.path.isfile(_shared_lib):
    sys.path.insert(0, _wrapper_dir)  # mega.py
    sys.path.insert(0, _libs_dir)     # _mega.so

from mega import (MegaApi, MegaListener, MegaError, MegaRequest, MegaNode)

APP_KEY = 'ox8xnQZL'

CREDENTIALS_FILE = 'credentials.json'


class AppListener(MegaListener):
    """
    A listener that will contain callback methods called upon events
    from the asyncrhonous API. Only a certain number of usefuul
    callbacks are implemented.
    """
    
    def __init__(self):
        """
        Constructor.
        """
        self.root_node = None
        super(AppListener, self).__init__()


    def onRequestStart(self, api, request):
        """
        Called upon starting the async API operation.

        :param api: Reference to the API object.
        :param request: Reference to the request this operation belongs to.
        """
        logging.info('Request start ({})'.format(request))


    def onRequestFinish(self, api, request, error):
        """
        Called upon finishing the async API operation.

        :param api: Reference to the API object.
        :param request: Reference to the request this operation belongs to.
        :param eror: Error information. API_OK if it finishes without a
            problem.
        """
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
            logging.info('Storage: {} of {} ({} %)'
                         .format(account_details.getStorageUsed(),
                                 account_details.getStorageMax(),
                                 100 * account_details.getStorageUsed()
                                 / account_details.getStorageMax()))
            logging.info('Pro level: {}'.format(account_details.getProLevel()))


    def onRequestTemporaryError(self, api, request, error):
        """
        Called upon a temprary error of the API operation. To be continued
        (involving further callbacks).

        :param api: Reference to the API object.
        :param request: Reference to the request this operation belongs to.
        :param eror: Error information.
        """
        logging.info('Request temporary error ({}); Error: {}'
                     .format(request, error))


    def onTransferFinish(self, api, transfer, error):
        """
        Called upon finishing the async API transfer operation.

        :param api: Reference to the API object.
        :param request: Reference to the request this operation belongs to.
        :param eror: Error information. API_OK if it finishes without a
            problem.
        """
        logging.info('Transfer finished ({}); Result: {}'
                     .format(transfer, transfer.getFileName(), error))


    def onTransferUpdate(self, api, transfer):
        """
        Called intermittently to provide updates on the async API transfer
        operation.

        :param api: Reference to the API object.
        :param transfer: Information about the transfer.
        """
        logging.info('Transfer update ({} {});'
                     ' Progress: {} KB of {} KB, {} KB/s'
                     .format(transfer,
                             transfer.getFileName(),
                             transfer.getTransferredBytes() / 1024,
                             transfer.getTotalBytes() / 1024,
                             transfer.getSpeed() / 1024))


    def onTransferTemporaryError(self, api, transfer, error):
        """
        Called upon a temprary error of the API transfer operation. To be
        continued (involving further callbacks).

        :param api: Reference to the API object.
        :param transfer: Information about the transfer.
        :param eror: Error information.
        """
        logging.info('Transfer temporary error ({} {}); Error: {}'
                     .format(transfer, transfer.getFileName(), error))


    def onUsersUpdate(self, api, users):
        """
        Called upon an update from the API to a user's contacts.

        :param api: Reference to the API object.
        :param users: List that contains the new or updated contacts.
        """
        logging.info('Users updated ({})'.format(users.size()))


    def onNodesUpdate(self, api, nodes):
        """
        Called upon an update from the API for changed/updated storage nodes.

        :param api: Reference to the API object.
        :param nodes: List that contains the new or updated nodes.
        """
        if nodes != None:
            logging.info('Nodes updated ({})'.format(nodes.size()))
        else:
            self.root_node = api.getRootNode()


def worker(api, listener, credentials):
    """
    Sequential collection of (CRUD) operations on the Mega API.

    :param api: Reference to the Mega API object.
    :param listener: Listener to receie callbacks and results from the
        Mega API.
    :param executor: Simple asynchronous API command executor to enable
        a sequential/synchronous flow of operations in this function.
    :param credentials: Dictionary containing a `user` and a `password`
        string to log into the Mega account.
    """
    # Log in.
    logging.info('*** start: login ***')
    api.login(str(credentials['user']),
              str(credentials['password']))
    cwd = listener.root_node
    logging.info('*** done: login ***')

    # Who am I.
    logging.info('*** start: whoami ***')
    logging.info('My email: {}'.format(api.getMyEmail()))
    api.getAccountDetails()
    time.sleep(1)
    logging.info('*** done: whoami ***')

    # Make a directory.
    logging.info('*** start: mkdir ***')
    check = api.getNodeByPath('sandbox', cwd)
    if check == None:
        api.createFolder('sandbox', cwd)
        time.sleep(1)
    else:
        logging.warn('Path already exists: {}'
                     .format(api.getNodePath(check)))
    logging.info('*** done: mkdir ***')

    # Now go and play in the sandbox.
    logging.info('*** start: cd ***')
    node = api.getNodeByPath('sandbox', cwd)
    if node == None:
        logging.warn('No such file or directory: sandbox')
    if node.getType() == MegaNode.TYPE_FOLDER:
        cwd = node
    else:
        logging.warn('Not a directory: sandbox')
    logging.info('*** done: cd ***')

    # Upload a file (create).
    logging.info('*** start: upload ***')
    api.startUpload('README.md', cwd)
    time.sleep(1)
    logging.info('*** done: upload ***')

    # Download a file (read).
    logging.info('*** start: download ***')
    node = api.getNodeByPath('README.md', cwd)
    if node != None:
        api.startDownload(node, 'README_returned.md')
        time.sleep(1)
    else:
        logging.warn('Node not found: {}'.format('README.md'))
    logging.info('*** done: download ***')

    # Change a file (update).
    # Note: A new upload won't overwrite, but create a new node with same
    #       name!
    logging.info('*** start: update ***')
    old_node = api.getNodeByPath('README.md', cwd)
    api.startUpload('README.md', cwd)
    time.sleep(1)
    if old_node != None:
        # Remove the old node with the same name.
        api.remove(old_node)
        time.sleep(1)
    else:
        logging.info('No old file node needs removing')
    logging.info('*** done: update ***')

    # Delete a file.
    logging.info('*** start: delete ***')
    node = api.getNodeByPath('README.md', cwd)
    if node != None:
        api.remove(node)
        time.sleep(1)
    else:
        logging.warn('Node not found: README.md')
    logging.info('*** done: delete ***')

    # Logout.
    logging.info('*** start: logout ***')
    api.logout()
    time.sleep(1)
    listener.root_node = None
    logging.info('*** done: logout ***')


def main():
    """
    Sets up all that is needed to make (asynchronous) calls on the Mega API,
    then executs the worker to perform the desired operations.
    """
    # Get credentials.
    logging.info('Obtaining Mega login credentials.')
    credentials = {}
    if os.path.exists(CREDENTIALS_FILE):
        credentials = json.load(open(CREDENTIALS_FILE))
    else:
        credentials['user'] = raw_input('User: ')
        credentials['password'] = getpass.getpass()
    
    # Create the required Mega API objects.
    api = MegaApi(APP_KEY, None, None, 'Python CRUD example')
    listener = AppListener()
    api.addListener(listener)

    # Run the operations.
    start_time = time.time()
    worker(api, listener, credentials)
    logging.info('Total time taken: {} s'.format(time.time() - start_time))


if __name__ == '__main__':
    # Set up logging.
    logging.basicConfig(level=logging.INFO,
                        #filename='runner.log',
                        format='%(levelname)s\t%(asctime)s (%(threadName)-10s) %(message)s')

    # Do the work.
    main()
