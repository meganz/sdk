/**
 * @file tests/synctests.cpp
 * @brief Mega SDK test file
 *
 * (c) 2018 by Mega Limited, Wellsford, New Zealand
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

// Many of these tests are still being worked on.
// The file uses some C++17 mainly for the very convenient std::filesystem library, though the main SDK must still build with C++11 (and prior)


#include "test.h"
#include <mega.h>
#include "gtest/gtest.h"
#include <stdio.h>
#include <map>
#include <future>
//#include <mega/tsthooks.h>
#include <fstream>
#include <atomic>

#include <megaapi_impl.h>

#define DEFAULTWAIT std::chrono::seconds(20)

#ifdef ENABLE_SYNC

using namespace ::mega;
using namespace ::std;

#ifdef WIN32
 #include <filesystem>
 namespace fs = ::std::filesystem;
 #define LOCAL_TEST_FOLDER "c:\\tmp\\synctests"
#else
 #include <experimental/filesystem>
 namespace fs = ::std::experimental::filesystem;
 #define LOCAL_TEST_FOLDER (string(getenv("HOME"))+"/synctests_mega_auto")
#endif

namespace {

bool suppressfiles = false;

typedef ::mega::byte byte;

void WaitMillisec(unsigned n)
{
#ifdef _WIN32
    Sleep(n);
#else
    usleep(n * 1000);
#endif
}
struct Model
{
    // records what we think the tree should look like after sync so we can confirm it

    struct ModelNode
    {
        enum nodetype { file, folder };
        nodetype type = folder;
        string name;
        vector<uint8_t> data;
        vector<unique_ptr<ModelNode>> kids;
        ModelNode* parent = nullptr;

        string path() 
        {
            string s;
            for (auto p = this; p; p = p->parent) 
                s = "/" + p->name + s;
            return s;
        }
        void addkid(unique_ptr<ModelNode>&& p)
        {
            p->parent = this;
            kids.emplace_back(move(p));
        }
        bool typematchesnodetype(nodetype_t nodetype)
        {
            switch (type)
            {
            case file: return nodetype == FILENODE;
            case folder: return nodetype == FOLDERNODE;
            }
            return false;
        }
        void print(string prefix="")
        {
            cout << prefix << name << endl;
            prefix.append(name).append("/");
            for (const auto &in: kids)
            {
                in->print(prefix);
            }
        }

    };

    unique_ptr<ModelNode> makeModelSubfolder(const string& utf8Name)
    {
        unique_ptr<ModelNode> n(new ModelNode);
        n->name = utf8Name;
        return n;
    }

    unique_ptr<ModelNode> makeModelSubfile(const string& u8name, const void *data, const size_t data_length)
    {
        unique_ptr<ModelNode> node(new ModelNode());
        const uint8_t * const m = reinterpret_cast<const uint8_t *>(data);

        node->name = u8name;
        node->data.assign(m, m + data_length);
        node->type = ModelNode::file;

        return node;
    }

    unique_ptr<ModelNode> makeModelSubfile(const string& u8name, const std::vector<uint8_t> &data)
    {
        return makeModelSubfile(u8name, data.data(), data.size());
    }

    unique_ptr<ModelNode> makeModelSubfile(const string& u8name)
    {
        return makeModelSubfile(u8name, u8name.data(), u8name.size());
    }


    unique_ptr<ModelNode> buildModelSubdirs(const string& prefix, int n, int recurselevel, int filesperdir)
    {
        if (suppressfiles) filesperdir = 0;

        unique_ptr<ModelNode> nn = makeModelSubfolder(prefix);
        
        for (int i = 0; i < filesperdir; ++i)
        {
            nn->addkid(makeModelSubfile("file" + to_string(i) + "_" + prefix));
        }

        if (recurselevel > 0)
        {
            for (int i = 0; i < n; ++i)
            {
                unique_ptr<ModelNode> sn = buildModelSubdirs(prefix + "_" + to_string(i), n, recurselevel - 1, filesperdir);
                sn->parent = nn.get();
                nn->addkid(move(sn));
            }
        }
        return nn;
    }

    ModelNode* childnodebyname(ModelNode* n, const std::string& s)
    {
        for (auto& m : n->kids)
        {
            if (m->name == s)
            {
                return m.get();
            }
        }
        return nullptr;
    }

    ModelNode* findnode(string path, ModelNode* startnode = nullptr)
    {
        ModelNode* n = startnode ? startnode : root.get();
        while (n && !path.empty())
        {
            auto pos = path.find("/");
            n = childnodebyname(n, path.substr(0, pos));
            path.erase(0, pos == string::npos ? path.size() : pos + 1);
        }
        return n;
    }

    unique_ptr<ModelNode> removenode(const string& path)
    {
        ModelNode* n = findnode(path);
        if (n && n->parent)
        {
            unique_ptr<ModelNode> extracted;
            ModelNode* parent = n->parent;
            auto newend = std::remove_if(parent->kids.begin(), parent->kids.end(), [&extracted, n](unique_ptr<ModelNode>& v) { if (v.get() == n) return extracted = move(v), true; else return false; });
            parent->kids.erase(newend, parent->kids.end());
            return extracted;
        }
        return nullptr;
    }

    bool movenode(const string& sourcepath, const string& destpath)
    {
        ModelNode* source = findnode(sourcepath);
        ModelNode* dest = findnode(destpath);
        if (source && source && source->parent && dest)
        {
            unique_ptr<ModelNode> n;
            ModelNode* parent = source->parent;
            auto newend = std::remove_if(parent->kids.begin(), parent->kids.end(), [&n, source](unique_ptr<ModelNode>& v) { if (v.get() == source) return n = move(v), true; else return false; });
            parent->kids.erase(newend, parent->kids.end());
            if (n)
            {
                dest->addkid(move(n));
                return true;
            }
        }
        return false;
    }

    bool movetosynctrash(const string& path, const string& syncrootpath)
    {
        ModelNode* syncroot;
        if (!(syncroot = findnode(syncrootpath)))
        {
            return false;
        }

        ModelNode* trash;
        if (!(trash = childnodebyname(syncroot, DEBRISFOLDER)))
        {
            auto uniqueptr = makeModelSubfolder(DEBRISFOLDER);
            trash = uniqueptr.get();
            syncroot->addkid(move(uniqueptr));
        }

        char today[50];
        auto rawtime = time(NULL);
        strftime(today, sizeof today, "%F", localtime(&rawtime));

        ModelNode* dayfolder;
        if (!(dayfolder = findnode(today, trash)))
        {
            auto uniqueptr = makeModelSubfolder(today);
            dayfolder = uniqueptr.get();
            trash->addkid(move(uniqueptr));
        }

        if (auto uniqueptr = removenode(path))
        {
            dayfolder->addkid(move(uniqueptr));
            return true;
        }
        return false;
    }

    void ensureLocalDebrisTmpLock(const string& syncrootpath)
    {
        // if we've downloaded a file then it's put in debris/tmp initially, and there is a lock file
        if (ModelNode* syncroot = findnode(syncrootpath))
        {
            ModelNode* trash;
            if (!(trash = childnodebyname(syncroot, DEBRISFOLDER)))
            {
                auto uniqueptr = makeModelSubfolder(DEBRISFOLDER);
                trash = uniqueptr.get();
                syncroot->addkid(move(uniqueptr));
            }

            ModelNode* tmpfolder;
            if (!(tmpfolder = findnode("tmp", trash)))
            {
                auto uniqueptr = makeModelSubfolder("tmp");
                tmpfolder = uniqueptr.get();
                trash->addkid(move(uniqueptr));
            }

            ModelNode* lockfile;
            if (!(lockfile = findnode("lock", tmpfolder)))
            {
                tmpfolder->addkid(makeModelSubfile("lock"));
            }
        }
    }

    bool removesynctrash(const string& syncrootpath, const string& subpath = "")
    {
        if (subpath.empty())
        {
            return removenode(syncrootpath + "/" + DEBRISFOLDER).get();
        }
        else
        {
            char today[50];
            auto rawtime = time(NULL);
            strftime(today, sizeof today, "%F", localtime(&rawtime));

            return removenode(syncrootpath + "/" + DEBRISFOLDER + "/" + today + "/" + subpath).get();
        }
    }

    Model() : root(makeModelSubfolder("root"))
    {
    }

    unique_ptr<ModelNode> root;
};


bool waitonresults(future<bool>* r1 = nullptr, future<bool>* r2 = nullptr, future<bool>* r3 = nullptr, future<bool>* r4 = nullptr)
{
    if (r1) r1->wait();
    if (r2) r2->wait();
    if (r3) r3->wait();
    if (r4) r4->wait();
    return (!r1 || r1->get()) && (!r2 || r2->get()) && (!r3 || r3->get()) && (!r4 || r4->get());
}

struct StandardClient : public MegaApp
{
    WAIT_CLASS waiter;
#ifdef GFX_CLASS
    GFX_CLASS gfx;
#endif

    string client_dbaccess_path; 
    std::unique_ptr<HttpIO> httpio;
    std::unique_ptr<FileSystemAccess> fsaccess;
    MegaClient client;
    std::atomic<bool> clientthreadexit{false};
    bool fatalerror = false;
    string clientname;
    std::function<void(MegaClient&, promise<bool>&)> nextfunctionMC;
    std::promise<bool> nextfunctionMCpromise;
    std::function<void(StandardClient&, promise<bool>&)> nextfunctionSC;
    std::promise<bool> nextfunctionSCpromise;
    std::condition_variable functionDone;
    std::mutex functionDoneMutex;
    std::string salt;

    fs::path fsBasePath;

    handle basefolderhandle = UNDEF;

    // thread as last member so everything else is initialised before we start it
    std::thread clientthread;

    fs::path ensureDir(const fs::path& p)
    {
        fs::create_directories(p);
        return p;
    }

    StandardClient(const fs::path& basepath, const string& name)
        : client_dbaccess_path(ensureDir(basepath / name / "").u8string())
        , httpio(new HTTPIO_CLASS)
        , fsaccess(new FSACCESS_CLASS)
        , client(this, &waiter, httpio.get(), fsaccess.get(),
#ifdef DBACCESS_CLASS
            new DBACCESS_CLASS(&client_dbaccess_path),
#else
            NULL,
#endif
#ifdef GFX_CLASS
            &gfx,
#else
            NULL,
#endif
            "N9tSBJDC", USER_AGENT.c_str(), THREADS_PER_MEGACLIENT )
        , clientname(name)
        , fsBasePath(basepath / fs::u8path(name))
        , clientthread([this]() { threadloop(); })
    {
        client.clientname = clientname + " ";
#ifdef GFX_CLASS
        gfx.startProcessingThread();
#endif
    }

    ~StandardClient()
    {
        // shut down any syncs on the same thread, or they stall the client destruction (CancelIo instead of CancelIoEx on the WinDirNotify)
        thread_do([](MegaClient& mc, promise<bool>&) { 
            #ifdef _WIN32
                // logout stalls in windows due to the issue above
                mc.purgenodesusersabortsc(false); 
            #else
                mc.logout();
            #endif
        });

        clientthreadexit = true;
        waiter.notify();
        clientthread.join();
    }

    void localLogout()
    {
        thread_do([](MegaClient& mc, promise<bool>&) {
            #ifdef _WIN32
                // logout stalls in windows due to the issue above
                mc.purgenodesusersabortsc(false);
            #else
                mc.locallogout(false);
            #endif
        });
    }

    static mutex om;
    bool logcb = false;
    chrono::steady_clock::time_point lastcb = std::chrono::steady_clock::now();
    string lp(LocalNode* ln) { string lp;  ln->getlocalpath(&lp); client.fsaccess->local2name(&lp, client.fsaccess->getFilesystemType(&lp)); return lp; }
    void syncupdate_state(Sync*, syncstate_t state) override { if (logcb) { lock_guard<mutex> g(om);  cout << clientname << " syncupdate_state() " << state << endl; } }
    void syncupdate_scanning(bool b) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_scanning()" << b << endl; } }
    //void syncupdate_local_folder_addition(Sync* s, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_local_folder_addition() " << lp(ln) << " " << cp << endl; }}
    //void syncupdate_local_folder_deletion(Sync*, LocalNode* ln) override { if (logcb) { lock_guard<mutex> g(om);  cout << clientname << " syncupdate_local_folder_deletion() " << lp(ln) << endl; }}
    void syncupdate_local_folder_addition(Sync*, LocalNode* ln, const char* cp) override { lastcb = chrono::steady_clock::now(); }
    void syncupdate_local_folder_deletion(Sync*, LocalNode* ln) override { lastcb = chrono::steady_clock::now(); }
    void syncupdate_local_file_addition(Sync*, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_local_file_addition() " << lp(ln) << " " << cp << endl; }}
    void syncupdate_local_file_deletion(Sync*, LocalNode* ln) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_local_file_deletion() " << lp(ln) << endl; }}
    void syncupdate_local_file_change(Sync*, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_local_file_change() " << lp(ln) << " " << cp << endl; }}
    void syncupdate_local_move(Sync*, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_local_move() " << lp(ln) << " " << cp << endl; }}
    void syncupdate_local_lockretry(bool b) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_local_lockretry() " << b << endl; }}
    //void syncupdate_get(Sync*, Node* n, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_get()" << n->displaypath() << " " << cp << endl; }}
    void syncupdate_put(Sync*, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_put()" << lp(ln) << " " << cp << endl; }}
    //void syncupdate_remote_file_addition(Sync*, Node* n) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_file_addition() " << n->displaypath() << endl; }}
    //void syncupdate_remote_file_deletion(Sync*, Node* n) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_file_deletion() " << n->displaypath() << endl; }}
    //void syncupdate_remote_folder_addition(Sync*, Node* n) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_folder_addition() " << n->displaypath() << endl; }}
    //void syncupdate_remote_folder_deletion(Sync*, Node* n) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_folder_deletion() " << n->displaypath() << endl; }}
    void syncupdate_remote_folder_addition(Sync*, Node* n) override { lastcb = chrono::steady_clock::now(); }
    void syncupdate_remote_folder_deletion(Sync*, Node* n) override { lastcb = chrono::steady_clock::now(); }
    void syncupdate_remote_copy(Sync*, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_copy() " << cp << endl; }}
    //void syncupdate_remote_move(Sync*, Node* n1, Node* n2) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_move() " << n1->displaypath() << " " << n2->displaypath() << endl; }}
    //void syncupdate_remote_rename(Sync*, Node* n, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_rename() " << n->displaypath() << " " << cp << endl; }}
    //void syncupdate_treestate(LocalNode* ln) override { if (logcb) { lock_guard<mutex> g(om);   cout << clientname << " syncupdate_treestate() " << ln->ts << " " << ln->dts << " " << lp(ln) << endl; }}


    
    std::atomic<unsigned> transfersAdded{0}, transfersRemoved{0}, transfersPrepared{0}, transfersFailed{0}, transfersUpdated{0}, transfersComplete{0};
    void transfer_added(Transfer*) override { ++transfersAdded; }
    void transfer_removed(Transfer*) override { ++transfersRemoved; }
    void transfer_prepare(Transfer*) override { ++transfersPrepared; }
    void transfer_failed(Transfer*,  const Error&, dstime = 0) override { ++transfersFailed; }
    void transfer_update(Transfer*) override { ++transfersUpdated; }
    void transfer_complete(Transfer*) override { ++transfersComplete; }


    void threadloop()
        try
    {
        while (!clientthreadexit)
        {
            int r = client.wait();

            {
                std::lock_guard<mutex> g(functionDoneMutex);
                if (nextfunctionMC)
                {
                    nextfunctionMC(client, nextfunctionMCpromise);
                    nextfunctionMC = nullptr;
                    functionDone.notify_all();
                    r = Waiter::NEEDEXEC;
                }
                if (nextfunctionSC)
                {
                    nextfunctionSC(*this, nextfunctionSCpromise);
                    nextfunctionSC = nullptr;
                    functionDone.notify_all();
                    r = Waiter::NEEDEXEC;
                }
            }
            if (r & Waiter::NEEDEXEC)
            {
                client.exec();
            }
        }
        cout << clientname << " thread exiting naturally" << endl;
    }
    catch (std::exception& e)
    {
        cout << clientname << " thread exception, StandardClient " << clientname << " terminated: " << e.what() << endl;
    }
    catch (...)
    {
        cout << clientname << " thread exception, StandardClient " << clientname << " terminated" << endl;
    }

    static bool debugging;  // turn this on to prevent the main thread timing out when stepping in the MegaClient

    future<bool> thread_do(std::function<void(MegaClient&, promise<bool>&)>&& f)
    {
        unique_lock<mutex> guard(functionDoneMutex);
        nextfunctionMCpromise = promise<bool>();
        nextfunctionMC = std::move(f);
        waiter.notify();
        while (!functionDone.wait_until(guard, chrono::steady_clock::now() + chrono::seconds(600), [this]() { return !nextfunctionMC; }))
        {
            if (!debugging)
            {
                nextfunctionMCpromise.set_value(false);
                break;
            }
        }
        return nextfunctionMCpromise.get_future();
    }

    future<bool> thread_do(std::function<void(StandardClient&, promise<bool>&)>&& f)
    {
        unique_lock<mutex> guard(functionDoneMutex);
        nextfunctionSCpromise = promise<bool>();
        nextfunctionSC = std::move(f);
        waiter.notify();
        while (!functionDone.wait_until(guard, chrono::steady_clock::now() + chrono::seconds(600), [this]() { return !nextfunctionSC; }))
        {
            if (!debugging)
            {
                nextfunctionSCpromise.set_value(false);
                break;
            }
        }
        return nextfunctionSCpromise.get_future();
    }

    enum resultprocenum { PRELOGIN, LOGIN, FETCHNODES, PUTNODES, UNLINK, MOVENODE };

    void preloginFromEnv(const string& userenv, promise<bool>& pb)
    {
        string user = getenv(userenv.c_str());

        ASSERT_FALSE(user.empty());

        resultproc.prepresult(PRELOGIN, [&pb](error e) { pb.set_value(!e); });
        client.prelogin(user.c_str());
    }

    void loginFromEnv(const string& userenv, const string& pwdenv, promise<bool>& pb)
    {
        string user = getenv(userenv.c_str());
        string pwd = getenv(pwdenv.c_str());

        ASSERT_FALSE(user.empty());
        ASSERT_FALSE(pwd.empty());

        byte pwkey[SymmCipher::KEYLENGTH];

        resultproc.prepresult(LOGIN, [&pb](error e) { pb.set_value(!e); });
        if (client.accountversion == 1)
        {
            if (error e = client.pw_key(pwd.c_str(), pwkey))
            {
                ASSERT_TRUE(false) << "login error: " << e;
            }
            else
            {
                client.login(user.c_str(), pwkey);
            }
        }
        else if (client.accountversion == 2 && !salt.empty())
        {
            client.login2(user.c_str(), pwd.c_str(), &salt);
        }
        else
        {
            ASSERT_TRUE(false) << "Login unexpected error";
        }
    }

    void loginFromSession(const string& session, promise<bool>& pb)
    {
        resultproc.prepresult(LOGIN, [&pb](error e) { pb.set_value(!e); });
        client.login((byte*)session.data(), (int)session.size());
    }


    class TreeProcPrintTree : public TreeProc
    {
    public:
        void proc(MegaClient* client, Node* n) override
        {
            //cout << "fetchnodes tree: " << n->displaypath() << endl;;
        }
    };

    // mark node as removed and notify

    std::function<void (StandardClient& mc, promise<bool>& pb)> onFetchNodes;

    void fetchnodes(promise<bool>& pb)
    {
        resultproc.prepresult(FETCHNODES, [this, &pb](error e) 
        { 
            if (e)
            {
                pb.set_value(false);
            }
            else
            {
                TreeProcPrintTree tppt;
                client.proctree(client.nodebyhandle(client.rootnodes[0]), &tppt);

                if (onFetchNodes)
                {
                    onFetchNodes(*this, pb);
                }
                else
                {
                    pb.set_value(true);
                }
            }
            onFetchNodes = nullptr;
        });
        client.fetchnodes();
    }

    NewNode makeSubfolder(const string& utf8Name)
    {
        NewNode newnode;
        client.putnodes_prepareOneFolder(&newnode, utf8Name);
        return newnode;
    }
     

    struct ResultProc
    {
        struct id_callback
        {
            handle h = UNDEF;
            std::function<void(error)> f;
            id_callback(std::function<void(error)> cf, handle ch = UNDEF) : h(ch), f(cf) {}
        };

        map<resultprocenum, deque<id_callback>> m;

        void prepresult(resultprocenum rpe, std::function<void(error)>&& f, handle h = UNDEF)
        {
            auto& entry = m[rpe];
            entry.emplace_back(move(f), h);
        }

        void processresult(resultprocenum rpe, error e, handle h = UNDEF)
        {
            //cout << "procenum " << rpe << " result " << e << endl;
            auto& entry = m[rpe];
            if (rpe == MOVENODE)
            {
                // rename_result is called back for our app requests but also for sync objects as well, so we need to skip those... todo: should we change that?
                if (entry.empty() || entry.front().h != h)
                {
                    cout << "received unsolicited rename_result call" << endl;
                    return;
                }
            }
            if (!entry.empty())
            {
                entry.front().f(e);
                entry.pop_front();
            }
            else
            {
                assert(!entry.empty());
            }
        }
    } resultproc;

    void deleteTestBaseFolder(bool mayneeddeleting, promise<bool>& pb)
    {
        if (Node* root = client.nodebyhandle(client.rootnodes[0]))
        {
            if (Node* basenode = client.childnodebyname(root, "mega_test_sync", false))
            {
                if (mayneeddeleting)
                {
                    resultproc.prepresult(UNLINK, [this, &pb](error e) {
                        if (e)
                        {
                            cout << "delete of test base folder reply reports: " << e << endl;
                        }
                        deleteTestBaseFolder(false, pb);
                    });
                    //cout << "old test base folder found, deleting" << endl;
                    client.unlink(basenode);
                    return;
                }
                cout << "base folder found, but not expected, failing" << endl;
                pb.set_value(false);
                return;
            }
            else
            {
                //cout << "base folder not found, wasn't present or delete successful" << endl;
                pb.set_value(true);
                return;
            }
        }
        cout << "base folder not found, as root was not found!" << endl;
        pb.set_value(false);
    }

    void ensureTestBaseFolder(bool mayneedmaking, promise<bool>& pb)
    {
        if (Node* root = client.nodebyhandle(client.rootnodes[0]))
        {
            if (Node* basenode = client.childnodebyname(root, "mega_test_sync", false))
            {
                if (basenode->type == FOLDERNODE)
                {
                    basefolderhandle = basenode->nodehandle;
                    //cout << clientname << " Base folder: " << Base64Str<MegaClient::NODEHANDLE>(basefolderhandle) << endl;
                    //parentofinterest = Base64Str<MegaClient::NODEHANDLE>(basefolderhandle);
                    pb.set_value(true);
                    return;
                }
            }
            else if (mayneedmaking)
            {
                resultproc.prepresult(PUTNODES, [this, &pb](error e) {
                    ensureTestBaseFolder(false, pb);
                });
                auto nn = new NewNode[1]; // freed by putnodes_result
                nn[0] = makeSubfolder("mega_test_sync");
                client.putnodes(root->nodehandle, nn, 1);
                return;
            }
        }
        pb.set_value(false);
    }

    NewNode* buildSubdirs(list<NewNode>& nodes, const string& prefix, int n, int recurselevel)
    {
        nodes.emplace_back(makeSubfolder(prefix));
        auto& nn = nodes.back();
        nn.nodehandle = nodes.size();

        if (recurselevel > 0)
        {
            for (int i = 0; i < n; ++i)
            {
                buildSubdirs(nodes, prefix + "_" + to_string(i), n, recurselevel - 1)->parenthandle = nn.nodehandle;
            }
        }

        return &nn;
    }

    void makeCloudSubdirs(const string& prefix, int depth, int fanout, promise<bool>& pb, const string& atpath = "")
    {
        assert(basefolderhandle != UNDEF);

        std::list<NewNode> nodes;
        NewNode* nn = buildSubdirs(nodes, prefix, fanout, depth);
        nn->parenthandle = UNDEF;
        nn->ovhandle = UNDEF;

        Node* atnode = client.nodebyhandle(basefolderhandle);
        if (atnode && !atpath.empty())
        {
            atnode = drillchildnodebyname(atnode, atpath);
        }
        if (!atnode)
        {
            cout << "path not found: " << atpath << endl;
            pb.set_value(false);
        }
        else
        {
            resultproc.prepresult(PUTNODES, [&pb](error e) {
                pb.set_value(!e);
                if (e) 
                {
                    cout << "putnodes result: " << e << endl;
                }
            });
            auto nodearray = new NewNode[nodes.size()]; // freed by putnodes_result
            size_t i = 0;
            for (auto n = nodes.begin(); n != nodes.end(); ++n, ++i)
            {
                nodearray[i] = std::move(*n);
            }
            client.putnodes(atnode->nodehandle, nodearray, (int)nodes.size());
        }
    }

    struct SyncInfo { handle h; fs::path localpath; };
    map<int, SyncInfo> syncSet;

    Node* getcloudrootnode()
    {
        return client.nodebyhandle(client.rootnodes[0]);
    }

    Node* gettestbasenode()
    {
        return client.childnodebyname(getcloudrootnode(), "mega_test_sync", false);
    }

    Node* getcloudrubbishnode()
    {
        return client.nodebyhandle(client.rootnodes[RUBBISHNODE - ROOTNODE]);
    }

    Node* drillchildnodebyname(Node* n, const string& path)
    {
        for (size_t p = 0; n && p < path.size(); )
        {
            auto pos = path.find("/", p);
            if (pos == string::npos) pos = path.size();
            n = client.childnodebyname(n, path.substr(p, pos - p).c_str(), false);
            p = pos == string::npos ? path.size() : pos + 1;
        }
        return n;
    }

    vector<Node*> drillchildnodesbyname(Node* n, const string& path)
    {
        auto pos = path.find("/");
        if (pos == string::npos)
        {
            return client.childnodesbyname(n, path.c_str(), false);
        }
        else
        {
            vector<Node*> results, subnodes = client.childnodesbyname(n, path.c_str(), false);
            for (size_t i = subnodes.size(); i--; )
            {
                if (subnodes[i]->type != FILENODE)
                {
                    vector<Node*> v = drillchildnodesbyname(subnodes[i], path.substr(pos + 1));
                    results.insert(results.end(), v.begin(), v.end());
                }
            }
            return results;
        }
    }

    bool setupSync_inthread(int syncid, const string& subfoldername, const fs::path& localpath)
    {
        if (Node* n = client.nodebyhandle(basefolderhandle))
        {
            if (Node* m = drillchildnodebyname(n, subfoldername))
            {
                SyncConfig syncConfig{localpath.u8string(), m->nodehandle, 0};
                error e = client.addsync(std::move(syncConfig), DEBRISFOLDER, NULL, syncid);  // use syncid as tag
                if (!e)
                {
                    syncSet[syncid] = SyncInfo{ m->nodehandle, localpath };
                    return true;
                }
            }
        }
        return false;
    }


    bool recursiveConfirm(Model::ModelNode* mn, Node* n, int& descendants, const string& identifier, int depth, bool& firstreported)
    {
        // top level names can differ so we don't check those
        if (!mn || !n) return false;
        if (depth && mn->name != n->displayname())
        {
            cout << "Node name mismatch: " << mn->path() << " " << n->displaypath() << endl;
            return false;
        }
        if (!mn->typematchesnodetype(n->type))
        {
            cout << "Node type mismatch: " << mn->path() << ":" << mn->type << " " << n->displaypath() << ":" << n->type << endl;
            return false;
        }

        if (n->type == FILENODE) 
        {
            // not comparing any file versioning (for now)
            return true;
        }

        multimap<string, Model::ModelNode*> ms;
        multimap<string, Node*> ns;
        for (auto& m : mn->kids) ms.emplace(m->name, m.get());
        for (auto& n2 : n->children) ns.emplace(n2->displayname(), n2);

        int matched = 0;
        vector<string> matchedlist;
        for (auto m_iter = ms.begin(); m_iter != ms.end(); )
        {
            if (!depth && m_iter->first == DEBRISFOLDER)
            {
                m_iter = ms.erase(m_iter); // todo: add checks of the remote debris folder later
                continue;
            }

            auto er = ns.equal_range(m_iter->first);
            auto next_m = m_iter;
            ++next_m;
            bool any_equal_matched = false;
            for (auto i = er.first; i != er.second; ++i)
            {
                int rdescendants = 0;
                if (recursiveConfirm(m_iter->second, i->second, rdescendants, identifier, depth+1, firstreported))
                {
                    ++matched;
                    matchedlist.push_back(m_iter->first);
                    ns.erase(i);
                    ms.erase(m_iter);
                    descendants += rdescendants;
                    any_equal_matched = true;
                    break;
                }
            }
            if (!any_equal_matched)
            {
                break;
            }
            m_iter = next_m;
        }
        if (ns.empty() && ms.empty())
        {
            descendants += matched;
            return true;
        }
        else if (!firstreported)
        {
            firstreported = true;
            cout << clientname << " " << identifier << " after matching " << matched << " child nodes [";
            for (auto& ml : matchedlist) cout << ml << " ";
            cout << "](with " << descendants << " descendants) in " << mn->path() << ", ended up with unmatched model nodes:";
            for (auto& m : ms) cout << " " << m.first;
            cout << " and unmatched remote nodes:";
            for (auto& i : ns) cout << " " << i.first;
            cout << endl;
        };
        return false;
    }

    bool localNodesMustHaveNodes = true;

    bool recursiveConfirm(Model::ModelNode* mn, LocalNode* n, int& descendants, const string& identifier, int depth, bool& firstreported)
    {
        // top level names can differ so we don't check those
        if (!mn || !n) return false;
        if (depth && mn->name != n->name)
        {
            cout << "LocalNode name mismatch: " << mn->path() << " " << n->name << endl;
            return false;
        }
        if (!mn->typematchesnodetype(n->type))
        {
            cout << "LocalNode type mismatch: " << mn->path() << ":" << mn->type << " " << n->name << ":" << n->type << endl;
            return false;
        }

        string localpath;
        n->getlocalpath(&localpath, false);
        ::mega::FileSystemType fileSystemType = client.fsaccess->getFilesystemType(&localpath);
        client.fsaccess->local2name(&localpath, fileSystemType);
        string n_localname = n->localname;
        client.fsaccess->local2name(&n_localname, fileSystemType);
        if (n_localname.size())
        {
            EXPECT_EQ(n->name, n_localname);
        }
        if (localNodesMustHaveNodes)
        {
            EXPECT_TRUE(n->node != nullptr);
        }
        if (depth && n->node)
        {
            EXPECT_EQ(n->node->displayname(), n->name);
        }
        if (depth && mn->parent)
        {
            EXPECT_EQ(mn->parent->type, Model::ModelNode::folder);
            EXPECT_EQ(n->parent->type, FOLDERNODE);

            string parentpath;
            n->parent->getlocalpath(&parentpath, false);
            client.fsaccess->local2name(&parentpath, client.fsaccess->getFilesystemType(&parentpath));
            EXPECT_EQ(localpath.substr(0, parentpath.size()), parentpath);
        }
        if (n->node && n->parent && n->parent->node)
        {
            string p = n->node->displaypath();
            string pp = n->parent->node->displaypath();
            EXPECT_EQ(p.substr(0, pp.size()), pp);
            EXPECT_EQ(n->parent->node, n->node->parent);
        }

        multimap<string, Model::ModelNode*> ms;
        multimap<string, LocalNode*> ns;
        for (auto& m : mn->kids)
        {
            ms.emplace(m->name, m.get());
        }
        for (auto& n2 : n->children)
        {
            if (!n2.second->deleted) ns.emplace(n2.second->name, n2.second); // todo: should LocalNodes marked as deleted actually have been removed by now?
        }

        int matched = 0;
        vector<string> matchedlist;
        for (auto m_iter = ms.begin(); m_iter != ms.end(); )
        {
            if (!depth && m_iter->first == DEBRISFOLDER)
            {
                m_iter = ms.erase(m_iter); // todo: are there LocalNodes representing the trash?
                continue;
            }

            auto er = ns.equal_range(m_iter->first);
            auto next_m = m_iter;
            ++next_m;
            bool any_equal_matched = false;
            for (auto i = er.first; i != er.second; ++i)
            {
                int rdescendants = 0; 
                if (recursiveConfirm(m_iter->second, i->second, rdescendants, identifier, depth+1, firstreported))
                {
                    ++matched;
                    matchedlist.push_back(m_iter->first);
                    ns.erase(i);
                    ms.erase(m_iter);
                    descendants += rdescendants;
                    any_equal_matched = true;
                    break;
                }
            }
            if (!any_equal_matched)
            {
                break;
            }
            m_iter = next_m;
        }
        if (ns.empty() && ms.empty())
        {
            return true;
        }
        else if (!firstreported)
        {
            firstreported = true;
            cout << clientname << " " << identifier << " after matching " << matched << " child nodes [";
            for (auto& ml : matchedlist) cout << ml << " ";
            cout << "](with " << descendants << " descendants) in " << mn->path() << ", ended up with unmatched model nodes:";
            for (auto& m : ms) cout << " " << m.first;
            cout << " and unmatched LocalNodes:";
            for (auto& i : ns) cout << " " << i.first;
            cout << endl;
        };
        return false;
    }


    bool recursiveConfirm(Model::ModelNode* mn, fs::path p, int& descendants, const string& identifier, int depth, bool ignoreDebris, bool& firstreported)
    {
        if (!mn) return false;
        if (depth && mn->name != p.filename().u8string())
        {
            cout << "filesystem name mismatch: " << mn->path() << " " << p << endl;
            return false;
        }
        nodetype_t pathtype = fs::is_directory(p) ? FOLDERNODE : fs::is_regular_file(p) ? FILENODE : TYPE_UNKNOWN;
        if (!mn->typematchesnodetype(pathtype))
        {
            cout << "Path type mismatch: " << mn->path() << ":" << mn->type << " " << p.u8string() << ":" << pathtype << endl;
            return false;
        }

        if (pathtype == FILENODE && p.filename().u8string() != "lock")
        {
            std::vector<uint8_t> buffer;
            std::ifstream istream(p, ios::binary);

            buffer.reserve(mn->data.size());

            istream.read(reinterpret_cast<char *>(buffer.data()), buffer.capacity());

            if (static_cast<size_t>(istream.gcount()) != buffer.capacity()
                || !std::equal(mn->data.begin(), mn->data.end(), buffer.begin()))
            {
                return false;
            }
        }

        if (pathtype != FOLDERNODE)
        {
            return true;
        }

        multimap<string, Model::ModelNode*> ms;
        multimap<string, fs::path> ps;
        for (auto& m : mn->kids) ms.emplace(m->name, m.get());
        for (fs::directory_iterator pi(p); pi != fs::directory_iterator(); ++pi) ps.emplace(pi->path().filename().u8string(), pi->path());

        if (ignoreDebris)
        {
            ps.erase(DEBRISFOLDER);
        }

        int matched = 0;
        vector<string> matchedlist;
        for (auto m_iter = ms.begin(); m_iter != ms.end(); )
        {
            auto er = ps.equal_range(m_iter->first);
            auto next_m = m_iter;
            ++next_m;
            bool any_equal_matched = false;
            for (auto i = er.first; i != er.second; ++i)
            {
                int rdescendants = 0; 
                if (recursiveConfirm(m_iter->second, i->second, rdescendants, identifier, depth+1, ignoreDebris, firstreported))
                {
                    ++matched;
                    matchedlist.push_back(m_iter->first);
                    ps.erase(i);
                    ms.erase(m_iter);
                    descendants += rdescendants;
                    any_equal_matched = true;
                    break;
                }
            }
            if (!any_equal_matched)
            {
                break;
            }
            m_iter = next_m;
        }
        //if (ps.size() == 1 && !mn->parent && ps.begin()->first == DEBRISFOLDER)
        //{
        //    ps.clear();
        //}
        if (ps.empty() && ms.empty())
        {
            return true;
        }
        else if (!firstreported)
        {
            firstreported = true;
            cout << clientname << " " << identifier << " after matching " << matched << " child nodes [";
            for (auto& ml : matchedlist) cout << ml << " ";
            cout << "](with " << descendants << " descendants) in " << mn->path() << ", ended up with unmatched model nodes:";
            for (auto& m : ms) cout << " " << m.first;
            cout << " and unmatched filesystem paths:";
            for (auto& i : ps) cout << " " << i.second.filename();
            cout << " in " << p << endl;
        };
        return false;
    }

    Sync* syncByTag(int tag)
    {
        for (Sync* s : client.syncs)
        {
            if (s->tag == tag)
                return s;
        }
        return nullptr;
    }

    enum Confirm
    {
        CONFIRM_LOCALFS = 0x01,
        CONFIRM_LOCALNODE = 0x02,
        CONFIRM_LOCAL = CONFIRM_LOCALFS | CONFIRM_LOCALNODE,
        CONFIRM_REMOTE = 0x04,
        CONFIRM_ALL = CONFIRM_LOCAL | CONFIRM_REMOTE,
    };

    bool confirmModel(int syncid, Model::ModelNode* mnode, const Confirm confirm, const bool ignoreDebris)
    {
        auto si = syncSet.find(syncid);
        if (si == syncSet.end())
        {
            cout << clientname << " syncid " << syncid << " not found " << endl;
            return false;
        }

        // compare model against nodes representing remote state
        int descendants = 0;
        bool firstreported = false;
        if (confirm & CONFIRM_REMOTE && !recursiveConfirm(mnode, client.nodebyhandle(si->second.h), descendants, "Sync " + to_string(syncid), 0, firstreported))
        {
            cout << clientname << " syncid " << syncid << " comparison against remote nodes failed" << endl;
            return false;
        }

        // compare model against LocalNodes
        descendants = 0; 
        if (Sync* sync = syncByTag(syncid))
        {
            bool firstreported = false;
            if (confirm & CONFIRM_LOCALNODE && !recursiveConfirm(mnode, sync->localroot.get(), descendants, "Sync " + to_string(syncid), 0, firstreported))
            {
                cout << clientname << " syncid " << syncid << " comparison against LocalNodes failed" << endl;
                return false;
            }
        }

        // compare model against local filesystem
        descendants = 0;
        firstreported = false;
        if (confirm & CONFIRM_LOCALFS && !recursiveConfirm(mnode, si->second.localpath, descendants, "Sync " + to_string(syncid), 0, ignoreDebris, firstreported))
        {
            cout << clientname << " syncid " << syncid << " comparison against local filesystem failed" << endl;
            return false;
        }

        return true;
    }

    void prelogin_result(int, string*, string* salt, error e) override
    {
        cout << clientname << " Prelogin: " << e << endl;
        if (!e)
        {
            this->salt = *salt;
        }
        resultproc.processresult(PRELOGIN, e);
    }

    void login_result(error e) override
    {
        cout << clientname << " Login: " << e << endl;
        resultproc.processresult(LOGIN, e);
    }

    void fetchnodes_result(const Error& e) override
    {
        cout << clientname << " Fetchnodes: " << e << endl;
        resultproc.processresult(FETCHNODES, e);
    }

    void unlink_result(handle, error e) override
    { 
        resultproc.processresult(UNLINK, e);
    }

    void putnodes_result(error e, targettype_t tt, NewNode* nn) override
    {
        if (nn)  // ignore sync based putnodes
        {
            resultproc.processresult(PUTNODES, e);
            delete[] nn;
        }
    }

    void rename_result(handle h, error e)  override
    { 
        resultproc.processresult(MOVENODE, e, h);
    }

    void deleteremote(string path, promise<bool>& pb )
    {
        if (Node* n = drillchildnodebyname(gettestbasenode(), path))
        {
        resultproc.prepresult(UNLINK, [ &pb](error e) { pb.set_value(!e); });
            client.unlink(n);
        }
        else
        {
            pb.set_value(false);
        }
    }

    void deleteremotenodes(vector<Node*> ns, promise<bool>& pb)
    {
        if (ns.empty())
        {
            pb.set_value(true);
        }
        else
        {
            for (size_t i = ns.size(); i--; )
            {
                resultproc.prepresult(UNLINK, [&pb, i](error e) { if (!i) pb.set_value(!e); });
                client.unlink(ns[i]);
            }
        }
    }

    void movenode(string path, string newparentpath, promise<bool>& pb)
    {
        Node* n = drillchildnodebyname(gettestbasenode(), path);
        Node* p = drillchildnodebyname(gettestbasenode(), path);
        if (n && p)
        {
            resultproc.prepresult(MOVENODE, [&pb](error e) { pb.set_value(!e); }, n->nodehandle);
            client.rename(n, p);
            return;
        }
        cout << "node or new parent not found" << endl;
        pb.set_value(false);
    }

    void movenode(handle h1, handle h2, promise<bool>& pb)
    {
        Node* n = client.nodebyhandle(h1);
        Node* p = client.nodebyhandle(h2);
        if (n && p)
        {
        resultproc.prepresult(MOVENODE, [&pb](error e) { pb.set_value(!e); }, n->nodehandle);
            client.rename(n, p);
            return;
        }
        cout << "node or new parent not found by handle" << endl;
        pb.set_value(false);
    }

    void movenodetotrash(string path, promise<bool>& pb)
    {
        Node* n = drillchildnodebyname(gettestbasenode(), path);
        Node* p = getcloudrubbishnode();
        if (n && p && n->parent)
        {
            resultproc.prepresult(MOVENODE, [&pb](error e) { pb.set_value(!e); }, n->nodehandle);
            client.rename(n, p, SYNCDEL_NONE, n->parent->nodehandle);
            return;
        }
        cout << "node or rubbish or node parent not found" << endl;
        pb.set_value(false);
    }



    void waitonsyncs(chrono::seconds d = chrono::seconds(2))
    {
        auto start = chrono::steady_clock::now();
        for (;;)
        {
            bool any_add_del = false;;
            vector<int> syncstates;

            thread_do([&syncstates, &any_add_del, this](StandardClient& mc, promise<bool>&)
            {
                for (auto& sync : mc.client.syncs)
                {
                    syncstates.push_back(sync->state);
                    if (sync->deleteq.size() || sync->insertq.size())
                        any_add_del = true;
                }
                if (!(client.todebris.empty() && client.tounlink.empty() && client.synccreate.empty()))
                {
                    any_add_del = true;
                }
            });
            bool allactive = true;
            {
                lock_guard<mutex> g(StandardClient::om);
                //std::cout << "sync state: ";
                //for (auto n : syncstates)
                //{
                //    cout << n;
                //    if (n != SYNC_ACTIVE) allactive = false;
                //}
                //cout << endl;
            }

            if (any_add_del || debugging)
            {
                start = chrono::steady_clock::now();
            }

            if (allactive && ((chrono::steady_clock::now() - start) > d) && ((chrono::steady_clock::now() - lastcb) > d))
            {
               break;
            }
//cout << "waiting 500" << endl;
            WaitMillisec(500);
        }

    }

    bool login_reset(const string& user, const string& pw)
    {
        future<bool> p1;
        p1 = thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.preloginFromEnv(user, pb); });
        if (!waitonresults(&p1))
        {
            cout << "preloginFromEnv failed" << endl;
            return false;
        }
        p1 = thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.loginFromEnv(user, pw, pb); });
        if (!waitonresults(&p1))
        {
            cout << "loginFromEnv failed" << endl;
            return false;
        }
        p1 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.fetchnodes(pb); });
        if (!waitonresults(&p1)) {
            cout << "fetchnodes failed" << endl;
            return false;
        }
        p1 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.deleteTestBaseFolder(true, pb); });
        if (!waitonresults(&p1)) {
            cout << "deleteTestBaseFolder failed" << endl;
            return false;
        }
        p1 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.ensureTestBaseFolder(true, pb); });
        if (!waitonresults(&p1)) {
            cout << "ensureTestBaseFolder failed" << endl;
            return false;
        }
        return true;
    }

    bool login_reset_makeremotenodes(const string& user, const string& pw, const string& prefix, int depth, int fanout)
    {
        if (!login_reset(user, pw))
        {
            cout << "login_reset failed" << endl;
            return false;
        }
        future<bool> p1 = thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.makeCloudSubdirs(prefix, depth, fanout, pb); });
        if (!waitonresults(&p1))
        {
            cout << "makeCloudSubdirs failed" << endl;
            return false;
        }
        return true;
    }

    bool login_fetchnodes(const string& user, const string& pw, bool makeBaseFolder = false)
    {
        future<bool> p2;
        p2 = thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.preloginFromEnv(user, pb); });
        if (!waitonresults(&p2)) return false;
        p2 = thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.loginFromEnv(user, pw, pb); });
        if (!waitonresults(&p2)) return false;
        p2 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.fetchnodes(pb); });
        if (!waitonresults(&p2)) return false;
        p2 = thread_do([makeBaseFolder](StandardClient& sc, promise<bool>& pb) { sc.ensureTestBaseFolder(makeBaseFolder, pb); });
        if (!waitonresults(&p2)) return false;
        return true;
    }

    bool login_fetchnodes(const string& session)
    {
        future<bool> p2;
        p2 = thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.loginFromSession(session, pb); });
        if (!waitonresults(&p2)) return false; 
        p2 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.fetchnodes(pb); });
        if (!waitonresults(&p2)) return false;
        p2 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.ensureTestBaseFolder(false, pb); });
        if (!waitonresults(&p2)) return false;
        return true;
    }

    bool login_fetchnodes_resumesync(const string& session, const string& localsyncpath, const std::string& remotesyncrootfolder, int syncid)
    {
        future<bool> p2;
        p2 = thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.loginFromSession(session, pb); });
        if (!waitonresults(&p2)) return false;
        
        assert(!onFetchNodes);
        onFetchNodes = [=](StandardClient& mc, promise<bool>& pb)
        {
            mc.syncSet[syncid] = StandardClient::SyncInfo{ mc.drillchildnodebyname(mc.gettestbasenode(), remotesyncrootfolder)->nodehandle, localsyncpath };
            pb.set_value(true);
        };

        p2 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.fetchnodes(pb); });
        if (!waitonresults(&p2)) return false;
        p2 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.ensureTestBaseFolder(false, pb); });
        if (!waitonresults(&p2)) return false;
        return true;
    }

    bool setupSync_mainthread(const std::string& localsyncrootfolder, const std::string& remotesyncrootfolder, int syncid)
    {
        fs::path syncdir = fsBasePath / fs::u8path(localsyncrootfolder);
        fs::create_directory(syncdir);
        future<bool> fb = thread_do([=](StandardClient& mc, promise<bool>& pb) { pb.set_value(mc.setupSync_inthread(syncid, remotesyncrootfolder, syncdir)); });
        return fb.get();
    }

    bool confirmModel_mainthread(Model::ModelNode* mnode, int syncid, const bool ignoreDebris = false, const Confirm confirm = CONFIRM_ALL)
    {
        future<bool> fb;
        fb = thread_do([syncid, mnode, ignoreDebris, confirm](StandardClient& sc, promise<bool>& pb) { pb.set_value(sc.confirmModel(syncid, mnode, confirm, ignoreDebris)); });
        return fb.get();
    }

};


void waitonsyncs(chrono::seconds d = std::chrono::seconds(4), StandardClient* c1 = nullptr, StandardClient* c2 = nullptr, StandardClient* c3 = nullptr, StandardClient* c4 = nullptr)
{
    auto start = chrono::steady_clock::now();
    std::vector<StandardClient*> v{ c1, c2, c3, c4 };
    bool onelastsyncdown = true;
    for (;;)
    {
        bool any_add_del = false;
        vector<int> syncstates;

        for (auto vn : v) if (vn)
        {
            vn->thread_do([&syncstates, &any_add_del](StandardClient& mc, promise<bool>&)
            {
                for (auto& sync : mc.client.syncs)
                {
                    syncstates.push_back(sync->state);
                    if (sync->deleteq.size() || sync->insertq.size())
                        any_add_del = true;
                }
                if (!(mc.client.todebris.empty() && mc.client.tounlink.empty() && mc.client.synccreate.empty() 
                    && mc.client.transferlist.transfers[GET].empty() && mc.client.transferlist.transfers[PUT].empty()))
                {
                    any_add_del = true;
                }
            });
        }

        bool allactive = true;
        {
            //lock_guard<mutex> g(StandardClient::om);
            //std::cout << "sync state: ";
            //for (auto n : syncstates)
            //{
            //    cout << n;
            //    if (n != SYNC_ACTIVE) allactive = false;
            //}
            //cout << endl;
        }

        if (any_add_del || StandardClient::debugging)
        {
            start = chrono::steady_clock::now();
        }

        if (onelastsyncdown && (chrono::steady_clock::now() - start + d/2) > d)
        {
            // synced folders that were removed remotely don't have the corresponding local folder removed unless we prompt an extra syncdown.  // todo:  do we need to fix
            for (auto vn : v) if (vn) vn->client.syncdownrequired = true;
            onelastsyncdown = false;
        }

        for (auto vn : v) if (vn)
        {
            if (allactive && ((chrono::steady_clock::now() - start) > d) && ((chrono::steady_clock::now() - vn->lastcb) > d))
            {
                return;
            }
        }

        WaitMillisec(400);
    }

}


mutex StandardClient::om;
bool StandardClient::debugging = false;

void moveToTrash(const fs::path& p)
{
    fs::path trashpath = p.parent_path() / "trash";
    fs::create_directory(trashpath);
    fs::path newpath = trashpath / p.filename();
    for (int i = 2; fs::exists(newpath); ++i)
    {
        newpath = trashpath / fs::u8path(p.filename().stem().u8string() + "_" + to_string(i) + p.extension().u8string());
    }
    fs::rename(p, newpath);
}

fs::path makeNewTestRoot(fs::path p)
{
    if (fs::exists(p))
    {
        moveToTrash(p);
    }
    #ifndef NDEBUG
    bool b =
    #endif
    fs::create_directory(p);
    assert(b);
    return p;
}

//std::atomic<int> fileSizeCount = 20;

bool createFile(const fs::path &path, const void *data, const size_t data_length)
{
#if (__cplusplus >= 201700L)
    ofstream ostream(path, ios::binary);
#else
    ofstream ostream(path.u8string(), ios::binary);
#endif

    ostream.write(reinterpret_cast<const char *>(data), data_length);

    return ostream.good();
}

bool createFile(const fs::path &path, const std::vector<uint8_t> &data)
{
    return createFile(path, data.data(), data.size());
}

bool createFile(const fs::path &p, const string &filename)
{
    return createFile(p / fs::u8path(filename), filename.data(), filename.size());
}

bool createFileWithTimestamp(const fs::path &path,
                             const std::vector<uint8_t> &data,
                             const fs::file_time_type &timestamp)
{
    const bool result = createFile(path, data);

    if (result)
    {
        fs::last_write_time(path, timestamp);
    }

    return result;
}

bool buildLocalFolders(fs::path targetfolder, const string& prefix, int n, int recurselevel, int filesperfolder)
{
    if (suppressfiles) filesperfolder = 0;

    fs::path p = targetfolder / fs::u8path(prefix);
    if (!fs::create_directory(p))
        return false;

    for (int i = 0; i < filesperfolder; ++i)
    {
        string filename = "file" + to_string(i) + "_" + prefix;
        createFile(p, filename);
        //int thisSize = (++fileSizeCount)/2;
        //for (int j = 0; j < thisSize; ++j) fs << ('0' + j % 10);
    }

    if (recurselevel > 0)
    {
        for (int i = 0; i < n; ++i)
        {
            if (!buildLocalFolders(p, prefix + "_" + to_string(i), n, recurselevel - 1, filesperfolder))
                return false;
        }
    }

    return true;
}

void renameLocalFolders(fs::path targetfolder, const string& newprefix)
{
    std::list<fs::path> toRename;
    for (fs::directory_iterator i(targetfolder); i != fs::directory_iterator(); ++i)
    {
        if (fs::is_directory(i->path()))
        {
            renameLocalFolders(i->path(), newprefix);
        }
        toRename.push_back(i->path());
    }

    for (auto p : toRename)
    {
        auto newpath = p.parent_path() / (newprefix + p.filename().u8string());
        fs::rename(p, newpath);
    }
}


#ifdef __linux__
bool createSpecialFiles(fs::path targetfolder, const string& prefix, int n = 1)
{
    fs::path p = targetfolder;
    for (int i = 0; i < n; ++i)
    {
        string filename = "file" + to_string(i) + "_" + prefix;
        fs::path fp = p / fs::u8path(filename);

        int fdtmp = openat(AT_FDCWD, p.c_str(), O_RDWR|O_CLOEXEC|O_TMPFILE, 0600);
        write(fdtmp, filename.data(), filename.size());

        stringstream fdproc;
        fdproc << "/proc/self/fd/";
        fdproc << fdtmp;

        int r = linkat(AT_FDCWD, fdproc.str().c_str() , AT_FDCWD, fp.c_str(), AT_SYMLINK_FOLLOW);
        if (r)
        {
            cerr << " errno =" << errno << endl;
            return false;
        }
        close(fdtmp);
    }
    return true;
}
#endif

} // anonymous

class SyncFingerprintCollision
  : public ::testing::Test
{
public:
    SyncFingerprintCollision()
      : client0()
      , client1()
      , model0()
      , model1()
      , arbitraryFileLength(16384)
    {
        const fs::path root = makeNewTestRoot(LOCAL_TEST_FOLDER);

        client0 = std::make_unique<StandardClient>(root, "c0");
        client1 = std::make_unique<StandardClient>(root, "c1");

        client0->logcb = true;
        client1->logcb = true;
    }

    ~SyncFingerprintCollision()
    {
    }

    void SetUp() override
    {
        ASSERT_TRUE(client0->login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "d", 1, 2));
        ASSERT_TRUE(client1->login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
        ASSERT_EQ(client0->basefolderhandle, client1->basefolderhandle);

        model0.root->addkid(model0.buildModelSubdirs("d", 2, 1, 0));
        model1.root->addkid(model1.buildModelSubdirs("d", 2, 1, 0));

        startSyncs();
        waitOnSyncs();
        confirmModels();
    }

    void addModelFile(Model &model,
                      const std::string &directory,
                      const std::string &file, 
                      const std::vector<uint8_t> &content)
    {
        auto *node = model.findnode(directory);
        ASSERT_NE(node, nullptr);

        node->addkid(model.makeModelSubfile(file, content));
    }

    void confirmModel(StandardClient &client, Model &model, const int id)
    {
        ASSERT_TRUE(client.confirmModel_mainthread(model.findnode("d"), id));
    }

    void confirmModels()
    {
        confirmModel(*client0, model0, 0);
        confirmModel(*client1, model1, 1);
    }

    const fs::path &localRoot(const StandardClient &client) const
    {
        return client.syncSet.at(0).localpath;
    }

    std::vector<uint8_t> randomData(const std::size_t length) const
    {
        std::vector<uint8_t> data(length);

        std::generate_n(data.begin(), data.size(), [](){ return (uint8_t)std::rand(); });

        return data;
    }

    void startSyncs()
    {
        ASSERT_TRUE(client0->setupSync_mainthread("s0", "d", 0));
        ASSERT_TRUE(client1->setupSync_mainthread("s1", "d", 1));
    }

    void waitOnSyncs()
    {
        waitonsyncs(chrono::seconds(4), client0.get(), client1.get());
    }

    std::unique_ptr<StandardClient> client0;
    std::unique_ptr<StandardClient> client1;
    Model model0;
    Model model1;
    const std::size_t arbitraryFileLength;
}; /* SyncFingerprintCollision */

