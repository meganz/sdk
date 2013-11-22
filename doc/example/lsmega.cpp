
#define __STDC_FORMAT_MACROS


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <memory.h>
#include <time.h>
#include <inttypes.h>
#include <endian.h>

typedef int64_t m_off_t;

#include <iostream>
#include <algorithm>
#include <string>
#include <sstream>
#include <map>
#include <set>
#include <iterator>
#include <queue>
#include <list>
using namespace std;


#include <crypto/cryptopp.h>
#include <megaclient.h>

#include <db/bdb.h>
#include <db/sqlite.h>

#include <posix/fs.h>
#include <posix/wait.h>
#include <posix/net.h>
#include <posix/console.h>


struct LsApp : public MegaApp
{
	void nodes_updated(Node**, int);
	void debug_log(const char*);
    void login_result(error e);

    void request_error(error e);
};

MegaClient* client;
static handle cwd = UNDEF;

bool debug;

static const char* accesslevels[] = { "read-only", "read/write", "full access" };

/*{{{*/
struct TestWaiter : public Waiter
{

	dstime getdstime();

	void init(dstime);
	void waitfor(EventTrigger*);
	int wait();

	void wakeupby(struct EventTrigger*);

    static const int XXX = 3;

};

void TestWaiter ::init(dstime ds)
{
	maxds = ds;

}

void TestWaiter:: wakeupby(struct EventTrigger*)
{
    cout << "EVENT ADDEDD !!! " << endl;
}
// update monotonously increasing timestamp in deciseconds
dstime TestWaiter::getdstime()
{
	timespec ts;

	clock_gettime(CLOCK_MONOTONIC,&ts);

	return ds = ts.tv_sec*10+ts.tv_nsec/100000000;
}

int TestWaiter ::wait()
{
	return NEEDEXEC;

}
/*}}}*/

/*{{{*/
void LsApp::nodes_updated(Node** n, int count)
{
    cout << "NODES updated !" << endl;
	if (ISUNDEF(cwd)) cwd = client->rootnodes[0];
}

void LsApp::debug_log(const char* message)
{
	cout << "DEBUG: " << message << endl;
}

void LsApp::login_result(error e)
{

	cout << "LOGIN: " << endl;

		client->fetchnodes();
}

void LsApp::request_error(error e)
{
	cout << "FATAL: Request failed  exiting" << endl;

	exit(0);
}
/*}}}*/

/*{{{*/
static void dumptree(Node* n, int recurse, int depth = 0, const char* title = NULL)
{
	if (depth)
	{
		if (!title && !(title = n->displayname())) title = "CRYPTO_ERROR";

		for (int i = depth; i--; ) cout << "\t";

		cout << title << " (";

		switch (n->type)
		{
			case FILENODE:
				cout << n->size;

				const char* p;
				if ((p = strchr(n->fileattrstring.c_str(),':'))) cout << ", has attributes " << p+1;
				break;

			case FOLDERNODE:
				cout << "folder";

				for (share_map::iterator it = n->outshares.begin(); it != n->outshares.end(); it++)
				{
					if (it->first) cout << ", shared with " << it->second->user->email << ", access " << accesslevels[it->second->access];
					else cout << ", shared as exported folder link";
				}

				if (n->inshare) cout << ", inbound " << accesslevels[n->inshare->access] << " share";
				break;

			default:
				cout << "unsupported type, please upgrade";
		}

		cout << ")" << (n->removed ? " (DELETED)" : "") << endl;

		if (!recurse) return;
	}

	if (n->type != FILENODE) for (node_list::iterator it = n->children.begin(); it != n->children.end(); it++) dumptree(*it,recurse,depth+1);
}
/*}}}*/

int main (int argc, char *argv[])
{
    static byte pwkey[SymmCipher::KEYLENGTH];
    Node* n;

    if (!getenv ("MEGA_EMAIL") && !getenv ("MEGA_PWD")) {
        cout << "Please set both MEGA_EMAIL and MEGA_PWD env variables!" << endl;
        return 1;
    }

	client = new MegaClient(new LsApp, new TestWaiter, new HTTPIO_CLASS, new FSACCESS_CLASS, new DBACCESS_CLASS, "lsmega");

    client->pw_key (getenv ("MEGA_PWD"), pwkey);
    client->login (getenv ("MEGA_EMAIL"), pwkey);
	cout << "Initiated login attempt..." << endl;

    //client->exec();
    while (! client->loggedin ()) {
		client->wait();
        client->exec();
        usleep (100);
    }
    client->exec();
    cout << "logged: " << client->loggedin () << endl;

    while (! (n = client->nodebyhandle(cwd))) {
		client->wait();
        client->exec();
        usleep (100);
    }

	dumptree(n, 1);

    return 0;
}
