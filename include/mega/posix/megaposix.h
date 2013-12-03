/**
 * @file mega/posix/megaposix.h
 * @brief Sample application for the gcc/POSIX environment
 *
 * (c) 2013 by Mega Limited, Wellsford, New Zealand
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

#ifndef _WIN32
#ifndef MEGAPOSIX_H
#define MEGAPOSIX_H

namespace mega {

struct PosixWaiter : public Waiter
{
	int maxfd;
	fd_set rfds, wfds, efds;

	dstime getdstime();

	void init(dstime);
	void waitfor(EventTrigger*);
	int wait();

	void bumpmaxfd(int);
};

class CurlHttpIO : public HttpIO
{
protected:
	CURLM* curlm;
	CURLSH* curlsh;

	static size_t write_data(void *, size_t, size_t, void *);

	curl_slist* contenttypejson;
	curl_slist* contenttypebinary;

public:
	void post(HttpReq*, const char* = 0, unsigned = 0);
	void cancel(HttpReq*);

	m_off_t postpos(void*);

	bool doio(void);

	void addevents(Waiter*);

	CurlHttpIO();
	~CurlHttpIO();
};

struct PosixDirAccess : public DirAccess
{
	DIR* dp;
	bool globbing;
	glob_t globbuf;
	unsigned globindex;

	bool dopen(string*, FileAccess*, bool);
	bool dnext(string*, nodetype* = NULL);

	PosixDirAccess();
	virtual ~PosixDirAccess();
};

class PosixFileSystemAccess : public FileSystemAccess
{
	int notifyfd;
	bool notifyerr;
	char notifybuf[sizeof(struct inotify_event)+NAME_MAX+1];
	int notifypos, notifyleft;

	typedef map<int,LocalNode*> wdlocalnode_map;
	wdlocalnode_map wdnodes;

public:
	FileAccess* newfileaccess();
	DirAccess* newdiraccess();

	void tmpnamelocal(string*, string* = NULL);

	void local2path(string*, string*);
	void path2local(string*, string*);

	void name2local(string*, const char* = NULL);
	void local2name(string*);

	bool localhidden(string*, string*);

	bool renamelocal(string*, string*);
	bool copylocal(string*, string*);
	bool rubbishlocal(string*);
	bool unlinklocal(string*);
	bool rmdirlocal(string*);
	bool mkdirlocal(string*);
	bool setmtimelocal(string*, time_t);
	bool chdirlocal(string*);

	void addnotify(LocalNode*, string*);
	void delnotify(LocalNode*);
	bool notifynext(sync_list*, string*, LocalNode**);
	bool notifyfailed();

	void addevents(Waiter*);

	PosixFileSystemAccess();
	~PosixFileSystemAccess();
};

class PosixFileAccess : public FileAccess
{
public:
	int fd;

	bool fopen(string*, bool, bool);
	bool fread(string*, unsigned, unsigned, m_off_t);
	bool frawread(byte*, unsigned, m_off_t);
	bool fwrite(const byte*, unsigned, m_off_t);

	PosixFileAccess();
	~PosixFileAccess();
};

} // namespace

#endif
#endif