TEST_F(SyncFingerprintCollision, DifferentMacSameName)
{
    auto data0 = randomData(arbitraryFileLength);
    auto data1 = data0;
    const auto path0 = localRoot(*client0) / "d_0" / "a";
    const auto path1 = localRoot(*client0) / "d_1" / "a";

    // Alter MAC but leave fingerprint untouched.
    data1[0x41] = static_cast<uint8_t>(~data1[0x41]);

    ASSERT_TRUE(createFile(path0, data0));
    waitOnSyncs();

    auto result0 =
      client0->thread_do([&](StandardClient &sc, std::promise<bool> &p)
                         {
                             p.set_value(
                               createFileWithTimestamp(
                                 path1,
                                 data1,
                                 fs::last_write_time(path0)));
                         });

    ASSERT_TRUE(waitonresults(&result0));
    waitOnSyncs();

    addModelFile(model0, "d/d_0", "a", data0);
    addModelFile(model0, "d/d_1", "a", data1);
    addModelFile(model1, "d/d_0", "a", data0);
    addModelFile(model1, "d/d_1", "a", data0);
    model1.ensureLocalDebrisTmpLock("d");

    confirmModels();
}

TEST_F(SyncFingerprintCollision, DifferentMacDifferentName)
{
    auto data0 = randomData(arbitraryFileLength);
    auto data1 = data0;
    const auto path0 = localRoot(*client0) / "d_0" / "a";
    const auto path1 = localRoot(*client0) / "d_0" / "b";

    data1[0x41] = static_cast<uint8_t>(~data1[0x41]);

    ASSERT_TRUE(createFile(path0, data0));
    waitOnSyncs();

    auto result0 =
      client0->thread_do([&](StandardClient &sc, std::promise<bool> &p)
                         {
                             p.set_value(
                               createFileWithTimestamp(
                                 path1,
                                 data1,
                                 fs::last_write_time(path0)));
                         });

    ASSERT_TRUE(waitonresults(&result0));
    waitOnSyncs();

    addModelFile(model0, "d/d_0", "a", data0);
    addModelFile(model0, "d/d_0", "b", data1);
    addModelFile(model1, "d/d_0", "a", data0);
    addModelFile(model1, "d/d_0", "b", data1);
    model1.ensureLocalDebrisTmpLock("d");

    confirmModels();
}

