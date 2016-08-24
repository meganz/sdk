import socket, sys, struct, os

debug = False
if (debug): from inspect import currentframe, getframeinfo

def parseArgs(argsin):
	itochange = None
	totalRealArgs = 0
	if argsin[0] in ["sync"]:
		#get first argument (local path) and append fullpath
		for i in range(1,len(argsin)):
			if not argsin[i].startswith("-"):
				if (itochange is None): itochange = i;
				totalRealArgs+=1
		

	
	if argsin[0] == "sync":
		if (itochange is not None and totalRealArgs>=2):
			argsin[itochange]=os.path.abspath(argsin[itochange])

	if argsin[0] in ["get"]:
		#get first argument (local path) and append fullpath
		for i in range(1,len(argsin)):
			if not argsin[i].startswith("-"):
				argsin[i]=os.path.abspath(argsin[i])
				if (totalRealArgs>1): argsin[i]=os.path.abspath(argsin[i])
				totalRealArgs+=1
		


	return argsin;

#sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
#sock.connect(('127.0.0.1',12347))

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
if (debug): print " at ",getframeinfo(currentframe()).lineno
try:
	suid=str(os.getuid())
	sock.connect(('/tmp/megaCMD_'+suid+'/srv'))
	if (debug): print " at ",getframeinfo(currentframe()).lineno

	commandArgs=parseArgs(sys.argv[1:])

	print "sending: "," ".join(commandArgs)

	sock.send(" ".join(commandArgs))
	if (debug): print " at ",getframeinfo(currentframe()).lineno

	data = sock.recv(100);
	if (debug): print " at ",getframeinfo(currentframe()).lineno
	sockOutId, = struct.unpack('H', data[:2])

	if (debug): print " at ",getframeinfo(currentframe()).lineno
	sock.close()

	if (debug): print " at ",getframeinfo(currentframe()).lineno
	sockout =  socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	if (debug): print "connecting to output socket: " + '/tmp/megaCMD_'+suid+'/srv_'+str(sockOutId)
	if (debug): print " at ",getframeinfo(currentframe()).lineno
	sockout.connect(('/tmp/megaCMD_'+suid+'/srv_'+str(sockOutId)))

	if (debug): print " at ",getframeinfo(currentframe()).lineno
	data = sockout.recv(100);
	if (debug): print " at ",getframeinfo(currentframe()).lineno
	commandOutput=""
	if (debug): print " at ",getframeinfo(currentframe()).lineno
	while (len(data)):
	#	out, = struct.unpack('H', data[:2])
		commandOutput+=data
	#	print "data ("+str(len(data))+")= ",str(data)
	#	print "trozo=",out
		data = sockout.recv(100);
	if (debug): print " at ",getframeinfo(currentframe()).lineno

	sockout.close()
	if (debug): print " at ",getframeinfo(currentframe()).lineno

	if "" != commandOutput:
		print commandOutput,

	#print "<"+str(commandOutput)+">",
except Exception as ex:
	print "Unable to connect to service",ex

