/**
 * @file examples/megasimplesync.cpp
 * @brief sample daemon, which synchronizes local and remote folders
 *
 * (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
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

class SyncApp : public MegaApp
{
    string local_folder;
    string remote_folder;
    handle cwd;
    bool initial_fetch;

    void login_result(error e);

    void fetchnodes_result(error e);

    void request_error(error e);

#ifdef ENABLE_SYNC
    void syncupdate_state(Sync *, syncstate_t);
    void syncupdate_local_folder_addition(Sync*, LocalNode*, const char *);
    void syncupdate_local_folder_deletion(Sync*, LocalNode*);
    void syncupdate_local_file_addition(Sync*, LocalNode*, const char *);
    void syncupdate_local_file_deletion(Sync*, LocalNode*);
    void syncupdate_local_file_change(Sync*, LocalNode*, const char *);
    void syncupdate_local_move(Sync*, LocalNode*, const char*);
    void syncupdate_get(Sync*, Node*, const char*);
    void syncupdate_put(Sync*, LocalNode*, const char*);
    void syncupdate_remote_file_addition(Sync*, Node*);
    void syncupdate_remote_file_deletion(Sync*, Node*);
    void syncupdate_remote_folder_addition(Sync*, Node*);
    void syncupdate_remote_folder_deletion(Sync*, Node*);
    void syncupdate_remote_copy(Sync*, const char*);
    void syncupdate_remote_move(Sync*, Node*, Node*);
    void syncupdate_remote_rename(Sync*sync, Node* n, const char* prevname);
    void syncupdate_treestate(LocalNode*);
#endif

    Node* nodebypath(const char* ptr, string* user, string* namepart);
public:
    SyncApp(string local_folder_, string remote_folder_);
};

// globals
MegaClient* client;

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
Node* SyncApp::nodebypath(const char* ptr, string* user = NULL, string* namepart = NULL)
{
    vector<string> c;
    string s;
    int l = 0;
    const char* bptr = ptr;
    int remote = 0;
    Node* n;
    Node* nn;

    // split path by / or :
    do
    {
        if (!l)
        {
            if (*ptr >= 0)
            {
                if (*ptr == '\\')
                {
                    if (ptr > bptr)
                    {
                        s.append(bptr, ptr - bptr);
                    }
                    bptr = ++ptr;

                    if (*bptr == 0)
                    {
                        c.push_back(s);
                        break;
                    }

                    ptr++;
                    continue;
                }

                if (( *ptr == '/' ) || ( *ptr == ':' ) || !*ptr)
                {
                    if (*ptr == ':')
                    {
                        if (c.size())
                        {
                            return NULL;
                        }
                        remote = 1;
                    }

                    if (ptr > bptr)
                    {
                        s.append(bptr, ptr - bptr);
                    }

                    bptr = ptr + 1;

                    c.push_back(s);

                    s.erase();
                }
            }
            else if (( *ptr & 0xf0 ) == 0xe0)
            {
                l = 1;
            }
            else if (( *ptr & 0xf8 ) == 0xf0)
            {
                l = 2;
            }
            else if (( *ptr & 0xfc ) == 0xf8)
            {
                l = 3;
            }
            else if (( *ptr & 0xfe ) == 0xfc)
            {
                l = 4;
            }
        }
        else
        {
            l--;
        }
    }
    while (*ptr++);

    if (l)
    {
        return NULL;
    }

    if (remote)
    {
        // target: user inbox - record username/email and return NULL
        if (( c.size() == 2 ) && !c[1].size())
        {
            if (user)
            {
                *user = c[0];
            }
            return NULL;
        }

        User* u;

        if (( u = client->finduser(c[0].c_str())))
        {
            // locate matching share from this user
            handle_set::iterator sit;
            string name;
            for (sit = u->sharing.begin(); sit != u->sharing.end(); sit++)
            {
                if (( n = client->nodebyhandle(*sit)))
                {
                    if(!name.size())
                    {
                        name =  c[1];
                        n->client->fsaccess->normalize(&name);
                    }

                    if (!strcmp(name.c_str(), n->displayname()))
                    {
                        l = 2;
                        break;
                    }
                }
            }
        }

        if (!l)
        {
            return NULL;
        }
    }
    else
    {
        // path starting with /
        if (( c.size() > 1 ) && !c[0].size())
        {
            // path starting with //
            if (( c.size() > 2 ) && !c[1].size())
            {
                if (c[2] == "in")
                {
                    n = client->nodebyhandle(client->rootnodes[1]);
                }
                else if (c[2] == "bin")
                {
                    n = client->nodebyhandle(client->rootnodes[2]);
                }
                else
                {
                    return NULL;
                }

                l = 3;
            }
            else
            {
                n = client->nodebyhandle(client->rootnodes[0]);

                l = 1;
            }
        }
        else
        {
            n = client->nodebyhandle(cwd);
        }
    }

    // parse relative path
    while (n && l < (int)c.size())
    {
        if (c[l] != ".")
        {
            if (c[l] == "..")
            {
                if (n->parent)
                {
                    n = n->parent;
                }
            }
            else
            {
                // locate child node (explicit ambiguity resolution: not
                // implemented)
                if (c[l].size())
                {
                    nn = client->childnodebyname(n, c[l].c_str());

                    if (!nn)
                    {
                        // mv command target? return name part of not found
                        if (namepart && ( l == (int)c.size() - 1 ))
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

SyncApp:: SyncApp(string local_folder_, string remote_folder_) :
    local_folder(local_folder_), remote_folder(remote_folder_), cwd(UNDEF), initial_fetch(true)
{}

// this callback function is called when we have login result (success or
// error)
// TODO: check for errors
void SyncApp::login_result(error e)
{
    if (e != API_OK)
    {
        LOG_err << "FATAL: Failed to get login result, exiting";
        exit(1);
    }
    // get the list of nodes
    client->fetchnodes();
}

void SyncApp::fetchnodes_result(error e)
{
    if (e != API_OK)
    {
        LOG_err << "FATAL: Failed to fetch remote nodes, exiting";
                    exit(1);
    }

    if (initial_fetch)
    {
        initial_fetch = false;
        if (ISUNDEF(cwd))
        {
            cwd = client->rootnodes[0];
        }

        Node* n = nodebypath(remote_folder.c_str());
        if (client->checkaccess(n, FULL))
        {
            string localname;

            client->fsaccess->path2local(&local_folder, &localname);

            if (!n)
            {
                LOG_err << remote_folder << ": Not found.";
                exit(1);
            }
            else if (n->type == FILENODE)
            {
                LOG_err << remote_folder << ": Remote sync root must be folder.";
                exit(1);
            }
            else
            {
                error e = client->addsync(&localname, DEBRISFOLDER, NULL, n, 0);
                if (e)
                {
                    LOG_err << "Sync could not be added! ";
                    exit(1);
                }

                LOG_info << "Sync started !";
            }
        }
        else
        {
            LOG_err << remote_folder << ": Syncing requires full access to path.";
            exit(1);
        }
    }
}

// this callback function is called when request-level error occurred
void SyncApp::request_error(error e)
{
    LOG_err << "FATAL: Request failed, exiting";
    exit(1);
}

#ifdef ENABLE_SYNC
void SyncApp::syncupdate_state(Sync*, syncstate_t state)
{
    if (( state == SYNC_CANCELED ) || ( state == SYNC_FAILED ))
    {
        LOG_err << "FATAL: Sync failed !";
        exit(1);
    }
    else if (state == SYNC_ACTIVE)
    {
        LOG_info << "Sync is now active";
    }
}

// sync update callbacks are for informational purposes only and must not
// change or delete the sync itself
void SyncApp::syncupdate_local_folder_addition(Sync*, LocalNode *, const char* path)
{
    LOG_info << "Sync - local folder addition detected: " << path;
}

void SyncApp::syncupdate_local_folder_deletion(Sync*, LocalNode *localNode)
{
    LOG_info << "Sync - local folder deletion detected: " << localNode->name;
}

void SyncApp::syncupdate_local_file_addition(Sync*, LocalNode *, const char* path)
{
    LOG_info << "Sync - local file addition detected: " << path;
}

void SyncApp::syncupdate_local_file_deletion(Sync*, LocalNode *localNode)
{
    LOG_info << "Sync - local file deletion detected: " << localNode->name;
}

void SyncApp::syncupdate_local_file_change(Sync*, LocalNode *, const char* path)
{
    LOG_info << "Sync - local file change detected: " << path;
}

void SyncApp::syncupdate_local_move(Sync*, LocalNode *localNode, const char* path)
{
    LOG_info << "Sync - local rename/move " << localNode->name << " -> " << path;
}

void SyncApp::syncupdate_remote_move(Sync *, Node *n, Node *prevparent)
{
    LOG_info << "Sync - remote move " << n->displayname() << ": " << (prevparent ? prevparent->displayname() : "?") <<
                 " -> " << (n->parent ? n->parent->displayname() : "?");
}

void SyncApp::syncupdate_remote_rename(Sync *, Node *n, const char *prevname)
{
    LOG_info << "Sync - remote rename " << prevname << " -> " << n->displayname();
}

void SyncApp::syncupdate_remote_folder_addition(Sync *, Node* n)
{
    LOG_info << "Sync - remote folder addition detected " << n->displayname();
}

void SyncApp::syncupdate_remote_file_addition(Sync*, Node* n)
{
    LOG_info << "Sync - remote file addition detected " << n->displayname();
}

void SyncApp::syncupdate_remote_folder_deletion(Sync*, Node* n)
{
    LOG_info << "Sync - remote folder deletion detected " << n->displayname();
}

void SyncApp::syncupdate_remote_file_deletion(Sync*, Node* n)
{
    LOG_info << "Sync - remote file deletion detected " << n->displayname();
}

void SyncApp::syncupdate_get(Sync*, Node *, const char* path)
{
    LOG_info << "Sync - requesting file " << path;
}

void SyncApp::syncupdate_put(Sync*, LocalNode*, const char* path)
{
    LOG_info  << "Sync - sending file " << path;
}

void SyncApp::syncupdate_remote_copy(Sync*, const char* name)
{
    LOG_info << "Sync - creating remote file " << name << " by copying existing remote file";
}

static const char* treestatename(treestate_t ts)
{
    switch (ts)
    {
        case TREESTATE_NONE:
            return "None/Undefined";

        case TREESTATE_SYNCED:
            return "Synced";

        case TREESTATE_PENDING:
            return "Pending";

        case TREESTATE_SYNCING:
            return "Syncing";
    }

    return "UNKNOWN";
}

void SyncApp::syncupdate_treestate(LocalNode* l)
{
    LOG_info << "Sync - state change of node " << l->name << " to " << treestatename(l->ts);
}

#endif
int main(int argc, char *argv[])
{
#ifndef ENABLE_SYNC
    cerr << "Synchronization features are disabled" << endl;
    return 1;
#endif

    static byte pwkey[SymmCipher::KEYLENGTH];
    SyncApp *app;

    // use logInfo level
    SimpleLogger::setLogLevel(logInfo);
    // set output to stdout
    SimpleLogger::setAllOutputs(&std::cout);

    if (argc < 3)
    {
        LOG_info << "Usage: " << argv[0] << " [local folder] [remote folder]";
        LOG_info << "   (set MEGA_DEBUG to 1 or 2 to see debug output.";
        return 1;
    }

    app = new SyncApp(argv[1], argv[2]);

    if (!getenv("MEGA_EMAIL") || !getenv("MEGA_PWD"))
    {
        LOG_info << "Please set both MEGA_EMAIL and MEGA_PWD env variables!";
        return 1;
    }

    // create MegaClient, providing our custom MegaApp and Waiter classes
    client = new MegaClient(app, new WAIT_CLASS, new HTTPIO_CLASS, new FSACCESS_CLASS,
                        #ifdef DBACCESS_CLASS
                                                    new DBACCESS_CLASS,
                        #else
                                                    NULL,
                        #endif
                        #ifdef GFX_CLASS
                                                    new GFX_CLASS,
                        #else
                                                    NULL,
                        #endif
                            "N9tSBJDC", "megasimplesync");

    // if MEGA_DEBUG env variable is set
    if (getenv("MEGA_DEBUG"))
    {
        if (!strcmp(getenv("MEGA_DEBUG"), "1") || !strcmp(getenv("MEGA_DEBUG"), "2"))
        {
            SimpleLogger::setLogLevel(logDebug);
        }
    }

    // uncomment this line if you want to follow symbolic links
    //client->followsymlinks = true;

    // get values from env
    client->pw_key(getenv("MEGA_PWD"), pwkey);
    client->login(getenv("MEGA_EMAIL"), pwkey);

    while (true)
    {
        // pass the CPU to the engine (nonblocking)
        client->exec();
        client->wait();
    }

    return 0;
}