TEST_F(SyncFingerprintCollision, SameMacDifferentName)
{
    auto data0 = randomData(arbitraryFileLength);
    const auto path0 = localRoot(*client0) / "d_0" / "a";
    const auto path1 = localRoot(*client0) / "d_0" / "b";

    ASSERT_TRUE(createFile(path0, data0));
    waitOnSyncs();

    auto result0 =
      client0->thread_do([&](StandardClient &sc, std::promise<bool> &p)
                         {
                             p.set_value(
                               createFileWithTimestamp(
                                 path1,
                                 data0,
                                 fs::last_write_time(path0)));
                         });

    ASSERT_TRUE(waitonresults(&result0));
    waitOnSyncs();

    addModelFile(model0, "d/d_0", "a", data0);
    addModelFile(model0, "d/d_0", "b", data0);
    addModelFile(model1, "d/d_0", "a", data0);
    addModelFile(model1, "d/d_0", "b", data0);
    model1.ensureLocalDebrisTmpLock("d");

    confirmModels();
}

GTEST_TEST(Sync, BasicSync_DelRemoteFolder)
{
    // delete a remote folder and confirm the client sending the request and another also synced both correctly update the disk
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2


    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // delete something remotely and let sync catch up
    future<bool> fb = clientA1.thread_do([](StandardClient& sc, promise<bool>& pb) { sc.deleteremote("f/f_2/f_2_1", pb); });
    ASSERT_TRUE(waitonresults(&fb));
    waitonsyncs(std::chrono::seconds(60), &clientA1, &clientA2);

    // check everything matches in both syncs (model has expected state of remote and local)
    ASSERT_TRUE(model.movetosynctrash("f/f_2/f_2_1", "f"));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}

