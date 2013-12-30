/**
 * @file examples/megasync.cpp
 * @brief sample daemon, which synchronizes local and remote folders
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

#include "mega.h"

#ifdef _WIN32
#include <conio.h>
#endif

using namespace mega;

struct SyncApp : public MegaApp
{
    void nodes_updated(Node**, int);
    void debug_log(const char*);
    void login_result(error e);

    void request_error(error e);
};

// globals
MegaClient* client;
static handle cwd = UNDEF;
bool mega::debug;

// this callback function is called when nodes have been updated
// save root node handle
void SyncApp::nodes_updated(Node** n, int count)
{
    if (ISUNDEF(cwd)) cwd = client->rootnodes[0];
}

// callback for displaying debug logs
void SyncApp::debug_log(const char* message)
{
    cout << "DEBUG: " << message << endl;
}

// this callback function is called when we have login result (success or error)
// TODO: check for errors
void SyncApp::login_result(error e)
{
    // get the list of nodes
    client->fetchnodes();
}

// this callback function is called when request-level error occurred
void SyncApp::request_error(error e)
{
    cout << "FATAL: Request failed  exiting" << endl;
    exit(0);
}

// returns node pointer determined by path relative to cwd
// Path naming conventions:
// path is relative to cwd
// /path is relative to ROOT
// //in is in INBOX
// //bin is in RUBBISH
// X: is user X's INBOX
// X:SHARE is share SHARE from user X
// : and / filename components, as well as the \, must be escaped by \.
// (correct UTF-8 encoding is assumed)
// returns NULL if path malformed or not found
static Node* nodebypath(const char* ptr, string* user = NULL, string* namepart = NULL)
{
	vector<string> c;
	string s;
	int l = 0;
	const char* bptr = ptr;
	int remote = 0;
	Node* n;
	Node* nn;

	// split path by / or :
	do {
		if (!l)
		{
			if (*ptr >= 0)
			{
				if (*ptr == '\\')
				{
					if (ptr > bptr) s.append(bptr,ptr-bptr);
					bptr = ++ptr;

					if (*bptr == 0)
					{
						c.push_back(s);
						break;
					}

					ptr++;
					continue;
				}

				if (*ptr == '/' || *ptr == ':' || !*ptr)
				{
					if (*ptr == ':')
					{
						if (c.size()) return NULL;
						remote = 1;
					}

					if (ptr > bptr) s.append(bptr,ptr-bptr);

					bptr = ptr+1;

					c.push_back(s);

					s.erase();
				}
			}
			else if ((*ptr & 0xf0) == 0xe0) l = 1;
			else if ((*ptr & 0xf8) == 0xf0) l = 2;
			else if ((*ptr & 0xfc) == 0xf8) l = 3;
			else if ((*ptr & 0xfe) == 0xfc) l = 4;
		}
		else l--;
	} while (*ptr++);

	if (l) return NULL;

	if (remote)
	{
		// target: user inbox - record username/email and return NULL
		if (c.size() == 2 && !c[1].size())
		{
			if (user) *user = c[0];
			return NULL;
		}

		User* u;

		if ((u = client->finduser(c[0].c_str())))
		{
			// locate matching share from this user
			handle_set::iterator sit;

			for (sit = u->sharing.begin(); sit != u->sharing.end(); sit++)
			{
				if ((n = client->nodebyhandle(*sit)))
				{
					if (!strcmp(c[1].c_str(),n->displayname()))
					{
						l = 2;
						break;
					}
				}

				if (l) break;
			}
		}

		if (!l) return NULL;
	}
	else
	{
		// path starting with /
		if (c.size() > 1 && !c[0].size())
		{
			// path starting with //
			if (c.size() > 2 && !c[1].size())
			{
				if (c[2] == "in") n = client->nodebyhandle(client->rootnodes[1]);
				else if (c[2] == "bin") n = client->nodebyhandle(client->rootnodes[2]);
				else if (c[2] == "mail") n = client->nodebyhandle(client->rootnodes[3]);
				else return NULL;

				l = 3;
			}
			else
			{
				n = client->nodebyhandle(client->rootnodes[0]);

				l = 1;
			}
		}
		else n = client->nodebyhandle(cwd);
	}

	// parse relative path
	while (n && l < (int)c.size())
	{
		if (c[l] != ".")
		{
			if (c[l] == "..")
			{
				if (n->parent) n = n->parent;
			}
			else
			{
				// locate child node (explicit ambiguity resolution: not implemented)
				if (c[l].size())
				{
					nn = client->childnodebyname(n,c[l].c_str());

					if (!nn)
					{
						// mv command target? return name part of not found
						if (namepart && l == (int)c.size()-1)
						{
							*namepart = c[l];
							return n;
						}

						return NULL;
					}

					n = nn;
				}
			}
		}

		l++;
	}

	return n;
}

//
int main (int argc, char *argv[])
{
    static byte pwkey[SymmCipher::KEYLENGTH];
    bool is_active = true;
    string folder_local;


    if (argc < 3) {
        cout << "Usage: " << argv[0] << " [local folder] [remote folder]" << endl;
        return 1;
    }
    folder_local = argv[1];

    if (!getenv ("MEGA_EMAIL") || !getenv ("MEGA_PWD")) {
        cout << "Please set both MEGA_EMAIL and MEGA_PWD env variables!" << endl;
        return 1;
    }

    // if MEGA_DEBUG env variable is set
    if (getenv ("MEGA_DEBUG"))
        mega::debug = true;

    // create MegaClient, providing our custom MegaApp and Waiter classes
    client = new MegaClient(new SyncApp, new WAIT_CLASS, new HTTPIO_CLASS, new FSACCESS_CLASS,
#ifdef DBACCESS_CLASS
	new DBACCESS_CLASS,
#else
	NULL,
#endif
    "megasync");

    // get values from env
    client->pw_key (getenv ("MEGA_PWD"), pwkey);
    client->login (getenv ("MEGA_EMAIL"), pwkey);

    // loop while we are not logged in
    while (! client->loggedin ()) {
        client->wait();
        client->exec();
    }

    Node* n = nodebypath(argv[2]);
    if (client->checkaccess(n, FULL))
    {
        string localname;

        client->fsaccess->path2local(&folder_local, &localname);

        if (!n) cout << argv[2] << ": Not found." << endl;
        else if (n->type == FILENODE) cout << argv[2] << ": Remote sync root must be folder." << endl;
        else new Sync(client,&localname,n);
    }
    else cout << argv[2] << ": Syncing requires full access to path." << endl;

    while (is_active) {
		client->wait();
	    // pass the CPU to the engine (nonblocking)
		client->exec();
    }

    return 0;
}
