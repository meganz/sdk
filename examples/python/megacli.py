import sys
import os
import time
import cmd

libsdir = os.getcwd() + '/../../bindings/python'
if os.path.isdir(libsdir) and os.path.isfile(libsdir + '/.libs/_mega.so'):
	sys.path.insert(0, libsdir) # mega.py
	sys.path.insert(0, libsdir + '/.libs') # _mega.so

from mega import *

api = None
cwd = None

class AppListener(MegaListener):
	def println(self, arg):
		print(arg)
		sys.stdout.write('(MEGA) ')
		sys.stdout.flush()
		
	def onRequestStart(self, api, request):
		self.println("INFO: Request start ( " + str(request) + " )")
		
	def onRequestFinish(self, api, request, error):
		self.println("INFO: Request finished ( " + str(request) + " )   Result: " +  str(error))
		if(error.getErrorCode() != MegaError.API_OK): return;
		
		requestType = request.getType()
		if(requestType == MegaRequest.TYPE_LOGIN): api.fetchNodes();
		elif(requestType == MegaRequest.TYPE_EXPORT): self.println("INFO: Exported link: " + request.getLink());
		elif(requestType == MegaRequest.TYPE_ACCOUNT_DETAILS):
			accountDetails = request.getMegaAccountDetails();
			self.println("INFO: Account details received"); 
			self.println("Account e-mail: " + api.getMyEmail());
			self.println("Storage: " + str(accountDetails.getStorageUsed()) + " of " + str(accountDetails.getStorageMax()) + " (" + str(100*accountDetails.getStorageUsed()/accountDetails.getStorageMax()) + "%)");
			self.println("Pro level: " + str(accountDetails.getProLevel()));
	
	def onRequestTemporaryError(self, api, request, error):
		self.println("INFO: Request temporary error ( "+ str(request) +" )   Error: " + str(error))
	
				
	def onTransferFinish(self, api, transfer, error):
		self.println("INFO: Transfer finished ( " + str(transfer) + " " + transfer.getFileName() + " )   Result: " + str(error))
		
	def onTransferUpdate(self, api, transfer):
		self.println("INFO: Transfer update ( " + str(transfer) + " " + transfer.getFileName() + " )   Progress: " + str(transfer.getTransferredBytes()/1024) + " KB of " + str(transfer.getTotalBytes()/1024) + " KB, " + str(transfer.getSpeed()/1024) + " KB/s")
		
	def onTransferTemporaryError(self, api, transfer, error):
		self.println("INFO: Transfer temporary error ( " + str(transfer) + " " + transfer.getFileName() + " )   Error: " + str(error))
		
	def onUsersUpdate(self, api, users):
		self.println("INFO: Users updated ( "+ str(users.size()) + " )")

	def onNodesUpdate(self, api, nodes):
		if(nodes != None): self.println("INFO: Nodes updated ( " + str(nodes.size()) + " )"); 
		else:
			global cwd
			cwd = api.getRootNode();