GTEST_TEST(Sync, BasicSync_DelLocalFolder)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // delete something in the local filesystem and see if we catch up in A1 and A2 (deleter and observer syncs)
    error_code e;
    ASSERT_TRUE(fs::remove_all(clientA1.syncSet[1].localpath / "f_2" / "f_2_1", e) != static_cast<std::uintmax_t>(-1)) << e;

    // let them catch up
    waitonsyncs(std::chrono::seconds(60), &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(model.movetosynctrash("f/f_2/f_2_1", "f"));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
    ASSERT_TRUE(model.removesynctrash("f"));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
}

GTEST_TEST(Sync, BasicSync_MoveLocalFolder)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // move something in the local filesystem and see if we catch up in A1 and A2 (deleter and observer syncs)
    error_code rename_error;
    fs::rename(clientA1.syncSet[1].localpath / "f_2" / "f_2_1", clientA1.syncSet[1].localpath / "f_2_1", rename_error);
    ASSERT_TRUE(!rename_error) << rename_error;

    // let them catch up
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(model.movenode("f/f_2/f_2_1", "f"));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}

GTEST_TEST(Sync, BasicSync_MoveLocalFolderBetweenSyncs)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2
    StandardClient clientA3(localtestroot, "clientA3");   // user 1 client 3

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_TRUE(clientA3.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    // set up sync for A1 and A2, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f/f_0", 11));
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync2", "f/f_2", 12));
    ASSERT_TRUE(clientA2.setupSync_mainthread("syncA2_1", "f/f_0", 21));
    ASSERT_TRUE(clientA2.setupSync_mainthread("syncA2_2", "f/f_2", 22));
    ASSERT_TRUE(clientA3.setupSync_mainthread("syncA3", "f", 31));
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2, &clientA3);
    clientA1.logcb = clientA2.logcb = clientA3.logcb = true;

    // check everything matches (model has expected state of remote and local)
    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f/f_0"), 11));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f/f_2"), 12));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f/f_0"), 21));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f/f_2"), 22));
    ASSERT_TRUE(clientA3.confirmModel_mainthread(model.findnode("f"), 31));

    // move a folder form one local synced folder to another local synced folder and see if we sync correctly and catch up in A2 and A3 (mover and observer syncs)
    error_code rename_error;
    fs::path path1 = clientA1.syncSet[11].localpath / "f_0_1";
    fs::path path2 = clientA1.syncSet[12].localpath / "f_2_1" / "f_2_1_0" / "f_0_1";
    fs::rename(path1, path2, rename_error);
    ASSERT_TRUE(!rename_error) << rename_error;

    // let them catch up
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2, &clientA3);

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(model.movenode("f/f_0/f_0_1", "f/f_2/f_2_1/f_2_1_0"));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f/f_0"), 11));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f/f_2"), 12));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f/f_0"), 21));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f/f_2"), 22));
    ASSERT_TRUE(clientA3.confirmModel_mainthread(model.findnode("f"), 31));
}



