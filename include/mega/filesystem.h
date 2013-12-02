/*

MEGA SDK - Client Access Engine Core Logic

(c) 2013 by Mega Limited, Wellsford, New Zealand

Applications using the MEGA API must present a valid application key
and comply with the the rules set forth in the Terms of Service.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#ifndef MEGA_FILESYSTEM_H
#define MEGA_FILESYSTEM_H 1

// FIXME: #define PRI*64 if missing
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
typedef int64_t m_off_t;

#include "types.h"


// generic host file access interface
struct FileAccess
{
	// file size
	m_off_t size;

	// mtime of a file opened for reading
	time_t mtime;

	// type of opened path
	nodetype type;

	// if the open failed, retry indicates a potentially transient reason
	bool retry;

	// open for reading, writing or reading and writing
	virtual bool fopen(string*, bool, bool) = 0;

	// absolute position read, with NUL padding
	virtual bool fread(string*, unsigned, unsigned, m_off_t) = 0;

	// absolute position read to byte buffer
	virtual bool frawread(byte*, unsigned, m_off_t) = 0;

	// absolute position write
	virtual bool fwrite(const byte*, unsigned, m_off_t) = 0;

	virtual ~FileAccess() { }
};

// generic host directory enumeration
struct DirAccess
{
	// open for scanning
	virtual bool dopen(string*, FileAccess*, bool) = 0;

	// get next record
	virtual bool dnext(string*, nodetype* = NULL) = 0;

	virtual ~DirAccess() { }
};

// generic host filesystem access interface
struct FileSystemAccess : public EventTrigger
{
	// local path separator, e.g. "/"
	string localseparator;

	// instantiate FileAccess object
	virtual FileAccess* newfileaccess() = 0;

	// return DirAccess object or NULL if unsuccessful
	virtual DirAccess* newdiraccess() = 0;

	// check if character is lowercase hex ASCII
	bool islchex(char);

	// convert MEGA path (UTF-8) to local format
	virtual void path2local(string*, string*) = 0;

	// convert local path to MEGA format (UTF-8)
	virtual void local2path(string*, string*) = 0;

	// convert MEGA-formatted filename (UTF-8) to local filesystem name; escape forbidden characters using urlencode
	virtual void name2local(string*, const char* = NULL) = 0;

	// reverse local2name()
	virtual void local2name(string*) = 0;

	// generate local temporary file name
	virtual void tmpnamelocal(string*, string* = NULL) = 0;

	// check if local file/name is to be treated as hidden
	virtual bool localhidden(string*, string*) = 0;

	// rename file, overwrite target
	virtual bool renamelocal(string*, string*) = 0;

	// copy file, overwrite target
	virtual bool copylocal(string*, string*) = 0;

	// move file or folder tree to OS-managed local rubbish bin
	virtual bool rubbishlocal(string*) = 0;

	// delete file
	virtual bool unlinklocal(string*) = 0;

	// delete empty directory
	virtual bool rmdirlocal(string*) = 0;

	// create directory
	virtual bool mkdirlocal(string*) = 0;

	// set mtime
	virtual bool setmtimelocal(string*, time_t) = 0;

	// change working directory
	virtual bool chdirlocal(string*) = 0;

	// add notification (has to be called for all directories in tree for full crossplatform support)
	virtual void addnotify(LocalNode*, string*) = 0;

	// delete notification
	virtual void delnotify(LocalNode*) = 0;

	// return next notified local name and corresponding parent node
	virtual bool notifynext(sync_list*, string*, LocalNode**) = 0;

	// true if notifications were unreliable and/or a full rescan is required
	virtual bool notifyfailed() = 0;

	virtual ~FileSystemAccess() { }
};

#endif