class MegaShell(cmd.Cmd, MegaListener):
	intro = 'Mega sample app. Type help or ? to list commands.\n'
	prompt = '(MEGA) '
	file = None
	
	def emptyline(self):
		return
		
	def do_login(self, arg):
		'Usage: login email password'
		args = arg.split()
		if len(args) != 2 or not "@" in args[0]:
			print self.do_login.__doc__
			return
		
		api.login(args[0], args[1])
		
	def do_logout(self, arg):
		'Usage: logout'
		
		args = arg.split()
		if len(args) != 0:
			print self.do_logout.__doc__
			return
			
		global cwd
		if cwd == None:
			print("INFO: Not logged in")
			return
			
		api.logout();
		cwd = None
			
	def do_mount(self, arg):
		'Usage: mount'
		args = arg.split()
		if len(args) != 0:
			print self.do_mount.__doc__
			return
		if(not api.isLoggedIn()):
			print("INFO: Not logged in");
			return;

		print("INFO: INSHARES:");
		users = api.getContacts();
		for i in range(users.size()):
			user = users.get(i);
			if(user.getVisibility() == MegaUser.VISIBILITY_VISIBLE):
				shares = api.getInShares(user);
				for j in range(shares.size()):
					share = shares.get(j);
					print("INFO: INSHARE on " + users.get(i).getEmail() + " " + share.getName() + " Access level: " + str(api.getAccess(share)));

	def do_ls(self, arg):
		'Usage: ls [path]'
		args = arg.split()
		if len(args) > 1:
			print self.do_ls.__doc__
			return
			
		global cwd
		if cwd == None:
			print("INFO: Not logged in")
			return
			
		path = None;
		if(len(args)==0): path = cwd
		else: path = api.getNodeByPath(args[0], cwd)
			
		print("    .");
		if(api.getParentNode(path) != None): print("    ..")

		nodelist = api.getChildren(path);
		for i in range (nodelist.size()):
			node = nodelist.get(i);
			sys.stdout.write("    " + node.getName());
			if(node.getType() == MegaNode.TYPE_FILE): print("   (" + str(node.getSize()) + " bytes)")
			else: print("   (folder)");
		
	def do_cd(self, arg):
		'Usage: cd [path]'
		args = arg.split()
		if len(args) > 1:
			print self.do_cd.__doc__
			return
		global cwd
		if cwd == None:
			print("INFO: Not logged in")
			return
			
		if(len(args)==0):
			cwd = api.getRootNode();
			return;
			
		node = api.getNodeByPath(args[0], cwd)
		if(node == None):
			print(args[0] + ": No such file or directory");
			return;
		if(node.getType() == MegaNode.TYPE_FILE):
			print(args[0] + ": Not a directory"); 
			return;
		cwd = node;
		
	def do_get(self, arg):
		'Usage: get remotefile'
		args = arg.split()
		if len(args) != 1:
			print self.do_get.__doc__
			return
		global cwd;
		if(cwd == None):
			print("INFO: Not logged in"); 
			return;
			
		node = api.getNodeByPath(args[0], cwd);
		if(node == None):
			print("Node not found: " + args[0]);
			return;
			
		api.startDownload(node, "./");
		
	def do_put(self, arg):
		'Usage: put localfile'
		args = arg.split()
		if len(args) != 1:
			print self.do_put.__doc__
			return
		global cwd;
		if(cwd == None):
			print("INFO: Not logged in"); 
			return;
			
		api.startUpload(args[0], cwd);

	def do_mkdir(self, arg):
		'Usage: mkdir path'
		args = arg.split()
		if len(args) != 1:
			print self.do_mkdir.__doc__
			return
		global cwd;
		if(cwd == None):
			print("INFO: Not logged in"); 
			return;	

		base = cwd;
		name = args[0]
		if("/" in name or "\\" in name):
			index1 = name.rfind("/");
			index2 = name.rfind("\\");
			index = None
			if(index1 > index2): index = index1
			else: index = index2;
			path = name[:index+1];
			base = api.getNodeByPath(path, cwd);
			name = name[index+1:];
			
			if(len(name)==0): 
				print(path + ": Path already exists"); 
				return;
			if(base == None): 
				print(path + ": Target path not found"); 
				return;	
		
		check = api.getNodeByPath(name, base);
		if (check != None): 
			print(api.getNodePath(check) + ": Path already exists"); 
			return;
			
		api.createFolder(name, base)
		
					
	def do_rm(self, arg):
		'Usage: rm path'
		args = arg.split()
		if len(args) != 1:
			print self.do_rm.__doc__
			return
		global cwd;
		if(cwd == None):
			print("INFO: Not logged in"); 
			return;	
			
		node = api.getNodeByPath(args[0], cwd);
		if(node == None):
			print("Node not found: " + args[0]);
			return;
			
		api.remove(node);
		
	def do_mv(self, arg):
		'Usage: mv srcpath dstpath'
		args = arg.split()
		if len(args) != 2:
			print self.do_mv.__doc__
			return
		global cwd;
		if(cwd == None):
			print("INFO: Not logged in"); 
			return;	

		srcNode = api.getNodeByPath(args[0], cwd);
		if(srcNode == None):
			print(args[0] + ": No such file or directory");
			return;

		name = args[1];
		dstNode = api.getNodeByPath(name, cwd);
		if((dstNode != None) and (dstNode.getType() == MegaNode.TYPE_FILE)):
			print(name + ": Not a directory");
			return;
		
		if(dstNode != None):
			api.moveNode(srcNode, dstNode);
			return;

		if("/" in name or "\\" in name):			
			index1 = name.rfind("/");
			index2 = name.rfind("\\");
			index = None
			if(index1 > index2): index = index1
			else: index = index2;
			path = name[:index+1];
			base = api.getNodeByPath(path, cwd);
			name = name[index+1:];			
						
			if(base == None):
				print(path + ": No such directory");
				return;
									
			if(base.getType() == MegaNode.TYPE_FILE):
				print(path + ": Not a directory");
				return;

			api.moveNode(srcNode, base);
			if(len(name)!=0): api.renameNode(srcNode, name);
			return;
			
		if(dstNode == None):
			api.renameNode(srcNode, name); 
			return;
				
	def do_pwd(self, arg):
		'Usage: pwd'
		args = arg.split()
		if len(args) != 0:
			print self.do_pwd.__doc__
			return
		global cwd;
		if(cwd == None):
			print("INFO: Not logged in"); 
			return;
		
		print(self.prompt + "INFO: Current working directory: " + api.getNodePath(cwd));

	def do_export(self, arg):
		'Usage: export path'
		args = arg.split()
		if len(args) != 1:
			print self.do_export.__doc__
			return
		global cwd;
		if(cwd == None):
			print("INFO: Not logged in"); 
			return;
		
		node = api.getNodeByPath(args[0], cwd);
		api.exportNode(node);
		
	def do_import(self, arg):
		'Usage: import exportedfilelink#key'
		args = arg.split()
		if len(args) != 1:
			print self.do_import.__doc__
			return
		global cwd;
		if(cwd == None):
			print("INFO: Not logged in"); 
			return;	
		
		api.importFileLink(args[0], cwd);
			
	def do_whoami(self, arg):
		'Usage: whoami'
		args = arg.split()
		if len(args) != 0:
			print self.do_whoami.__doc__
			return
		if(not api.isLoggedIn()):
			print("INFO: Not logged in");
			return;
		print api.getMyEmail()
		api.getAccountDetails();
		
	def do_passwd(self, arg):
		'Usage: passwd <old password> <new password> <new password>'
		args = arg.split()
		if len(args) != 3:
			print self.do_passwd.__doc__
			return
		if(not api.isLoggedIn()):
			print("INFO: Not logged in");
			return;

		if(not (args[1] == args[2])):
			print("Mismatch, please try again");
			return;
		
		api.changePassword(args[0], args[1]);
		
	def do_quit(self, arg):
		'Usage: quit'
		print('Bye!')
		exit()
	
	def do_exit(self, arg):
		'Usage: exit'
		print('Bye!')
		exit()


shell = MegaShell()
listener = AppListener()
api = MegaApi('ox8xnQZL', None, None, 'Python megacli')
api.addListener(listener); 
shell.cmdloop()