GTEST_TEST(Sync, BasicSync_AddLocalFolder)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // make new folders (and files) in the local filesystem and see if we catch up in A1 and A2 (adder and observer syncs)
    ASSERT_TRUE(buildLocalFolders(clientA1.syncSet[1].localpath / "f_2", "newkid", 2, 2, 2));

    // let them catch up
    waitonsyncs(std::chrono::seconds(30), &clientA1, &clientA2);  // two minutes should be long enough to get past API_ETEMPUNAVAIL == -18 for sync2 downloading the files uploaded by sync1

    // check everything matches (model has expected state of remote and local)
    model.findnode("f/f_2")->addkid(model.buildModelSubdirs("newkid", 2, 2, 2));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    model.ensureLocalDebrisTmpLock("f"); // since we downloaded files
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}


// todo: add this test once the sync can keep up with file system notifications - at the moment
// it's too slow because we wait for the cloud before processing the next layer of files+folders.
// So if we add enough changes to exercise the notification queue, we can't check the results because
// it's far too slow at the syncing stage.
GTEST_TEST(Sync, BasicSync_MassNotifyFromLocalFolderTree)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    //StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 0, 0));
    //ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    //ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    //ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));
    waitonsyncs(std::chrono::seconds(4), &clientA1/*, &clientA2*/);
    //clientA1.logcb = clientA2.logcb = true;

    // Create a directory tree in one sync, it should be synced to the cloud and back to the other
    // Create enough files and folders that we put a strain on the notification logic: 3k entries
    ASSERT_TRUE(buildLocalFolders(clientA1.syncSet[1].localpath, "initial", 0, 0, 16000));

    //waitonsyncs(std::chrono::seconds(10), &clientA1 /*, &clientA2*/);
    std::this_thread::sleep_for(std::chrono::seconds(20));

    Model model;
    model.root->addkid(model.buildModelSubdirs("initial", 0, 0, 16000));

    // check everything matches (just local since it'll still be uploading files)
    clientA1.localNodesMustHaveNodes = false;
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.root.get(), 1, false, StandardClient::CONFIRM_LOCAL));
    //ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    ASSERT_GT(clientA1.transfersAdded.load(), 0u);
    clientA1.transfersAdded = 0;

    // rename all those files and folders, put a strain on the notify system again.
    // Also, no downloads (or uploads) should occur as a result of this.
 //   renameLocalFolders(clientA1.syncSet[1].localpath, "renamed_");

    // let them catch up
    //waitonsyncs(std::chrono::seconds(10), &clientA1 /*, &clientA2*/);  

    // rename is too slow to check, even just in localnodes, for now.

    //ASSERT_EQ(clientA1.transfersAdded.load(), 0u);

    //Model model2;
    //model2.root->addkid(model.buildModelSubdirs("renamed_initial", 0, 0, 100));

    //// check everything matches (model has expected state of remote and local)
    //ASSERT_TRUE(clientA1.confirmModel_mainthread(model2.root.get(), 1));
    ////ASSERT_TRUE(clientA2.confirmModel_mainthread(model2.findnode("f"), 2));
}               



