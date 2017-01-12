/**
 * @file examples/linux/megafuse.cpp
 * @brief Example MEGA filesystem based on FUSE
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

// This example implements the following operations: getattr, readdir,
// open, read, mkdir, rmdir, unlink and rename.
// File writes are NOT supported yet.
// There isn't any caching nor the implementation does any prefetching
// to improve read performance.

#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <megaapi.h>
#include <unistd.h>
#include <termios.h>
#include <mutex>
#include <condition_variable>
#include <iostream>

using namespace mega;
using namespace std;

MegaApi* megaApi;
string megaBasePath;

class SynchronousRequestListenerFuse : public MegaRequestListener
{
	public:
		SynchronousRequestListenerFuse()
		{
			request = NULL;
			error = NULL;
			notified = false;
		}
		
		~SynchronousRequestListenerFuse()
		{
			delete request;
			delete error;
		}
		
		void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *error) 
		{
			this->error = error->copy();
			this->request = request->copy();
			
			{
				unique_lock<mutex> lock(m);
				notified = true;
			}
			cv.notify_all();
		}
		
		void wait() 
		{
			unique_lock<mutex> lock(m);
			cv.wait(lock, [this]{return notified;});
		}
		
		void reset()
		{
			delete request;
			delete error;
			request = NULL;
			error = NULL;
			notified = false;
		}
		
		MegaRequest *getRequest()
		{
			return request;
		}
		
		MegaError *getError()
		{
			return error;
		}
		
	private:
		bool notified;
		MegaError *error;
		MegaRequest *request;
		condition_variable cv;
		mutex m;
};

class SynchronousTransferListenerFuse : public MegaTransferListener
{
	public:
		SynchronousTransferListenerFuse()
		{
			transfer = NULL;
			error = NULL;
			notified = false;
		}
		
		~SynchronousTransferListenerFuse()
		{
			delete transfer;
			delete error;
		}
		
		void onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error) 
		{
			this->error = error->copy();
			this->transfer = transfer->copy();
		
			{
				unique_lock<mutex> lock(m);
				notified = true;
			}
			cv.notify_all();
		}
				
		bool onTransferData(MegaApi *api, MegaTransfer *transfer, char *buffer, size_t s)
		{
			data.append(buffer, s);
			return true;
		}
		
		void wait() 
		{
			unique_lock<mutex> lock(m);
			cv.wait(lock, [this]{return notified;});
		}
		
		void reset()
		{
			delete transfer;
			delete error;
			transfer = NULL;
			error = NULL;
			notified = false;
		}
		
		MegaTransfer *getTransfer()
		{
			return transfer;
		}
		
		MegaError *getError()
		{
			return error;
		}
		
		const char *getData()
		{
			return data.data();
		}
		
		long long getDataSize()
		{
			return data.size();
		}
		
	private:
		bool notified;
		MegaError *error;
		MegaTransfer *transfer;
		string data;
		condition_variable cv;
		mutex m;
};

static int MEGAgetattr(const char *p, struct stat *stbuf)
{
	string path = megaBasePath + p;
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Getting attributes:");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());

	MegaNode *n = megaApi->getNodeByPath(path.c_str());
	if (!n)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Node not found");
		return -ENOENT;
	}
	
	stbuf->st_uid = getuid();
	stbuf->st_gid = getgid();
	stbuf->st_mode = n->isFile() ? S_IFREG | 0444 : S_IFDIR | 0755;
	stbuf->st_nlink = 1;
	stbuf->st_size = n->isFile() ? n->getSize() : 4096;
	stbuf->st_mtime = n->isFile() ? n->getModificationTime() : n->getCreationTime();
		
	delete n;
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Attributes read OK");
	return 0;
}

static int MEGAmkdir(const char *p, mode_t mode)
{
	string path = megaBasePath + p;
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Creating folder:");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	
	MegaNode *n = megaApi->getNodeByPath(path.c_str());
	if (n)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "Path already exists");
		delete n;
		return -EEXIST;
	}
	
	string spath = path;
	size_t index = spath.find_last_of('/');
	if (index == string::npos)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Invalid path");
		return -ENOENT;
	}
	
	spath.resize(index + 1);
	n = megaApi->getNodeByPath(spath.c_str());
	if (!n || n->isFile())
	{
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "Parent folder not found");

		delete n;
		return -ENOTDIR;
	}

	SynchronousRequestListenerFuse listener;
	megaApi->createFolder(path.c_str() + index + 1, n, &listener);
	listener.wait();
	delete n;
	
	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error creating folder");
		return -EIO;
	}

	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Folder created OK");
	return 0;
}

static int MEGArmdir(const char *p)
{
	string path = megaBasePath + p;
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Deleting folder:");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	
	MegaNode *n = megaApi->getNodeByPath(path.c_str());
	if (!n)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "Folder not found");
		return -ENOENT;
	}
	
	if (n->isFile())
	{
		delete n;
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "The path isn't a folder");
		return -ENOTDIR;
	}
	
	if (megaApi->getNumChildren(n))
	{
		delete n;
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "Folder not empty");
		return -ENOTEMPTY;
	}
	
	SynchronousRequestListenerFuse listener;	
	megaApi->remove(n, &listener);
	listener.wait();
	delete n;
	
	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error deleting folder");
		return -EIO;
	}

	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Folder deleted OK");
	return 0;
}

static int MEGAunlink(const char *p)
{
	string path = megaBasePath + p;

	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Deleting file:");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	
	MegaNode *n = megaApi->getNodeByPath(path.c_str());
	if (!n)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "File not found");
		return -ENOENT;
	}
	
	if (!n->isFile())
	{
		delete n;
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "The path isn't a file");
		return -EISDIR;
	}
	
	SynchronousRequestListenerFuse listener;	
	megaApi->remove(n, &listener);
	listener.wait();
	delete n;
	
	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error deleting file");
		return -EIO;
	}

	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "File deleted OK");
	return 0;
}


static int MEGArename(const char *f, const char *t)
{
	string from = megaBasePath + f;
	string to = megaBasePath + t;

	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Renaming/moving file/folder");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, from.c_str());
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, to.c_str());

	MegaNode *source = megaApi->getNodeByPath(from.c_str());
	if (!source)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "Source not found");
		return -ENOENT;
	}
	
	MegaNode *dest = megaApi->getNodeByPath(to.c_str());
	if (dest)
	{
		if (dest->isFile())
		{
			delete source;
			delete dest;
			MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "The destination is an existing file");
			return -ENOTDIR;
		}
		else
		{
			SynchronousRequestListenerFuse listener;	
			megaApi->moveNode(source, dest, &listener);
			listener.wait();
			delete source;
			delete dest;
			
			if (listener.getError()->getErrorCode() != MegaError::API_OK)
			{
				MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error moving file/folder");
				return -EIO;
			}

			MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "File/folder moved OK");
			return 0;
		}
	}
	
	string destpath = to;
	size_t index = destpath.find_last_of('/');
	if (index == string::npos)
	{
		delete source;
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Invalid path");
		return -ENOENT;
	}
	
	string destname = destpath.c_str() + index + 1;
	destpath.resize(index + 1);
	dest = megaApi->getNodeByPath(destpath.c_str());
	if (!dest)
	{
		delete source;
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "Destination folder not found");
		return -ENOENT;
	}
	
	if (dest->isFile())
	{
		delete source;
		delete dest;
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "The destination folder is a file");
		return -ENOTDIR;
	}
	
	MegaNode *n = megaApi->getChildNode(dest, destname.c_str());
	if(n)
	{
		delete n;
		delete source;
		delete dest;
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "The destination path already exists");
		return -EEXIST;
	}
	
	SynchronousRequestListenerFuse listener;	
	megaApi->moveNode(source, dest, &listener);
	listener.wait();
	delete dest;
	
	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		delete source;
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error moving file/folder");
		return -EIO;
	}
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "File/folder moved OK");

	if (strcmp(source->getName(), destname.c_str()))
	{
		listener.reset();
		megaApi->renameNode(source, destname.c_str(), &listener);
		listener.wait();
		
		if(listener.getError()->getErrorCode() != MegaError::API_OK)
		{
			delete source;
			MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error renaming file/folder");
			return -EIO;
		}
		
		MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "File/folder renamed OK");
	}
	
	delete source;
	return 0;	
}

static int MEGAreaddir(const char *p, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
	string path = megaBasePath + p;
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Listing folder:");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());

	MegaNode *node = megaApi->getNodeByPath(path.c_str());
	if (!node)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Folder not found");
		return -ENOENT;
	}
	
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	MegaNodeList *children = megaApi->getChildren(node);
	for (int i=0; i<children->size(); i++)
	{
		MegaNode *n = children->get(i);
		filler(buf, n->getName(), NULL, 0);
		MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, n->getName());
	}
	
	delete node;
	delete children;

	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Folder listed OK");	
	return 0;
}

static int MEGAopen(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

static int MEGAread(const char *p, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
	string path = megaBasePath + p;
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Reading file:");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
        	
	MegaNode *node = megaApi->getNodeByPath(path.c_str());
	if (!node)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "File not found");
		return -ENOENT;
	}	
	
	if (offset >= node->getSize())
	{
		delete node;
		return 0;
	}
	
	if (offset + size > node->getSize())
	{
		size = node->getSize() - offset;
	}
	
	SynchronousTransferListenerFuse listener;
	megaApi->startStreaming(node, offset, size, &listener);
	listener.wait();
	delete node;
	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Transfer error");
		return -EIO;
	}
	
	if (listener.getDataSize() != size)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Internal error");
		return -EIO;
	}
	
	memcpy(buf, listener.getData(), size);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "File read OK");
    return size;
}

int main(int argc, char *argv[])
{
	string megauser;
	string megapassword;
	string mountpoint;
	if (argc != 1 && argc != 4 && argc != 5)
	{
		cout << "Usage: " << argv[0] << " [megauser megapassword localmountpoint [megamountpoint]]" << endl; 
		return 0;
	}
	
	if (argc == 1)
	{
		cout << "MEGA email: ";
		getline(cin, megauser);
		
		struct termios oldt;
		tcgetattr(STDIN_FILENO, &oldt);
		struct termios newt = oldt;
		newt.c_lflag &= ~(ECHO);
		tcsetattr( STDIN_FILENO, TCSANOW, &newt);
		cout << "MEGA password (won't be shown): ";
		getline(cin, megapassword);
		tcsetattr( STDIN_FILENO, TCSANOW, &oldt);
		cout << endl;
		
		cout << "Local mountpoint: ";
		getline(cin, mountpoint);	
		
		cout << "MEGA mountpoint (default /): ";
		getline(cin, megaBasePath);		
	}
	else if (argc == 4 || argc == 5)
	{
		megauser = argv[1];
		megapassword = argv[2];
		mountpoint = argv[3];
		
		if(argc == 5)
		{
			megaBasePath = argv[4];
		}
	}	

	if (megaBasePath.size() && megaBasePath[megaBasePath.size()-1] == '/')
	{
		megaBasePath.resize(megaBasePath.size()-1);
	}
			
	megaApi = new MegaApi("BhU0CKAT", (const char*)NULL, "MEGA/SDK FUSE filesystem");
	megaApi->setLogLevel(MegaApi::LOG_LEVEL_INFO);
	
	//Login
	SynchronousRequestListenerFuse listener;
	megaApi->login(megauser.c_str(), megapassword.c_str(), &listener);
	listener.wait();
	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Login error");
		return 0;
	}
	MegaApi::log(MegaApi::LOG_LEVEL_INFO, "Login OK. Fetching nodes");	
	
	//Fetch nodes
	listener.reset();
	megaApi->fetchNodes(&listener);
	listener.wait();
	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error fetchning nodes");	
		return 0;
	}
	
	if (megaBasePath.size())
    {
		MegaNode *baseNode = megaApi->getNodeByPath(megaBasePath.c_str());
		if (!baseNode)
		{
			MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "MEGA mountpoint not found");	
			return 0;
		}
		delete baseNode;
	}
		
	MegaApi::log(MegaApi::LOG_LEVEL_INFO, "MEGA initialization complete!");	
	megaApi->setLogLevel(MegaApi::LOG_LEVEL_WARNING);

	//Start FUSE
	struct fuse_operations ops = {0};
    ops.getattr     = MEGAgetattr;
    ops.readdir     = MEGAreaddir;
    ops.open        = MEGAopen;
    ops.read		= MEGAread;
    ops.mkdir		= MEGAmkdir;
    ops.rmdir		= MEGArmdir;
    ops.unlink		= MEGAunlink;
	ops.rename		= MEGArename;
    
	char *fuseargv[3] = { argv[0], (char *)"-f", (char *)mountpoint.c_str()};
    return fuse_main(3, fuseargv, &ops, NULL);
}
