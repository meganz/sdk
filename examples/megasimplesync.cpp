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
using std::cout;
using std::cerr;
using std::endl;



struct Login
{
    string email, password, salt, pin;
    int version;

    Login() : version(0)
    {
    }

    void reset()
    {
        *this = Login();
    }

    void login(MegaClient* client)
    {
        byte pwkey[SymmCipher::KEYLENGTH];

        if (version == 1)
        {
            if (error e = client->pw_key(password.c_str(), pwkey))
            {
                cout << "Login error: " << e << endl;
            }
            else
            {
                client->login(email.c_str(), pwkey, pin.c_str());
            }
        }
        else if (version == 2 && !salt.empty())
        {
            client->login2(email.c_str(), password.c_str(), &salt, pin.c_str());
        }
        else
        {
            cout << "Login unexpected error" << endl;
        }
    }
};
static Login login;

class SyncApp : public MegaApp, public Logger
{
    string local_folder;
    string remote_folder;
    handle cwd;
    bool initial_fetch;

    void prelogin_result(int version, string* email, string *salt, error e);

    void login_result(error e);

    void fetchnodes_result(const Error& e);

    void request_error(error e);

#ifdef ENABLE_SYNC
    void syncupdate_stateconfig(const SyncConfig& config) override;
    void syncupdate_treestate(const SyncConfig& config, const LocalPath& lp, treestate_t ts, nodetype_t) override;
#endif

    Node* nodebypath(const char* ptr, string* user, string* namepart);
public:
    SyncApp(string local_folder_, string remote_folder_);

    // Logger interface
public:
    void log(const char *time, int loglevel, const char *source, const char *message
        #ifdef ENABLE_LOG_PERFORMANCE
            , const char **directMessages = nullptr, size_t *directMessagesSizes = nullptr, unsigned numberMessages = 0
        #endif
    );
};

// globals
MegaClient* client;

// returns node pointer determined by path relative to cwd
// Path naming conventions:
// path is relative to cwd
// /path is relative to ROOT
// //in is in VAULT (formerly INBOX)
// //bin is in RUBBISH
// X: is user X's VAULT (formerly INBOX)
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
    Node* n = nullptr;
    Node* nn;

    // split path by / or :
    do
    {
        if (!l)
        {
            if (*(const signed char*)ptr >= 0)
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
                        LocalPath::utf8_normalize(&name);
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
                    n = client->nodeByHandle(client->rootnodes.vault);
                }
                else if (c[2] == "bin")
                {
                    n = client->nodeByHandle(client->rootnodes.rubbish);
                }
                else
                {
                    return NULL;
                }

                l = 3;
            }
            else
            {
                n = client->nodeByHandle(client->rootnodes.files);

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

void SyncApp::log(const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
                 , const char **directMessages, size_t *directMessagesSizes, unsigned numberMessages
#endif
                 )
{
    if (!time)
    {
        time = "";
    }

    if (!source)
    {
        source = "";
    }

    if (!message)
    {
        message = "";
    }

    cout << "[" << time << "][" << SimpleLogger::toStr((LogLevel)loglevel) << "] ";
    if (message) cout << message;
#ifdef ENABLE_LOG_PERFORMANCE
    for (unsigned i = 0; i < numberMessages; ++i) cout.write(directMessages[i], directMessagesSizes[i]);
#endif
    cout << endl;
}

void SyncApp::prelogin_result(int version, std::string* email, std::string *salt, error e)
{
    if (e)
    {
        cout << "Login error: " << e << endl;
        return;
    }

    login.version = version;
    login.salt = (version == 2 && salt ? *salt : string());

    if (login.password.empty())
    {
        cerr << "invalid empty password" << endl;
    }
    else
    {
        login.login(client);
    }
}


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

void SyncApp::fetchnodes_result(const Error &e)
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
            cwd = client->rootnodes.files.as8byte();
        }

        Node* n = nodebypath(remote_folder.c_str());
        if (client->checkaccess(n, FULL))
        {
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
#ifdef ENABLE_SYNC
                SyncConfig syncConfig(LocalPath::fromAbsolutePath(local_folder), local_folder, NodeHandle().set6byte(n->nodehandle), remote_folder, 0, LocalPath());
                client->addsync(syncConfig, false,
                                [](error err, const SyncError& serr, handle backupId) {
                    if (err)
                    {
                        LOG_err << "Sync could not be added! " << err << " syncError = " << serr;
                        exit(1);
                    }
                    else
                    {
                        LOG_info << "Sync started !";
                    }
                }, "");
#endif
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
void SyncApp::syncupdate_stateconfig(const SyncConfig& config)
{
    LOG_info << "Sync config updated: " << config.mBackupId;
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

void SyncApp::syncupdate_treestate(const SyncConfig &config, const LocalPath& lp, treestate_t ts, nodetype_t)
{
    LOG_info << "Sync - state change of node " << lp.toPath() << " to " << treestatename(ts);
}

#endif
int main(int argc, char *argv[])
{
#ifndef ENABLE_SYNC
    cerr << "Synchronization features are disabled" << endl;
    return 1;
#else

    SyncApp *app;

    // use logInfo level
    SimpleLogger::setLogLevel(logInfo);
    // set output to stdout
//    SimpleLogger::setAllOutputs(&std::cout);


    if (argc < 3)
    {
        cerr << "Usage: " << argv[0] << " [local folder] [remote folder]" << endl;
        cerr << "   (set MEGA_DEBUG to 1 or 2 to see debug output." << endl;
        return 1;
    }

    app = new SyncApp(argv[1], argv[2]);
    SimpleLogger::setOutputClass(app);

    if (!getenv("MEGA_EMAIL") || !getenv("MEGA_PWD"))
    {
        LOG_info << "Please set both MEGA_EMAIL and MEGA_PWD env variables!";
        return 1;
    }

    // Needed so we can get our hands on the cwd.
    auto fsAccess = ::mega::make_unique<FSACCESS_CLASS>();

    // Where are we?
    LocalPath currentDir;
    bool result = fsAccess->cwd(currentDir);

    if (!result)
    {
        cerr << "Unable to determine current working directory." << endl;
        return EXIT_FAILURE;
    }

    // create MegaClient, providing our custom MegaApp and Waiter classes
    client = new MegaClient(app,
                            new WAIT_CLASS,
                            new HTTPIO_CLASS,
                            move(fsAccess),
                        #ifdef DBACCESS_CLASS
                            new DBACCESS_CLASS(currentDir),
                        #else
                            nullptr,
                        #endif
                        #ifdef GFX_CLASS
                            new GfxProc(::mega::make_unique<GFX_CLASS>()),
                        #else
                            nullptr,
                        #endif
                            "N9tSBJDC",
                            "megasimplesync",
                            2);

    // if MEGA_DEBUG env variable is set
    if (getenv("MEGA_DEBUG"))
    {
        if (!strcmp(getenv("MEGA_DEBUG"), "1") || !strcmp(getenv("MEGA_DEBUG"), "2"))
        {
            SimpleLogger::setLogLevel(logDebug);
        }
    }

    // get values from env
    login.password = getenv("MEGA_PWD");
    login.email = getenv("MEGA_EMAIL");
    client->prelogin(login.email.c_str());

    while (true)
    {
        // pass the CPU to the engine (nonblocking)
        client->exec();
        client->wait();
    }

    return 0;
#endif
}