/* this one is too slow for regular testing with the current algorithm
GTEST_TEST(Sync, BasicSync_MAX_NEWNODES1)
{
    // create more nodes than we can upload in one putnodes.
    // this tree is 5x5 and the algorithm ends up creating nodes one at a time so it's pretty slow (and doesn't hit MAX_NEWNODES as a result)
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // make new folders in the local filesystem and see if we catch up in A1 and A2 (adder and observer syncs)
    assert(MegaClient::MAX_NEWNODES < 3125);
    ASSERT_TRUE(buildLocalFolders(clientA1.syncSet[1].localpath, "g", 5, 5, 0));  // 5^5=3125 leaf folders, 625 pre-leaf etc

    // let them catch up
    waitonsyncs(std::chrono::seconds(30), &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    model.findnode("f")->addkid(model.buildModelSubdirs("g", 5, 5, 0));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}
*/

/* this one is too slow for regular testing with the current algorithm
GTEST_TEST(Sync, BasicSync_MAX_NEWNODES2)
{
    // create more nodes than we can upload in one putnodes.
    // this tree is 5x5 and the algorithm ends up creating nodes one at a time so it's pretty slow (and doesn't hit MAX_NEWNODES as a result)
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // make new folders in the local filesystem and see if we catch up in A1 and A2 (adder and observer syncs)
    assert(MegaClient::MAX_NEWNODES < 3000);
    ASSERT_TRUE(buildLocalFolders(clientA1.syncSet[1].localpath, "g", 3000, 1, 0));

    // let them catch up
    waitonsyncs(std::chrono::seconds(30), &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    model.findnode("f")->addkid(model.buildModelSubdirs("g", 3000, 1, 0));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}
*/

GTEST_TEST(Sync, BasicSync_MoveExistingIntoNewLocalFolder)
{
    // historic case:  in the local filesystem, create a new folder then move an existing file/folder into it
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // make new folder in the local filesystem
    ASSERT_TRUE(buildLocalFolders(clientA1.syncSet[1].localpath, "new", 1, 0, 0));
    // move an already synced folder into it
    error_code rename_error;
    fs::path path1 = clientA1.syncSet[1].localpath / "f_2"; // / "f_2_0" / "f_2_0_0";
    fs::path path2 = clientA1.syncSet[1].localpath / "new" / "f_2"; // "f_2_0_0";
    fs::rename(path1, path2, rename_error);
    ASSERT_TRUE(!rename_error) << rename_error;

    // let them catch up
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    auto f = model.makeModelSubfolder("new");
    f->addkid(model.removenode("f/f_2")); // / f_2_0 / f_2_0_0"));
    model.findnode("f")->addkid(move(f));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}

