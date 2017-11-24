/**
* @file examples/win32/MEGAdokan/MEGAdokan.cpp
* @brief Example MEGA filesystem for Windows based on Dokan
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

// This isn't a final product, please use it for testing/development purposes only.
// File writes are NOT supported yet.
// There isn't any caching nor the implementation does any prefetching
// to improve read performance so the performance will be poor for many operations.

#include <windows.h>
#include <iostream>
#include <string>
#include <megaapi.h>
#include <dokan.h>

using namespace mega;
using namespace std;

//SETTINGS

//Put your access credentials for MEGA here
#define MEGA_USER_EMAIL "EMAIL"
#define MEGA_USER_PASSWORD "PASSWORD"

//App key. Please generate yours at https://mega.co.nz/#sdk
const char APP_KEY[] = "ht1gUZLZ";

//MEGA mountpoint
const char MEGA_MOUNTPOINT[] = "/";

//Local mountpoint (without a backslash)
const wchar_t LOCAL_MOUNTPOINT[] = L"M:";


const int ENABLE_DEBUG = 0;
const wchar_t DRIVE_LABEL[] = L"MEGA";

//Global variables
MegaApi* megaApi;
string megaBasePath;

//Helper objects
class SynchronousDataTransferListener : public SynchronousTransferListener
{
public:
	bool onTransferData(MegaApi *api, MegaTransfer *transfer, char *buffer, size_t s)
	{
		data.append(buffer, s);
		return true;
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
	string data;
};
////

void MEGAGetFilePath(string *filePath, const wchar_t* FileName)
{
	string utf8string;
	MegaApi::utf16ToUtf8(FileName, wcslen(FileName), &utf8string);
	char *buf = (char *)utf8string.data();
	for (unsigned int i = 0; i < utf8string.size(); i++)
	{
		if (buf[i] == '\\')
		{
			buf[i] = '/';
		}
	}
	*filePath = megaBasePath + utf8string;
}

static int __stdcall MEGACreateFile(
	LPCWSTR					FileName,
	DWORD					AccessMode,
	DWORD					ShareMode,
	DWORD					CreationDisposition,
	DWORD					FlagsAndAttributes,
	PDOKAN_FILE_INFO		DokanFileInfo)
{
	if (FlagsAndAttributes & FILE_FLAG_DELETE_ON_CLOSE)
	{
		//Workaround to delete files on Windows 8
		return -ERROR_INVALID_FUNCTION;
	}

	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGACreateFile");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());

	MegaNode *node = megaApi->getNodeByPath(path.c_str());
	if (!node)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGACreateFile Error");
		return -ERROR_FILE_NOT_FOUND;
	}

	DokanFileInfo->Context = (ULONG64)node->getHandle();
	
	delete node;
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGACreateFile OK");
	return 0;
}


static int __stdcall MEGACreateDirectory(
	LPCWSTR					FileName,
	PDOKAN_FILE_INFO		DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGACreateDirectory");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());

	MegaNode *n = megaApi->getNodeByPath(path.c_str());
	if (n)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "Path already exists");
		delete n;
		return -ERROR_ALREADY_EXISTS;
	}

	string spath = path;
	size_t index = spath.find_last_of('/');
	if (index == string::npos)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Invalid path");
		return -ERROR_PATH_NOT_FOUND;
	}

	spath.resize(index + 1);
	n = megaApi->getNodeByPath(spath.c_str());
	if (!n || n->isFile())
	{
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "Parent folder not found");

		delete n;
		return -ERROR_PATH_NOT_FOUND;
	}

	SynchronousRequestListener listener;
	megaApi->createFolder(path.c_str() + index + 1, n, &listener);
	listener.wait();
	delete n;

	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error creating folder");
		return -ERROR_IO_DEVICE;
	}

	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Folder created OK");
	return 0;
}


static int __stdcall MEGAOpenDirectory(
	LPCWSTR					FileName,
	PDOKAN_FILE_INFO		DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAOpenDirectory");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());

	MegaNode *node = megaApi->getNodeByPath(path.c_str());
	if (!node || node->isFile())
	{
		delete node;
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "Folder not found");
		return -ERROR_PATH_NOT_FOUND;
	}

	DokanFileInfo->Context = node->getHandle();
	
	delete node;
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAOpenDirectory OK");
	return 0;
}


static int __stdcall MEGACloseFile(
	LPCWSTR					FileName,
	PDOKAN_FILE_INFO		DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGACloseFile");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	return 0;
}


static int __stdcall MEGACleanup(
	LPCWSTR					FileName,
	PDOKAN_FILE_INFO		DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGACleanup");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	return 0;
}


static int __stdcall MEGAReadFile(
	LPCWSTR				FileName,
	LPVOID				Buffer,
	DWORD				size,
	LPDWORD				ReadLength,
	LONGLONG			offset,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAReadFile");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());

	MegaNode *node = megaApi->getNodeByPath(path.c_str());
	if (!node)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "File not found");
		return -ERROR_INVALID_HANDLE;
	}

	if (!node->isFile())
	{
		delete node;
		return -ERROR_INVALID_HANDLE;
	}

	if (offset >= node->getSize())
	{
		delete node;
		return -ERROR_HANDLE_EOF;
	}

	if (offset + size > node->getSize())
	{
		*ReadLength = node->getSize() - offset;
	}
	else
	{
		*ReadLength = size;
	}

	DokanResetTimeout(60000, DokanFileInfo);
	SynchronousDataTransferListener listener;
	megaApi->startStreaming(node, offset, *ReadLength, &listener);
	listener.wait();
	delete node;

	if (listener.getError()->getErrorCode() != MegaError::API_OK || listener.getDataSize() != *ReadLength)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Transfer error");
		return -ERROR_IO_DEVICE;
	}

	memcpy(Buffer, listener.getData(), *ReadLength);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "File read OK");
	return 0;
}


static int __stdcall MEGAWriteFile(
	LPCWSTR		FileName,
	LPCVOID		Buffer,
	DWORD		NumberOfBytesToWrite,
	LPDWORD		NumberOfBytesWritten,
	LONGLONG			Offset,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAWriteFile");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	return -ERROR_ACCESS_DENIED;
}


static int __stdcall MEGAFlushFileBuffers(
	LPCWSTR		FileName,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAFlushFileBuffers");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	return 0;
}


static int __stdcall MEGAGetFileInformation(
	LPCWSTR							FileName,
	LPBY_HANDLE_FILE_INFORMATION	HandleFileInformation,
	PDOKAN_FILE_INFO				DokanFileInfo)
{
	LONGLONG ll;
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAGetFileInformation");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());

	MegaNode *node = megaApi->getNodeByPath(path.c_str());
	if (!node)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "File not found");
		return -ERROR_FILE_NOT_FOUND;
	}

	HandleFileInformation->dwFileAttributes = node->isFile() ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_DIRECTORY;

	HandleFileInformation->ftLastAccessTime.dwHighDateTime = 0;
	HandleFileInformation->ftLastAccessTime.dwLowDateTime = 0;

	ll = (node->getCreationTime() + 11644473600) * 10000000;
	HandleFileInformation->ftCreationTime.dwLowDateTime = (DWORD)ll;
	HandleFileInformation->ftCreationTime.dwHighDateTime = ll >> 32;

	ll = (node->getModificationTime() + 11644473600) * 10000000;
	HandleFileInformation->ftLastWriteTime.dwLowDateTime = (DWORD)ll;
	HandleFileInformation->ftLastWriteTime.dwHighDateTime = ll >> 32;

	HandleFileInformation->dwVolumeSerialNumber = 0x19831116;
	HandleFileInformation->nFileSizeHigh		= (node->getSize() >> 32) & 0xFFFFFFFF;
	HandleFileInformation->nFileSizeLow			= (node->getSize() & 0xFFFFFFFF);
	HandleFileInformation->nNumberOfLinks		= 1;
	HandleFileInformation->nFileIndexHigh		= (node->getHandle() >> 32) & 0xFFFFFFFF;
	HandleFileInformation->nFileIndexLow		= (node->getHandle() & 0xFFFFFFFF);
	
	delete node;
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAGetFileInformation OK");
	return 0;
}


static int __stdcall MEGAFindFiles(
	LPCWSTR				FileName,
	PFillFindData		FillFindData,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	WIN32_FIND_DATAW	findData;
	LONGLONG ll;
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAFindFiles");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());

	MegaNode *node = megaApi->getNodeByPath(path.c_str());
	if (!node || node->isFile())
	{
		delete node;
		return -ERROR_INVALID_HANDLE;
	}

	MegaNodeList *list = megaApi->getChildren(node);
	delete node;

	for (int i = 0; i < list->size(); i++)
	{
		node = list->get(i);
		findData.dwFileAttributes	= node->isFile() ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_DIRECTORY;

		findData.ftLastAccessTime.dwHighDateTime = 0;
		findData.ftLastAccessTime.dwLowDateTime = 0;

		ll = (node->getCreationTime() + 11644473600) * 10000000;
		findData.ftCreationTime.dwLowDateTime = (DWORD)ll;
		findData.ftCreationTime.dwHighDateTime = ll >> 32;

		ll = (node->getModificationTime() + 11644473600) * 10000000;
		findData.ftLastWriteTime.dwLowDateTime = (DWORD)ll;
		findData.ftLastWriteTime.dwHighDateTime = ll >> 32;

		findData.nFileSizeHigh		= (node->getSize() >> 32) & 0xFFFFFFFF;
		findData.nFileSizeLow		= (node->getSize() & 0xFFFFFFFF);
		findData.dwReserved0		= 0;
		findData.dwReserved1		= 0;

		string utf16string;
		MegaApi::utf8ToUtf16(node->getName(), &utf16string);
		utf16string.append("", 1);
		memcpy(findData.cFileName, utf16string.data(), utf16string.size() + 1);

		findData.cAlternateFileName[0] = 0;
		FillFindData(&findData, DokanFileInfo);
	}

	delete list;
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAFindFiles OK");
	return 0;
}


static int __stdcall MEGADeleteFile(
	LPCWSTR				FileName,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGADeleteFile");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());

	MegaNode *n = megaApi->getNodeByPath(path.c_str());
	if (!n)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "File not found");
		return -ERROR_FILE_NOT_FOUND;
	}

	if (!n->isFile())
	{
		delete n;
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "The path isn't a file");
		return -ERROR_ACCESS_DENIED;
	}

	SynchronousRequestListener listener;
	megaApi->remove(n, &listener);
	listener.wait();
	delete n;

	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error deleting file");
		return -ERROR_IO_DEVICE;
	}

	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGADeleteFile OK");
	return 0;
}


static int __stdcall MEGADeleteDirectory(
	LPCWSTR				FileName,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGADeleteDirectory");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());

	MegaNode *n = megaApi->getNodeByPath(path.c_str());
	if (!n)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "Folder not found");
		return -ERROR_FILE_NOT_FOUND;
	}

	if (n->isFile())
	{
		delete n;
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "The path isn't a folder");
		return -ERROR_ACCESS_DENIED;
	}

	if (megaApi->getNumChildren(n))
	{
		delete n;
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "Folder not empty");
		return -ERROR_ACCESS_DENIED;
	}

	SynchronousRequestListener listener;
	megaApi->remove(n, &listener);
	listener.wait();
	delete n;

	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error deleting folder");
		return -ERROR_IO_DEVICE;
	}

	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "Folder deleted OK");
	return 0;
}


static int __stdcall MEGAMoveFile(
	LPCWSTR				FileName,
	LPCWSTR				NewFileName,
	BOOL				ReplaceIfExisting,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	string from, to;
	MEGAGetFilePath(&from, FileName);
	MEGAGetFilePath(&to, NewFileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAMoveFile");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, from.c_str());
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, to.c_str());

	MegaNode *source = megaApi->getNodeByPath(from.c_str());
	if (!source)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "Source not found");
		return -ERROR_FILE_NOT_FOUND;
	}

	MegaNode *dest = megaApi->getNodeByPath(to.c_str());
	if (dest)
	{
		if (dest->isFile())
		{
			delete source;
			delete dest;
			MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "The destination is an existing file");
			return -ERROR_ACCESS_DENIED;
		}
		else
		{
			SynchronousRequestListener listener;
			megaApi->moveNode(source, dest, &listener);
			listener.wait();
			delete source;
			delete dest;

			if (listener.getError()->getErrorCode() != MegaError::API_OK)
			{
				MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error moving file/folder");
				return -ERROR_ACCESS_DENIED;
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
		return -ERROR_PATH_NOT_FOUND;
	}

	string destname = destpath.c_str() + index + 1;
	destpath.resize(index + 1);
	dest = megaApi->getNodeByPath(destpath.c_str());
	if (!dest)
	{
		delete source;
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "Destination folder not found");
		return -ERROR_PATH_NOT_FOUND;
	}

	if (dest->isFile())
	{
		delete source;
		delete dest;
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "The destination folder is a file");
		return -ERROR_ACCESS_DENIED;
	}

	MegaNode *n = megaApi->getChildNode(dest, destname.c_str());
	if (n)
	{
		delete n;
		delete source;
		delete dest;
		MegaApi::log(MegaApi::LOG_LEVEL_WARNING, "The destination path already exists");
		return -ERROR_ALREADY_EXISTS;
	}

	SynchronousRequestListener listener;
	megaApi->moveNode(source, dest, &listener);
	listener.wait();
	delete dest;

	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		delete source;
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error moving file/folder");
		return -ERROR_IO_DEVICE;
	}
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "File/folder moved OK");

	if (strcmp(source->getName(), destname.c_str()))
	{
		megaApi->renameNode(source, destname.c_str(), &listener);
		listener.wait();

		if (listener.getError()->getErrorCode() != MegaError::API_OK)
		{
			delete source;
			MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error renaming file/folder");
			return -ERROR_IO_DEVICE;
		}

		MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "File/folder renamed OK");
	}

	delete source;
	return 0;
}


static int __stdcall MEGALockFile(
	LPCWSTR				FileName,
	LONGLONG			ByteOffset,
	LONGLONG			Length,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGALockFile");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	return 0;
}


static int __stdcall MEGASetEndOfFile(
	LPCWSTR				FileName,
	LONGLONG			ByteOffset,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGASetEndOfFile");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	return -ERROR_ACCESS_DENIED;
}


static int __stdcall MEGASetAllocationSize(
	LPCWSTR				FileName,
	LONGLONG			AllocSize,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGASetAllocationSize");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	return -ERROR_ACCESS_DENIED;
}


static int __stdcall MEGASetFileAttributes(
	LPCWSTR				FileName,
	DWORD				FileAttributes,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGASetFileAttributes");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	return 0;
}


static int __stdcall MEGASetFileTime(
	LPCWSTR				FileName,
	CONST FILETIME*		CreationTime,
	CONST FILETIME*		LastAccessTime,
	CONST FILETIME*		LastWriteTime,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGASetFileTime");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	return 0;
}


static int __stdcall MEGAUnlockFile(
LPCWSTR				FileName,
LONGLONG			ByteOffset,
LONGLONG			Length,
PDOKAN_FILE_INFO	DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAUnlockFile");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	return 0;
}


static int __stdcall MEGAGetFileSecurity(
	LPCWSTR					FileName,
	PSECURITY_INFORMATION	SecurityInformation,
	PSECURITY_DESCRIPTOR	SecurityDescriptor,
	ULONG				BufferLength,
	PULONG				LengthNeeded,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAGetFileSecurity");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());

	MegaNode *n = megaApi->getNodeByPath(path.c_str());
	if (!n)
	{
		return -ERROR_PATH_NOT_FOUND;
	}

	PSECURITY_INFORMATION absDesc = (PSECURITY_INFORMATION)malloc(SECURITY_DESCRIPTOR_MIN_LENGTH);
	InitializeSecurityDescriptor(absDesc, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(absDesc, TRUE, NULL, FALSE);
	if (!MakeSelfRelativeSD(absDesc, SecurityDescriptor, &BufferLength))
	{
		free(absDesc);
		MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAGetFileSecurity ERROR");
		DWORD e = GetLastError();
		*LengthNeeded = BufferLength;
		return -e;
	}

	if (!IsValidSecurityDescriptor(SecurityDescriptor))
	{
		free(absDesc);
		MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAGetFileSecurity INVALID");
		return -GetLastError();
	}

	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAGetFileSecurity OK");
	return 0;
}


static int __stdcall MEGASetFileSecurity(
	LPCWSTR					FileName,
	PSECURITY_INFORMATION	SecurityInformation,
	PSECURITY_DESCRIPTOR	SecurityDescriptor,
	ULONG				SecurityDescriptorLength,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	string path;
	MEGAGetFilePath(&path, FileName);
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGASetFileSecurity");
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, path.c_str());
	return 0;
}

static int __stdcall MEGAGetVolumeInformation(
	LPWSTR		VolumeNameBuffer,
	DWORD		VolumeNameSize,
	LPDWORD		VolumeSerialNumber,
	LPDWORD		MaximumComponentLength,
	LPDWORD		FileSystemFlags,
	LPWSTR		FileSystemNameBuffer,
	DWORD		FileSystemNameSize,
	PDOKAN_FILE_INFO	DokanFileInfo)
{
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAGetVolumeInformation");
	wcscpy_s(VolumeNameBuffer, VolumeNameSize / sizeof(WCHAR), DRIVE_LABEL);
	*VolumeSerialNumber = 0x19831116;
	*MaximumComponentLength = 256;
	*FileSystemFlags = FILE_CASE_SENSITIVE_SEARCH |
		FILE_CASE_PRESERVED_NAMES |
		FILE_SUPPORTS_REMOTE_STORAGE |
		FILE_UNICODE_ON_DISK;

	wcscpy_s(FileSystemNameBuffer, FileSystemNameSize / sizeof(WCHAR), DRIVE_LABEL);
	return 0;
}


static int __stdcall MEGAUnmount(PDOKAN_FILE_INFO	DokanFileInfo)
{
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAUnmount");
	return 0;
}

static int __stdcall MEGAGetDiskFreeSpace(
	PULONGLONG freeBytesAvailable, 
	PULONGLONG totalNumberOfBytes,
	PULONGLONG totalNumberOfFreeBytes, 
	PDOKAN_FILE_INFO dokanInfo)
{
	MegaApi::log(MegaApi::LOG_LEVEL_DEBUG, "MEGAGetDiskFreeSpace");
	SynchronousRequestListener listener;
	megaApi->getAccountDetails(&listener);
	listener.wait();
	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "getAccountDetails error");
		return 0;
	}

	MegaAccountDetails *details = listener.getRequest()->getMegaAccountDetails();
	if (freeBytesAvailable)
	{
		*freeBytesAvailable = details->getStorageMax() > details->getStorageUsed() ? details->getStorageMax() - details->getStorageUsed() : 0;
	}

	if (totalNumberOfBytes)
	{
		*totalNumberOfBytes = details->getStorageMax();
	}

	if (totalNumberOfFreeBytes)
	{
		*totalNumberOfFreeBytes = details->getStorageMax() > details->getStorageUsed() ? details->getStorageMax() - details->getStorageUsed() : 0;
	}

	MegaApi::log(MegaApi::LOG_LEVEL_INFO, "MEGAGetDiskFreeSpace OK");
	return 0;
}

int main(int argc, char *argv[])
{
	//Initialization
	megaBasePath = MEGA_MOUNTPOINT;
	if(megaBasePath[megaBasePath.size() -1] == '/')
	{
		megaBasePath.resize(megaBasePath.size() - 1);
	}

	megaApi = new MegaApi(APP_KEY, (const char*)NULL, "MEGA/SDK Dokan filesystem");
	if(ENABLE_DEBUG)
	{
		megaApi->setLogLevel(MegaApi::LOG_LEVEL_DEBUG);
	}
	else
	{
		megaApi->setLogLevel(MegaApi::LOG_LEVEL_INFO);
	}

	//Login
	SynchronousRequestListener listener;
	megaApi->login(MEGA_USER_EMAIL, MEGA_USER_PASSWORD, &listener);
	listener.wait();
	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Login error");
		delete megaApi;
		return 0;
	}
	MegaApi::log(MegaApi::LOG_LEVEL_INFO, "Login OK. Fetching nodes");

	//Fetch nodes
	megaApi->fetchNodes(&listener);
	listener.wait();
	if (listener.getError()->getErrorCode() != MegaError::API_OK)
	{
		MegaApi::log(MegaApi::LOG_LEVEL_ERROR, "Error fetchning nodes");
		delete megaApi;
		return 0;
	}
	MegaApi::log(MegaApi::LOG_LEVEL_INFO, "Fetchnodes OK");

	//Start Dokan
	MegaApi::log(MegaApi::LOG_LEVEL_INFO, "Starting Dokan!");
	if(!ENABLE_DEBUG)
	{
		megaApi->setLogLevel(MegaApi::LOG_LEVEL_WARNING);
	}

	int status;
	PDOKAN_OPERATIONS dokanOperations =
		(PDOKAN_OPERATIONS)malloc(sizeof(DOKAN_OPERATIONS));
	PDOKAN_OPTIONS dokanOptions =
		(PDOKAN_OPTIONS)malloc(sizeof(DOKAN_OPTIONS));

	ZeroMemory(dokanOptions, sizeof(DOKAN_OPTIONS));
	dokanOptions->Version = DOKAN_VERSION;
	dokanOptions->ThreadCount = 0; // use default
	dokanOptions->MountPoint = LOCAL_MOUNTPOINT;
	dokanOptions->Options |= DOKAN_OPTION_KEEP_ALIVE /*| DOKAN_OPTION_REMOVABLE*/;

	ZeroMemory(dokanOperations, sizeof(DOKAN_OPERATIONS));
	dokanOperations->CreateFile = MEGACreateFile;
	dokanOperations->OpenDirectory = MEGAOpenDirectory;
	dokanOperations->CreateDirectory = MEGACreateDirectory;
	dokanOperations->Cleanup = MEGACleanup;
	dokanOperations->CloseFile = MEGACloseFile;
	dokanOperations->ReadFile = MEGAReadFile;
	dokanOperations->WriteFile = MEGAWriteFile;
	dokanOperations->FlushFileBuffers = MEGAFlushFileBuffers;
	dokanOperations->GetFileInformation = MEGAGetFileInformation;
	dokanOperations->FindFiles = MEGAFindFiles;
	dokanOperations->FindFilesWithPattern = NULL;
	dokanOperations->SetFileAttributes = MEGASetFileAttributes;
	dokanOperations->SetFileTime = MEGASetFileTime;
	dokanOperations->DeleteFile = MEGADeleteFile;
	dokanOperations->DeleteDirectory = MEGADeleteDirectory;
	dokanOperations->MoveFile = MEGAMoveFile;
	dokanOperations->SetEndOfFile = MEGASetEndOfFile;
	dokanOperations->SetAllocationSize = MEGASetAllocationSize;
	dokanOperations->LockFile = MEGALockFile;
	dokanOperations->UnlockFile = MEGAUnlockFile;
	dokanOperations->GetFileSecurity = MEGAGetFileSecurity;
	dokanOperations->SetFileSecurity = MEGASetFileSecurity;
	dokanOperations->GetDiskFreeSpace = NULL;
	dokanOperations->GetVolumeInformation = MEGAGetVolumeInformation;
	dokanOperations->Unmount = MEGAUnmount;
	dokanOperations->GetDiskFreeSpaceW = MEGAGetDiskFreeSpace;

	status = DokanMain(dokanOptions, dokanOperations);
	switch (status) {
	case DOKAN_SUCCESS:
		fprintf(stderr, "Success\n");
		break;
	case DOKAN_ERROR:
		fprintf(stderr, "Error\n");
		break;
	case DOKAN_DRIVE_LETTER_ERROR:
		fprintf(stderr, "Bad Drive letter\n");
		break;
	case DOKAN_DRIVER_INSTALL_ERROR:
		fprintf(stderr, "Can't install driver\n");
		break;
	case DOKAN_START_ERROR:
		fprintf(stderr, "Driver something wrong\n");
		break;
	case DOKAN_MOUNT_ERROR:
		fprintf(stderr, "Can't assign a drive letter\n");
		break;
	case DOKAN_MOUNT_POINT_ERROR:
		fprintf(stderr, "Mount point error\n");
		break;
	default:
		fprintf(stderr, "Unknown error: %d\n", status);
		break;
	}

	free(dokanOptions);
	free(dokanOperations);
	delete megaApi;
	return 0;
}