GTEST_TEST(Sync, DISABLED_BasicSync_MoveSeveralExistingIntoDeepNewLocalFolders)
{
    // historic case:  in the local filesystem, create a new folder then move an existing file/folder into it
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // make new folder tree in the local filesystem
    ASSERT_TRUE(buildLocalFolders(clientA1.syncSet[1].localpath, "new", 3, 3, 3));

    // move already synced folders to serveral parts of it - one under another moved folder too
    error_code rename_error;
    fs::rename(clientA1.syncSet[1].localpath / "f_0", clientA1.syncSet[1].localpath / "new" / "new_0" / "new_0_1" / "new_0_1_2" / "f_0", rename_error);
    ASSERT_TRUE(!rename_error) << rename_error;
    fs::rename(clientA1.syncSet[1].localpath / "f_1", clientA1.syncSet[1].localpath / "new" / "new_1" / "new_1_2" / "f_1", rename_error);
    ASSERT_TRUE(!rename_error) << rename_error;
    fs::rename(clientA1.syncSet[1].localpath / "f_2", clientA1.syncSet[1].localpath / "new" / "new_1" / "new_1_2" / "f_1" / "f_1_2" / "f_2", rename_error);
    ASSERT_TRUE(!rename_error) << rename_error;

    // let them catch up
    waitonsyncs(std::chrono::seconds(30), &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    model.findnode("f")->addkid(model.buildModelSubdirs("new", 3, 3, 3));
    model.findnode("f/new/new_0/new_0_1/new_0_1_2")->addkid(model.removenode("f/f_0"));
    model.findnode("f/new/new_1/new_1_2")->addkid(model.removenode("f/f_1"));
    model.findnode("f/new/new_1/new_1_2/f_1/f_1_2")->addkid(model.removenode("f/f_2"));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    model.ensureLocalDebrisTmpLock("f"); // since we downloaded files
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}

/* not expected to work yet
GTEST_TEST(Sync, BasicSync_SyncDuplicateNames)
{
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);


    NewNode* nodearray = new NewNode[3];
    nodearray[0] = *clientA1.makeSubfolder("samename");
    nodearray[1] = *clientA1.makeSubfolder("samename");
    nodearray[2] = *clientA1.makeSubfolder("Samename");
    clientA1.resultproc.prepresult(StandardClient::PUTNODES, [this](error e) {
    });
    clientA1.client.putnodes(clientA1.basefolderhandle, nodearray, 3);

    // set up syncs, they should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "", 2));
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    Model model;
    model.root->addkid(model.makeModelSubfolder("samename"));
    model.root->addkid(model.makeModelSubfolder("samename"));
    model.root->addkid(model.makeModelSubfolder("Samename"));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.root.get(), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.root.get(), 2));
}*/

GTEST_TEST(Sync, BasicSync_RemoveLocalNodeBeforeSessionResume)
{
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    auto pclientA1 = ::mega::make_unique<StandardClient>(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(pclientA1->login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(pclientA1->basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(pclientA1->setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));
    waitonsyncs(std::chrono::seconds(4), pclientA1.get(), &clientA2);
    pclientA1->logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(pclientA1->confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // save session
    ::mega::byte session[64];
    int sessionsize = pclientA1->client.dumpsession(session, sizeof session);

    // logout (but keep caches)
    fs::path sync1path = pclientA1->syncSet[1].localpath;
    pclientA1->localLogout();

    // remove local folders
    error_code e;
    ASSERT_TRUE(fs::remove_all(sync1path / "f_2", e) != static_cast<std::uintmax_t>(-1)) << e;

    // resume session, see if nodes and localnodes get in sync
    pclientA1.reset(new StandardClient(localtestroot, "clientA1"));
    ASSERT_TRUE(pclientA1->login_fetchnodes_resumesync(string((char*)session, sessionsize), sync1path.u8string(), "f", 1));

    waitonsyncs(std::chrono::seconds(4), pclientA1.get(), &clientA2);

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(model.movetosynctrash("f/f_2", "f"));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
    ASSERT_TRUE(model.removesynctrash("f"));
    ASSERT_TRUE(pclientA1->confirmModel_mainthread(model.findnode("f"), 1));
}

/* not expected to work yet
GTEST_TEST(Sync, BasicSync_RemoteFolderCreationRaceSamename)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    // SN tagging needed for this one
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    // set up sync for both, it should build matching local folders (empty initially)
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "", 2));
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // now have both clients create the same remote folder structure simultaneously.  We should end up with just one copy of it on the server and in both syncs
    future<bool> p1 = clientA1.thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.makeCloudSubdirs("f", 3, 3, pb); });
    future<bool> p2 = clientA2.thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.makeCloudSubdirs("f", 3, 3, pb); });
    ASSERT_TRUE(waitonresults(&p1, &p2));

    // let them catch up
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.root.get(), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.root.get(), 2));
}*/

/* not expected to work yet
GTEST_TEST(Sync, BasicSync_LocalFolderCreationRaceSamename)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    // SN tagging needed for this one
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    // set up sync for both, it should build matching local folders (empty initially)
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "", 2));
    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // now have both clients create the same folder structure simultaneously.  We should end up with just one copy of it on the server and in both syncs
    future<bool> p1 = clientA1.thread_do([=](StandardClient& sc, promise<bool>& pb) { buildLocalFolders(sc.syncSet[1].localpath, "f", 3, 3, 0); pb.set_value(true); });
    future<bool> p2 = clientA2.thread_do([=](StandardClient& sc, promise<bool>& pb) { buildLocalFolders(sc.syncSet[2].localpath, "f", 3, 3, 0); pb.set_value(true); });
    ASSERT_TRUE(waitonresults(&p1, &p2));

    // let them catch up
    waitonsyncs(std::chrono::seconds(30), &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.root.get(), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.root.get(), 2));
}*/


GTEST_TEST(Sync, BasicSync_ResumeSyncFromSessionAfterNonclashingLocalAndRemoteChanges )
{
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    unique_ptr<StandardClient> pclientA1(new StandardClient(localtestroot, "clientA1"));   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(pclientA1->login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(pclientA1->basefolderhandle, clientA2.basefolderhandle);

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(pclientA1->setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));
    waitonsyncs(std::chrono::seconds(4), pclientA1.get(), &clientA2);
    pclientA1->logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    Model model1, model2;
    model1.root->addkid(model1.buildModelSubdirs("f", 3, 3, 0));
    model2.root->addkid(model2.buildModelSubdirs("f", 3, 3, 0));
    ASSERT_TRUE(pclientA1->confirmModel_mainthread(model1.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model2.findnode("f"), 2));

    cout << "********************* save session A1" << endl;
    ::mega::byte session[64];
    int sessionsize = pclientA1->client.dumpsession(session, sizeof session);

    cout << "*********************  logout A1 (but keep caches on disk)" << endl;
    fs::path sync1path = pclientA1->syncSet[1].localpath;
    pclientA1->localLogout();

    cout << "*********************  add remote folders via A2" << endl;
    future<bool> p1 = clientA2.thread_do([](StandardClient& sc, promise<bool>& pb) { sc.makeCloudSubdirs("newremote", 2, 2, pb, "f/f_1/f_1_0"); });
    model1.findnode("f/f_1/f_1_0")->addkid(model1.buildModelSubdirs("newremote", 2, 2, 0));
    model2.findnode("f/f_1/f_1_0")->addkid(model2.buildModelSubdirs("newremote", 2, 2, 0));
    ASSERT_TRUE(waitonresults(&p1));

    cout << "*********************  remove remote folders via A2" << endl;
    p1 = clientA2.thread_do([](StandardClient& sc, promise<bool>& pb) { sc.deleteremote("f/f_0", pb); });
    model1.movetosynctrash("f/f_0", "f");
    model2.movetosynctrash("f/f_0", "f");
    ASSERT_TRUE(waitonresults(&p1));

    cout << "*********************  add local folders in A1" << endl;
    ASSERT_TRUE(buildLocalFolders(sync1path / "f_1/f_1_2", "newlocal", 2, 2, 2));
    model1.findnode("f/f_1/f_1_2")->addkid(model1.buildModelSubdirs("newlocal", 2, 2, 2));
    model2.findnode("f/f_1/f_1_2")->addkid(model2.buildModelSubdirs("newlocal", 2, 2, 2));

    cout << "*********************  remove local folders in A1" << endl;
    error_code e;
    ASSERT_TRUE(fs::remove_all(sync1path / "f_2", e) != static_cast<std::uintmax_t>(-1)) << e;
    model1.removenode("f/f_2");
    model2.movetosynctrash("f/f_2", "f");

    cout << "*********************  get sync2 activity out of the way" << endl;
    waitonsyncs(DEFAULTWAIT, &clientA2);

    cout << "*********************  resume A1 session (with sync), see if A2 nodes and localnodes get in sync again" << endl;
    pclientA1.reset(new StandardClient(localtestroot, "clientA1"));
    ASSERT_TRUE(pclientA1->login_fetchnodes_resumesync(string((char*)session, sessionsize), sync1path.u8string(), "f", 1));
    ASSERT_EQ(pclientA1->basefolderhandle, clientA2.basefolderhandle);
    waitonsyncs(DEFAULTWAIT, pclientA1.get(), &clientA2);

    cout << "*********************  check everything matches (model has expected state of remote and local)" << endl;
    ASSERT_TRUE(pclientA1->confirmModel_mainthread(model1.findnode("f"), 1));
    model2.ensureLocalDebrisTmpLock("f"); // since we downloaded files
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model2.findnode("f"), 2));
}

GTEST_TEST(Sync, BasicSync_ResumeSyncFromSessionAfterClashingLocalAddRemoteDelete)
{
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    unique_ptr<StandardClient> pclientA1(new StandardClient(localtestroot, "clientA1"));   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(pclientA1->login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(pclientA1->basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(pclientA1->setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));
    waitonsyncs(std::chrono::seconds(4), pclientA1.get(), &clientA2);
    pclientA1->logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(pclientA1->confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // save session A1
    ::mega::byte session[64];
    int sessionsize = pclientA1->client.dumpsession(session, sizeof session);
    fs::path sync1path = pclientA1->syncSet[1].localpath;

    // logout A1 (but keep caches on disk)
    pclientA1->localLogout();

    // remove remote folder via A2
    future<bool> p1 = clientA2.thread_do([](StandardClient& sc, promise<bool>& pb) { sc.deleteremote("f/f_1", pb); });
    ASSERT_TRUE(waitonresults(&p1));

    // add local folders in A1 on disk folder
    ASSERT_TRUE(buildLocalFolders(sync1path / "f_1/f_1_2", "newlocal", 2, 2, 2));

    // get sync2 activity out of the way
    waitonsyncs(std::chrono::seconds(4), &clientA2);

    // resume A1 session (with sync), see if A2 nodes and localnodes get in sync again
    pclientA1.reset(new StandardClient(localtestroot, "clientA1"));
    ASSERT_TRUE(pclientA1->login_fetchnodes_resumesync(string((char*)session, sessionsize), sync1path.u8string(), "f", 1));
    ASSERT_EQ(pclientA1->basefolderhandle, clientA2.basefolderhandle);
    waitonsyncs(std::chrono::seconds(4), pclientA1.get(), &clientA2);

    // check everything matches (model has expected state of remote and local)
    model.findnode("f/f_1/f_1_2")->addkid(model.buildModelSubdirs("newlocal", 2, 2, 2));
    ASSERT_TRUE(model.movetosynctrash("f/f_1", "f"));
    ASSERT_TRUE(pclientA1->confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(model.removesynctrash("f", "f_1/f_1_2/newlocal"));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}


GTEST_TEST(Sync, CmdChecks_RRAttributeAfterMoveNode)
{
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    unique_ptr<StandardClient> pclientA1(new StandardClient(localtestroot, "clientA1"));   // user 1 client 1

    ASSERT_TRUE(pclientA1->login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 3, 3));

    Node* f = pclientA1->drillchildnodebyname(pclientA1->gettestbasenode(), "f");
    handle original_f_handle = f->nodehandle;
    handle original_f_parent_handle = f->parent->nodehandle;

    // make sure there are no 'f' in the rubbish
    auto fv = pclientA1->drillchildnodesbyname(pclientA1->getcloudrubbishnode(), "f");
    future<bool> fb = pclientA1->thread_do([&fv](StandardClient& sc, promise<bool>& pb) { sc.deleteremotenodes(fv, pb); });
    ASSERT_TRUE(waitonresults(&fb));

    f = pclientA1->drillchildnodebyname(pclientA1->getcloudrubbishnode(), "f");
    ASSERT_TRUE(f == nullptr);


    // remove remote folder via A2
    future<bool> p1 = pclientA1->thread_do([](StandardClient& sc, promise<bool>& pb)
        {
            sc.movenodetotrash("f", pb);
        });
    ASSERT_TRUE(waitonresults(&p1));

    WaitMillisec(3000);  // allow for attribute delivery too

    f = pclientA1->drillchildnodebyname(pclientA1->getcloudrubbishnode(), "f");
    ASSERT_TRUE(f != nullptr);

    // check the restore-from-trash handle got set, and correctly
    nameid rrname = AttrMap::string2nameid("rr");
    ASSERT_EQ(f->nodehandle, original_f_handle);
    ASSERT_EQ(f->attrs.map[rrname], string(Base64Str<MegaClient::NODEHANDLE>(original_f_parent_handle)));
    ASSERT_EQ(f->attrs.map[rrname], string(Base64Str<MegaClient::NODEHANDLE>(pclientA1->gettestbasenode()->nodehandle)));

    // move it back

    p1 = pclientA1->thread_do([&](StandardClient& sc, promise<bool>& pb)
    {
        sc.movenode(f->nodehandle, pclientA1->basefolderhandle, pb);
    });
    ASSERT_TRUE(waitonresults(&p1));

    WaitMillisec(3000);  // allow for attribute delivery too

    // check it's back and the rr attribute is gone
    f = pclientA1->drillchildnodebyname(pclientA1->gettestbasenode(), "f");
    ASSERT_TRUE(f != nullptr);
    ASSERT_EQ(f->attrs.map[rrname], string());
}


#ifdef __linux__
GTEST_TEST(Sync, BasicSync_SpecialCreateFile)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 2, 2));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 2, 2, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));

    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;
    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // make new folders (and files) in the local filesystem and see if we catch up in A1 and A2 (adder and observer syncs)
    ASSERT_TRUE(createSpecialFiles(clientA1.syncSet[1].localpath / "f_0", "newkid", 2));

    for (int i = 0; i < 2; ++i)
    {
        string filename = "file" + to_string(i) + "_" + "newkid";
        model.findnode("f/f_0")->addkid(model.makeModelSubfile(filename));
    }

    // let them catch up
    waitonsyncs(DEFAULTWAIT, &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    model.ensureLocalDebrisTmpLock("f"); // since we downloaded files
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}
#endif

GTEST_TEST(Sync, DISABLED_BasicSync_moveAndDeleteLocalFile)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 1, 1));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 1, 1, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));

    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;
    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));


    // move something in the local filesystem and see if we catch up in A1 and A2 (deleter and observer syncs)
    error_code rename_error;
    fs::rename(clientA1.syncSet[1].localpath / "f_0", clientA1.syncSet[1].localpath / "renamed", rename_error);
    ASSERT_TRUE(!rename_error) << rename_error;
    fs::remove(clientA1.syncSet[1].localpath / "renamed");

    // let them catch up
    waitonsyncs(DEFAULTWAIT, &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(model.movetosynctrash("f/f_0", "f"));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
    ASSERT_TRUE(model.removesynctrash("f"));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
}

namespace {

string makefa(const string& name, int fakecrc, int mtime)
{
    AttrMap attrs;
    attrs.map['n'] = name;

    FileFingerprint ff;
    ff.crc[0] = ff.crc[1] = ff.crc[2] = ff.crc[3] = fakecrc;
    ff.mtime = mtime;
    ff.serializefingerprint(&attrs.map['c']);

    string attrjson;
    attrs.getjson(&attrjson);
    return attrjson;
}

Node* makenode(MegaClient& mc, handle parent, ::mega::nodetype_t type, m_off_t size, handle owner, const string& attrs, ::mega::byte* key)
{
    static handle handlegenerator = 10;
    std::vector<Node*> dp;
    auto newnode = new Node(&mc, &dp, ++handlegenerator, parent, type, size, owner, nullptr, 1);
    
    newnode->setkey(key);
    newnode->attrstring.reset(new string);

    SymmCipher sc;
    sc.setkey(key, type);
    mc.makeattr(&sc, newnode->attrstring, attrs.c_str());

    int attrlen = int(newnode->attrstring->size());
    string base64attrstring;
    base64attrstring.resize(static_cast<size_t>(attrlen * 4 / 3 + 4));
    base64attrstring.resize(static_cast<size_t>(Base64::btoa((::mega::byte *)newnode->attrstring->data(), int(newnode->attrstring->size()), (char *)base64attrstring.data())));

    *newnode->attrstring = base64attrstring;

    return newnode;
}

} // anonymous

GTEST_TEST(Sync, NodeSorting_forPhotosAndVideos)
{
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient standardclient(localtestroot, "sortOrderTests");
    auto& client = standardclient.client;

    handle owner = 99999;

    ::mega::byte key[] = { 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04 };

    // first 3 are root nodes:
    auto cloudroot = makenode(client, UNDEF, ROOTNODE, -1, owner, makefa("root", 1, 1), key);
    makenode(client, UNDEF, INCOMINGNODE, -1, owner, makefa("inbox", 1, 1), key);
    makenode(client, UNDEF, RUBBISHNODE, -1, owner, makefa("bin", 1, 1), key);

    // now some files to sort
    auto photo1 = makenode(client, cloudroot->nodehandle, FILENODE, 9999, owner, makefa("abc.jpg", 1, 1570673890), key);
    auto photo2 = makenode(client, cloudroot->nodehandle, FILENODE, 9999, owner, makefa("cba.png", 1, 1570673891), key);
    auto video1 = makenode(client, cloudroot->nodehandle, FILENODE, 9999, owner, makefa("xyz.mov", 1, 1570673892), key);
    auto video2 = makenode(client, cloudroot->nodehandle, FILENODE, 9999, owner, makefa("zyx.mp4", 1, 1570673893), key);
    auto otherfile = makenode(client, cloudroot->nodehandle, FILENODE, 9999, owner, makefa("ASDF.fsda", 1, 1570673894), key);
    auto otherfolder = makenode(client, cloudroot->nodehandle, FOLDERNODE, -1, owner, makefa("myfolder", 1, 1570673895), key);

    node_vector v{ photo1, photo2, video1, video2, otherfolder, otherfile };
    for (auto n : v) n->setkey(key);

    MegaApiImpl::sortByComparatorFunction(v, MegaApi::ORDER_PHOTO_ASC, client);
    node_vector v2{ photo1, photo2, video1, video2, otherfolder, otherfile };
    ASSERT_EQ(v, v2);

    MegaApiImpl::sortByComparatorFunction(v, MegaApi::ORDER_PHOTO_DESC, client);
    node_vector v3{ photo2, photo1, video2, video1, otherfolder, otherfile };
    ASSERT_EQ(v, v3);

    MegaApiImpl::sortByComparatorFunction(v, MegaApi::ORDER_VIDEO_ASC, client);
    node_vector v4{ video1, video2, photo1, photo2, otherfolder, otherfile };
    ASSERT_EQ(v, v4);

    MegaApiImpl::sortByComparatorFunction(v, MegaApi::ORDER_VIDEO_DESC, client);
    node_vector v5{ video2, video1, photo2, photo1, otherfolder, otherfile };
    ASSERT_EQ(v, v5);
}


GTEST_TEST(Sync, PutnodesForMultipleFolders)
{
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient standardclient(localtestroot, "PutnodesForMultipleFolders");
    ASSERT_TRUE(standardclient.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD", true));

    NewNode* newnodes = new NewNode[4];
    
    standardclient.client.putnodes_prepareOneFolder(&newnodes[0], "folder1");
    standardclient.client.putnodes_prepareOneFolder(&newnodes[1], "folder2");
    standardclient.client.putnodes_prepareOneFolder(&newnodes[2], "folder2.1");
    standardclient.client.putnodes_prepareOneFolder(&newnodes[3], "folder2.2");

    newnodes[1].nodehandle = newnodes[2].parenthandle = newnodes[3].parenthandle = 2;

    handle targethandle = standardclient.client.rootnodes[0];

    std::atomic<bool> putnodesDone{false};
    standardclient.resultproc.prepresult(StandardClient::PUTNODES, [&putnodesDone](error e) {
        putnodesDone = true;
    });

    standardclient.client.putnodes(targethandle, newnodes, 4, nullptr);
    
    while (!putnodesDone)
    {
        WaitMillisec(100);
    }

    Node* cloudRoot = standardclient.client.nodebyhandle(targethandle);

    ASSERT_TRUE(nullptr != standardclient.drillchildnodebyname(cloudRoot, "folder1"));
    ASSERT_TRUE(nullptr != standardclient.drillchildnodebyname(cloudRoot, "folder2"));
    ASSERT_TRUE(nullptr != standardclient.drillchildnodebyname(cloudRoot, "folder2/folder2.1"));
    ASSERT_TRUE(nullptr != standardclient.drillchildnodebyname(cloudRoot, "folder2/folder2.2"));
}


#ifndef _WIN32
GTEST_TEST(Sync, BasicSync_CreateAndDeleteLink)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 1, 1));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 1, 1, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));

    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;
    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));


    // move something in the local filesystem and see if we catch up in A1 and A2 (deleter and observer syncs)
    error_code linkage_error;
    fs::create_symlink(clientA1.syncSet[1].localpath / "f_0", clientA1.syncSet[1].localpath / "linked", linkage_error);
    ASSERT_TRUE(!linkage_error) << linkage_error;

    // let them catch up
    waitonsyncs(DEFAULTWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));


    fs::remove(clientA1.syncSet[1].localpath / "linked");
    // let them catch up
    waitonsyncs(DEFAULTWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}

GTEST_TEST(Sync, BasicSync_CreateRenameAndDeleteLink)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 1, 1));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 1, 1, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));

    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;
    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));


    // move something in the local filesystem and see if we catch up in A1 and A2 (deleter and observer syncs)
    error_code linkage_error;
    fs::create_symlink(clientA1.syncSet[1].localpath / "f_0", clientA1.syncSet[1].localpath / "linked", linkage_error);
    ASSERT_TRUE(!linkage_error) << linkage_error;

    // let them catch up
    waitonsyncs(DEFAULTWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    fs::rename(clientA1.syncSet[1].localpath / "linked", clientA1.syncSet[1].localpath / "linkrenamed", linkage_error);

    // let them catch up
    waitonsyncs(DEFAULTWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    fs::remove(clientA1.syncSet[1].localpath / "linkrenamed");

    // let them catch up
    waitonsyncs(DEFAULTWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}

GTEST_TEST(Sync, BasicSync_CreateAndReplaceLinkLocally)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 1, 1));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 1, 1, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));

    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;
    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));


    // move something in the local filesystem and see if we catch up in A1 and A2 (deleter and observer syncs)
    error_code linkage_error;
    fs::create_symlink(clientA1.syncSet[1].localpath / "f_0", clientA1.syncSet[1].localpath / "linked", linkage_error);
    ASSERT_TRUE(!linkage_error) << linkage_error;

    // let them catch up
    waitonsyncs(DEFAULTWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
    fs::rename(clientA1.syncSet[1].localpath / "f_0", clientA1.syncSet[1].localpath / "linked", linkage_error);

    // let them catch up
    waitonsyncs(DEFAULTWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    fs::remove(clientA1.syncSet[1].localpath / "linked");
    ASSERT_TRUE(createFile(clientA1.syncSet[1].localpath, "linked"));

    // let them catch up
    waitonsyncs(DEFAULTWAIT, &clientA1, &clientA2);

    model.findnode("f")->addkid(model.makeModelSubfile("linked"));
    model.ensureLocalDebrisTmpLock("f"); // since we downloaded files

    //check client 2 is as expected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}


GTEST_TEST(Sync, BasicSync_CreateAndReplaceLinkUponSyncDown)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f", 1, 1));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(clientA1.basefolderhandle, clientA2.basefolderhandle);

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 1, 1, 0));

    // set up sync for A1, it should build matching local folders
    ASSERT_TRUE(clientA1.setupSync_mainthread("sync1", "f", 1));
    ASSERT_TRUE(clientA2.setupSync_mainthread("sync2", "f", 2));

    waitonsyncs(std::chrono::seconds(4), &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;
    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // move something in the local filesystem and see if we catch up in A1 and A2 (deleter and observer syncs)
    error_code linkage_error;
    fs::create_symlink(clientA1.syncSet[1].localpath / "f_0", clientA1.syncSet[1].localpath / "linked", linkage_error);
    ASSERT_TRUE(!linkage_error) << linkage_error;

    // let them catch up
    waitonsyncs(DEFAULTWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    ASSERT_TRUE(createFile(clientA2.syncSet[2].localpath, "linked"));

    // let them catch up
    waitonsyncs(DEFAULTWAIT, &clientA1, &clientA2);

    model.findnode("f")->addkid(model.makeModelSubfolder("linked")); //notice: the deleted here is folder because what's actually deleted is a symlink that points to a folder
                                                                     //ideally we could add full support for symlinks in this tests suite

    model.movetosynctrash("f/linked","f");
    model.findnode("f")->addkid(model.makeModelSubfile("linked"));
    model.ensureLocalDebrisTmpLock("f"); // since we downloaded files

    //check client 2 is as expected
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
}

#endif

#endif
