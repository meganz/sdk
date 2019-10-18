/**
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
        string content;
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

    unique_ptr<ModelNode> makeModelSubfile(const string& utf8Name, string content = {})
    {
        unique_ptr<ModelNode> n(new ModelNode);
        n->name = utf8Name;
        n->type = ModelNode::file;
        n->content = content.empty() ? utf8Name : std::move(content);
        return n;
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
            return std::move(extracted);
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
        ModelNode* syncroot;
        if (syncroot = findnode(syncrootpath))
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
        , client(this, &waiter, new HTTPIO_CLASS, new FSACCESS_CLASS,
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
            "N9tSBJDC", "synctests")
        , clientname(name)
        , fsBasePath(basepath / fs::u8path(name))
        , clientthread([this]() { threadloop(); })
    {
        client.clientname = clientname + " ";
    }

    ~StandardClient()
    {
        // shut down any syncs on the same thread, or they stall the client destruction (CancelIo instead of CancelIoEx on the WinDirNotify)
        thread_do([](MegaClient& mc, promise<bool>&) { 
            #ifdef _WIN32
                // logout stalls in windows due to the issue above
                mc.purgenodesusersabortsc(); 
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
                mc.purgenodesusersabortsc();
            #else
                mc.locallogout();
            #endif
        });
    }

    static mutex om;
    bool logcb = false;
    chrono::steady_clock::time_point lastcb = std::chrono::steady_clock::now();
    string lp(LocalNode* ln) { string lp;  ln->getlocalpath(&lp); client.fsaccess->local2name(&lp); return lp; }
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
        while (!functionDone.wait_until(guard, chrono::steady_clock::now() + 600s, [this]() { return !nextfunctionMC; }))
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
        while (!functionDone.wait_until(guard, chrono::steady_clock::now() + 600s, [this]() { return !nextfunctionSC; }))
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

        resultproc.prepresult(PRELOGIN, [this, &pb](error e) { pb.set_value(!e); });
        client.prelogin(user.c_str());
    }

    void loginFromEnv(const string& userenv, const string& pwdenv, promise<bool>& pb)
    {
        string user = getenv(userenv.c_str());
        string pwd = getenv(pwdenv.c_str());

        ASSERT_FALSE(user.empty());
        ASSERT_FALSE(pwd.empty());

        byte pwkey[SymmCipher::KEYLENGTH];

        resultproc.prepresult(LOGIN, [this, &pb](error e) { pb.set_value(!e); });
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
        resultproc.prepresult(LOGIN, [this, &pb](error e) { pb.set_value(!e); });
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

    NewNode* makeSubfolder(const string& utf8Name)
    {
        SymmCipher key;
        string attrstring;
        byte buf[FOLDERNODEKEYLENGTH];
        NewNode* newnode = new NewNode[1];

        // set up new node as folder node
        newnode->source = NEW_NODE;
        newnode->type = FOLDERNODE;
        newnode->nodehandle = 0;
        newnode->parenthandle = UNDEF;

        // generate fresh random key for this folder node
        PrnGen prngen;
        prngen.genblock(buf, FOLDERNODEKEYLENGTH);
        newnode->nodekey.assign((char*)buf, FOLDERNODEKEYLENGTH);
        key.setkey(buf);

        // generate fresh attribute object with the folder name
        AttrMap attrs;
        string& n = attrs.map['n'];
        client.fsaccess->normalize(&(n = utf8Name));

        // JSON-encode object and encrypt attribute string
        attrs.getjson(&attrstring);
        newnode->attrstring = new string;
        client.makeattr(&key, newnode->attrstring, attrstring.c_str());

        return newnode;
    }
     

    struct ResultProc
    {
        struct id_callback
        {
            handle h = UNDEF;
            std::function<void(error)> f;
            id_callback(std::function<void(error)> cf, handle ch = UNDEF) : f(cf), h(ch) {}
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
                client.putnodes(root->nodehandle, makeSubfolder("mega_test_sync"), 1);
                return;
            }
        }
        pb.set_value(false);
    }

    NewNode* buildSubdirs(vector<NewNode*>& nodes, const string& prefix, int n, int recurselevel)
    {
        NewNode* nn = makeSubfolder(prefix);
        nodes.push_back(nn);
        nn->nodehandle = nodes.size();

        if (recurselevel > 0)
        {
            for (int i = 0; i < n; ++i)
            {
                buildSubdirs(nodes, prefix + "_" + to_string(i), n, recurselevel - 1)->parenthandle = nn->nodehandle;
            }
        }

        return nn;
    }

    void makeCloudSubdirs(const string& prefix, int depth, int fanout, promise<bool>& pb, const string& atpath = "")
    {
        assert(basefolderhandle != UNDEF);

        vector<NewNode*> nodes;
        NewNode* nn = buildSubdirs(nodes, prefix, fanout, depth);
        nn->parenthandle = UNDEF;
        nn->ovhandle = UNDEF;

        NewNode* nodearray = new NewNode[nodes.size()];
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            nodearray[i] = *nodes[i]; // todo:  need move semantics
            //delete nodes[i];
        }

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
            resultproc.prepresult(PUTNODES, [this, &pb](error e) {
                pb.set_value(!e);
                if (e) 
                {
                    cout << "putnodes result: " << e << endl;
                }
            });
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
            for (int i = subnodes.size(); i--; )
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

    bool setupSync_inthread(SyncDescriptor desc, int syncid, const string& subfoldername, const fs::path& localpath)
    {
        if (Node* n = client.nodebyhandle(basefolderhandle))
        {
            if (Node* m = drillchildnodebyname(n, subfoldername))
            {
                string local, orig = localpath.u8string();
                client.fsaccess->path2local(&orig, &local);
                error e = client.addsync(desc, &local, DEBRISFOLDER, NULL, m, 0, syncid);  // use syncid as tag
                if (!e)
                {
                    syncSet[syncid] = SyncInfo{ m->nodehandle, localpath };
                    return true;
                }
            }
        }
        return false;
    }

    bool delSync_inthread(const int syncId, const bool keepCache)
    {
        const auto handle = syncSet.at(syncId).h;
        const auto node = client.nodebyhandle(handle);
        EXPECT_TRUE(node);
        client.delsync(node->localnode->sync, keepCache);
        return true;
    }

    bool recursiveConfirm(Model::ModelNode* mn, Node* n, int& descendants, const string& identifier, int depth)
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
                if (recursiveConfirm(m_iter->second, i->second, rdescendants, identifier, depth+1))
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
        else
        {
            cout << identifier << " after matching " << matched << " child nodes [";
            for (auto& ml : matchedlist) cout << ml << " ";
            cout << "](with " << descendants << " descendants) in " << mn->path() << ", ended up with unmatched model nodes:";
            for (auto& m : ms) cout << " " << m.first;
            cout << " and unmatched remote nodes:";
            for (auto& i : ns) cout << " " << i.first;
            cout << endl;
            return false;
        };
    }

    bool recursiveConfirm(Model::ModelNode* mn, LocalNode* n, int& descendants, const string& identifier, int depth)
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
        client.fsaccess->local2name(&localpath);
        string n_localname = n->localname;
        client.fsaccess->local2name(&n_localname);
        if (n_localname.size())
        {
            EXPECT_EQ(n->name, n_localname);
        }
        if (n->sync->isUpSync() && n->sync->isDownSync())
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
            client.fsaccess->local2name(&parentpath);
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
            if (m->parent && m->parent->type == Model::ModelNode::file)
            {
                // skip previous versions
                assert(m->type == Model::ModelNode::file);
                continue;
            }
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
                if (recursiveConfirm(m_iter->second, i->second, rdescendants, identifier, depth+1))
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
        else
        {
            cout << identifier << " after matching " << matched << " child nodes [";
            for (auto& ml : matchedlist) cout << ml << " ";
            cout << "](with " << descendants << " descendants) in " << mn->path() << ", ended up with unmatched model nodes:";
            for (auto& m : ms) cout << " " << m.first;
            cout << " and unmatched LocalNodes:";
            for (auto& i : ns) cout << " " << i.first;
            cout << endl;
            return false;
        };
    }


    bool recursiveConfirm(Model::ModelNode* mn, fs::path p, int& descendants, const string& identifier, int depth)
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
            ifstream fs(p, ios::binary);
            char filedata[1024];
            fs.read(filedata, sizeof(filedata));
            EXPECT_EQ(fs.gcount(), mn->content.size()) << " file is not expected size " << p;
            EXPECT_TRUE(!memcmp(filedata, mn->content.data(), mn->content.size())) << " file data mismatch " << p;
        }

        if (pathtype != FOLDERNODE)
        {
            return true;
        }

        multimap<string, Model::ModelNode*> ms;
        multimap<string, fs::path> ps;
        for (auto& m : mn->kids) ms.emplace(m->name, m.get());
        for (fs::directory_iterator pi(p); pi != fs::directory_iterator(); ++pi) ps.emplace(pi->path().filename().u8string(), pi->path());

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
                if (recursiveConfirm(m_iter->second, i->second, rdescendants, identifier, depth+1))
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
        else
        {
            cout << clientname << " " << identifier << " after matching " << matched << " child nodes [";
            for (auto& ml : matchedlist) cout << ml << " ";
            cout << "](with " << descendants << " descendants) in " << mn->path() << ", ended up with unmatched model nodes:";
            for (auto& m : ms) cout << " " << m.first;
            cout << " and unmatched filesystem paths:";
            for (auto& i : ps) cout << " " << i.second;
            cout << endl;
            return false;
        };
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
        CONFIRM_LOCAL = 0x01,
        CONFIRM_REMOTE = 0x02,
        CONFIRM_ALL = CONFIRM_LOCAL | CONFIRM_REMOTE,
    };

    bool confirmModel(int syncid, Model::ModelNode* mnode, const Confirm confirm)
    {
        auto si = syncSet.find(syncid);
        if (si == syncSet.end())
        {
            cout << clientname << " syncid " << syncid << " not found " << endl;
            return false;
        }

        // compare model aganst nodes representing remote state
        int descendants = 0;
        if (confirm & CONFIRM_REMOTE && !recursiveConfirm(mnode, client.nodebyhandle(si->second.h), descendants, "Sync " + to_string(syncid), 0))
        {
            cout << clientname <<  " syncid " << syncid << " comparison against remote nodes failed" << endl;
            return false;
        }

        // compare model against LocalNodes
        descendants = 0; 
        if (Sync* sync = syncByTag(syncid))
        {
            if (confirm & CONFIRM_LOCAL && !recursiveConfirm(mnode, &sync->localroot, descendants, "Sync " + to_string(syncid), 0))
            {
                cout << clientname << " syncid " << syncid << " comparison against LocalNodes failed" << endl;
                return false;
            }
        }

        // compare model against local filesystem
        descendants = 0;
        if (confirm & CONFIRM_LOCAL && !recursiveConfirm(mnode, si->second.localpath, descendants, "Sync " + to_string(syncid), 0))
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

    void fetchnodes_result(error e) override
    {
        cout << clientname << " Fetchnodes: " << e << endl;
        resultproc.processresult(FETCHNODES, e);
    }

    void unlink_result(handle, error e) 
    { 
        resultproc.processresult(UNLINK, e);
    }

    void putnodes_result(error e, targettype_t tt, NewNode* nn) override
    {
        if (nn)  // ignore sync based putnodes
        {
            resultproc.processresult(PUTNODES, e);
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
            resultproc.prepresult(UNLINK, [this, &pb](error e) { pb.set_value(!e); });
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
            for (int i = ns.size(); i--; )
            {
                resultproc.prepresult(UNLINK, [this, &pb, i](error e) { if (!i) pb.set_value(!e); });
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
            resultproc.prepresult(MOVENODE, [this, &pb](error e) { pb.set_value(!e); }, n->nodehandle);
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
            resultproc.prepresult(MOVENODE, [this, &pb](error e) { pb.set_value(!e); }, n->nodehandle);
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
            resultproc.prepresult(MOVENODE, [this, &pb](error e) { pb.set_value(!e); }, n->nodehandle);
            client.rename(n, p, SYNCDEL_NONE, n->parent->nodehandle);
            return;
        }
        cout << "node or rubbish or node parent not found" << endl;
        pb.set_value(false);
    }



    void waitonsyncs(chrono::seconds d = 2s)
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

    bool login_reset_makeremotenodes(const string& user, const string& pw, const string& prefix, int depth = 0, int fanout = 0)
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

    bool login_fetchnodes(const string& user, const string& pw)
    {
        future<bool> p2;
        p2 = thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.preloginFromEnv(user, pb); });
        if (!waitonresults(&p2)) return false;
        p2 = thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.loginFromEnv(user, pw, pb); });
        if (!waitonresults(&p2)) return false;
        p2 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.fetchnodes(pb); });
        if (!waitonresults(&p2)) return false;
        p2 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.ensureTestBaseFolder(false, pb); });
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
        return login_fetchnodes_resumesync(SyncDescriptor{}, session, localsyncpath, remotesyncrootfolder, syncid);
    }

    bool login_fetchnodes_resumesync(SyncDescriptor desc, const string& session, const string& localsyncpath, const std::string& remotesyncrootfolder, int syncid)
    {
        future<bool> p2;
        p2 = thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.loginFromSession(session, pb); });
        if (!waitonresults(&p2)) return false;
        
        assert(!onFetchNodes);
        onFetchNodes = [=](StandardClient& mc, promise<bool>& pb)
        {
            promise<bool> tp;
            mc.ensureTestBaseFolder(false, tp);
            pb.set_value(tp.get_future().get() ? mc.setupSync_inthread(desc, syncid, remotesyncrootfolder, localsyncpath) : false);
        };

        p2 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.fetchnodes(pb); });
        if (!waitonresults(&p2)) return false;
        p2 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.ensureTestBaseFolder(false, pb); });
        if (!waitonresults(&p2)) return false;
        return true;
    }

    bool setupSync_mainthread(const std::string& localsyncrootfolder, const std::string& remotesyncrootfolder, int syncid)
    {
        return setupSync_mainthread(SyncDescriptor{}, localsyncrootfolder, remotesyncrootfolder, syncid);
    }

    bool setupSync_mainthread(SyncDescriptor desc, const std::string& localsyncrootfolder, const std::string& remotesyncrootfolder, int syncid)
    {
        fs::path syncdir = fsBasePath / fs::u8path(localsyncrootfolder);
        fs::create_directory(syncdir);
        future<bool> fb = thread_do([=](StandardClient& mc, promise<bool>& pb) { pb.set_value(mc.setupSync_inthread(desc, syncid, remotesyncrootfolder, syncdir)); });
        return fb.get();
    }

    bool delSync_mainthread(int syncId, bool keepCache = false)
    {
        future<bool> fb = thread_do([=](StandardClient& mc, promise<bool>& pb) { pb.set_value(mc.delSync_inthread(syncId, keepCache)); });
        return fb.get();
    }

    bool confirmModel_mainthread(Model::ModelNode* mnode, int syncid, const Confirm confirm = CONFIRM_ALL)
    {
        future<bool> fb;
        fb = thread_do([syncid, mnode, confirm](StandardClient& sc, promise<bool>& pb) { pb.set_value(sc.confirmModel(syncid, mnode, confirm)); });
        return fb.get();
    }

    void removeLocalDebris(int syncid)
    {
        fs::remove_all(syncSet[syncid].localpath / DEBRISFOLDER);
    }
};


void waitonsyncs(chrono::seconds d = 4s, StandardClient* c1 = nullptr, StandardClient* c2 = nullptr, StandardClient* c3 = nullptr, StandardClient* c4 = nullptr)
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
    bool b = fs::create_directory(p);
    assert(b);
    return p;
}

//std::atomic<int> fileSizeCount = 20;

bool createFile(const fs::path &p, const string &filename)
{
    fs::path fp = p / fs::u8path(filename);
#if (__cplusplus >= 201700L)
    ofstream fs(fp/*, ios::binary*/);
#else
    ofstream fs(fp.u8string()/*, ios::binary*/);
#endif
    if (!fs.is_open())
    {
        return false;
    }
    fs << filename;
    if (fs.bad())
    {
        return false;
    }
    return true;
}

bool appendToFile(const fs::path &p, const string &filename, const string& data)
{
    fs::path fp = p / fs::u8path(filename);
#if (__cplusplus >= 201700L)
    ofstream fs(fp, std::ios_base::app);
#else
    ofstream fs(fp.u8string(), std::ios_base::app);
#endif
    if (!fs.is_open())
    {
        return false;
    }
    fs << data;
    if (fs.bad())
    {
        return false;
    }
    return true;
}

bool readFileContents(std::string& content, const fs::path& p, const string& filename)
{
    fs::path fp = p / fs::u8path(filename);
#if (__cplusplus >= 201700L)
    ifstream fs(fp);
#else
    ifstream fs(fp.u8string());
#endif
    if (!fs.is_open())
    {
        return false;
    }
    content = std::string{std::istreambuf_iterator<char>{fs}, std::istreambuf_iterator<char>{}};
    if (fs.bad())
    {
        return false;
    }
    return true;;
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
    waitonsyncs(4s, &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    Model model;
    model.root->addkid(model.buildModelSubdirs("f", 3, 3, 0));

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // delete something remotely and let sync catch up
    future<bool> fb = clientA1.thread_do([](StandardClient& sc, promise<bool>& pb) { sc.deleteremote("f/f_2/f_2_1", pb); });
    ASSERT_TRUE(waitonresults(&fb));
    waitonsyncs(60s, &clientA1, &clientA2);

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
    waitonsyncs(4s, &clientA1, &clientA2);
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
    waitonsyncs(60s, &clientA1, &clientA2);

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
    waitonsyncs(4s, &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // move something in the local filesystem and see if we catch up in A1 and A2 (deleter and observer syncs)
    error_code rename_error;
    fs::rename(clientA1.syncSet[1].localpath / "f_2" / "f_2_1", clientA1.syncSet[1].localpath / "f_2_1", rename_error);
    ASSERT_TRUE(!rename_error) << rename_error;

    // let them catch up
    waitonsyncs(4s, &clientA1, &clientA2);

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
    waitonsyncs(4s, &clientA1, &clientA2, &clientA3);
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
    waitonsyncs(4s, &clientA1, &clientA2, &clientA3);

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
    waitonsyncs(4s, &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // make new folders (and files) in the local filesystem and see if we catch up in A1 and A2 (adder and observer syncs)
    ASSERT_TRUE(buildLocalFolders(clientA1.syncSet[1].localpath / "f_2", "newkid", 2, 2, 2));

    // let them catch up
    waitonsyncs(30s, &clientA1, &clientA2);  // two minutes should be long enough to get past API_ETEMPUNAVAIL == -18 for sync2 downloading the files uploaded by sync1

    // check everything matches (model has expected state of remote and local)
    model.findnode("f/f_2")->addkid(model.buildModelSubdirs("newkid", 2, 2, 2));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    model.ensureLocalDebrisTmpLock("f"); // since we downloaded files
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
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
    waitonsyncs(4s, &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // make new folders in the local filesystem and see if we catch up in A1 and A2 (adder and observer syncs)
    assert(MegaClient::MAX_NEWNODES < 3125);
    ASSERT_TRUE(buildLocalFolders(clientA1.syncSet[1].localpath, "g", 5, 5, 0));  // 5^5=3125 leaf folders, 625 pre-leaf etc

    // let them catch up
    waitonsyncs(30s, &clientA1, &clientA2);

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
    waitonsyncs(4s, &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // make new folders in the local filesystem and see if we catch up in A1 and A2 (adder and observer syncs)
    assert(MegaClient::MAX_NEWNODES < 3000);
    ASSERT_TRUE(buildLocalFolders(clientA1.syncSet[1].localpath, "g", 3000, 1, 0));

    // let them catch up
    waitonsyncs(30s, &clientA1, &clientA2);

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
    waitonsyncs(4s, &clientA1, &clientA2);
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
    waitonsyncs(4s, &clientA1, &clientA2);

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
    waitonsyncs(4s, &clientA1, &clientA2);
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
    waitonsyncs(30s, &clientA1, &clientA2);

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
    waitonsyncs(4s, &clientA1, &clientA2);
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
    waitonsyncs(4s, pclientA1.get(), &clientA2);
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

    waitonsyncs(4s, pclientA1.get(), &clientA2);

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
    waitonsyncs(4s, &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // now have both clients create the same remote folder structure simultaneously.  We should end up with just one copy of it on the server and in both syncs
    future<bool> p1 = clientA1.thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.makeCloudSubdirs("f", 3, 3, pb); });
    future<bool> p2 = clientA2.thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.makeCloudSubdirs("f", 3, 3, pb); });
    ASSERT_TRUE(waitonresults(&p1, &p2));

    // let them catch up
    waitonsyncs(4s, &clientA1, &clientA2);

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
    waitonsyncs(4s, &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;

    // now have both clients create the same folder structure simultaneously.  We should end up with just one copy of it on the server and in both syncs
    future<bool> p1 = clientA1.thread_do([=](StandardClient& sc, promise<bool>& pb) { buildLocalFolders(sc.syncSet[1].localpath, "f", 3, 3, 0); pb.set_value(true); });
    future<bool> p2 = clientA2.thread_do([=](StandardClient& sc, promise<bool>& pb) { buildLocalFolders(sc.syncSet[2].localpath, "f", 3, 3, 0); pb.set_value(true); });
    ASSERT_TRUE(waitonresults(&p1, &p2));

    // let them catch up
    waitonsyncs(30s, &clientA1, &clientA2);

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
    waitonsyncs(4s, pclientA1.get(), &clientA2);
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
    waitonsyncs(20s, &clientA2);

    cout << "*********************  resume A1 session (with sync), see if A2 nodes and localnodes get in sync again" << endl;
    pclientA1.reset(new StandardClient(localtestroot, "clientA1"));
    ASSERT_TRUE(pclientA1->login_fetchnodes_resumesync(string((char*)session, sessionsize), sync1path.u8string(), "f", 1));
    ASSERT_EQ(pclientA1->basefolderhandle, clientA2.basefolderhandle);
    waitonsyncs(20s, pclientA1.get(), &clientA2);

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
    waitonsyncs(4s, pclientA1.get(), &clientA2);
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
    waitonsyncs(4s, &clientA2);

    // resume A1 session (with sync), see if A2 nodes and localnodes get in sync again
    pclientA1.reset(new StandardClient(localtestroot, "clientA1"));
    ASSERT_TRUE(pclientA1->login_fetchnodes_resumesync(string((char*)session, sessionsize), sync1path.u8string(), "f", 1));
    ASSERT_EQ(pclientA1->basefolderhandle, clientA2.basefolderhandle);
    waitonsyncs(4s, pclientA1.get(), &clientA2);

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

    waitonsyncs(4s, &clientA1, &clientA2);
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
    waitonsyncs(20s, &clientA1, &clientA2);

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

    waitonsyncs(4s, &clientA1, &clientA2);
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
    waitonsyncs(20s, &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(model.movetosynctrash("f/f_0", "f"));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
    ASSERT_TRUE(model.removesynctrash("f"));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
}

namespace {

// This fixture is intended for testing the one-way-sync functionality
class OneWayFixture
{
public:
    OneWayFixture(const SyncDescriptor::Type type, const bool syncDel, const bool overwrite)
    : mDesc{type, syncDel, overwrite}
    {
        assert(type != SyncDescriptor::TYPE_DEFAULT);

        const fs::path localtestroot = makeNewTestRoot(LOCAL_TEST_FOLDER);
        mClientRef.reset(new StandardClient{localtestroot, "ClientRef"});   // user 1 client 1 (two-way)
        mClientOneWay.reset(new StandardClient{localtestroot, "ClientOneWay"});   // user 1 client 2 (one-way)

        EXPECT_TRUE(mClientRef->login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "f"));
        EXPECT_TRUE(mClientOneWay->login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
        EXPECT_EQ(mClientRef->basefolderhandle, mClientOneWay->basefolderhandle);

        EXPECT_TRUE(mClientRef->setupSync_mainthread("sync", "f", 0));
        EXPECT_TRUE(mClientOneWay->setupSync_mainthread(mDesc, "sync", "f", 0));
        wait(4);
        mClientRef->logcb = mClientOneWay->logcb = true;
    }

    const fs::path& refRootPath() const
    {
        return mClientRef->syncSet.at(0).localpath;
    }

    const fs::path& oneWayRootPath() const
    {
        return mClientOneWay->syncSet.at(0).localpath;
    }

    void wait(const int sec = 5) const
    {
        waitonsyncs(chrono::seconds{sec}, mClientRef.get(), mClientOneWay.get());
    }

    bool checkRef(const Model& model) const
    {
        mClientRef->removeLocalDebris(0);
        return mClientRef->confirmModel_mainthread(model.root.get(), 0);
    }

    bool checkOneWay(const Model& model, const StandardClient::Confirm confirm = StandardClient::CONFIRM_ALL) const
    {
        mClientOneWay->removeLocalDebris(0);
        return mClientOneWay->confirmModel_mainthread(model.root.get(), 0, confirm);
    }

    bool pauseOneWay()
    {
        return mClientOneWay->delSync_mainthread(0, true);
    }

    bool resumeOneWay()
    {
        return mClientOneWay->setupSync_mainthread(mDesc, "sync", "f", 0);
    }

private:
    SyncDescriptor mDesc;
    std::unique_ptr<StandardClient> mClientRef;
    std::unique_ptr<StandardClient> mClientOneWay;
};

} // anonymous

TEST(Sync, OneWay_Upload_syncDelFalse_overwriteFalse_1)
{
    /* Steps:
     * - Add remote file
     * - Assert: No local file
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_UP, false, false};

    ASSERT_TRUE(createFile(fx.refRootPath(), "foo"));

    fx.wait();
    // foo is now uploaded

    Model localModel;

    Model remoteModel;
    remoteModel.root->addkid(remoteModel.makeModelSubfile("foo"));

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
}

TEST(Sync, OneWay_Upload_syncDelFalse_overwriteFalse_2)
{
    /* Steps:
     * - Add local file
     * - Assert: File uploaded
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_UP, false, false};

    ASSERT_TRUE(createFile(fx.oneWayRootPath(), "foo"));

    fx.wait();
    // foo is now uploaded

    Model model;
    model.root->addkid(model.makeModelSubfile("foo"));

    ASSERT_TRUE(fx.checkRef(model));
    ASSERT_TRUE(fx.checkOneWay(model));
}

TEST(Sync, OneWay_Upload_syncDelFalse_overwriteFalse_3)
{
    /* Steps:
     * - Add local file
     * - Wait for upload
     * - Edit local file
     * - Assert: Edited file uploaded
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_UP, false, false};

    ASSERT_TRUE(createFile(fx.oneWayRootPath(), "foo"));

    fx.wait();
    // foo is now uploaded

    ASSERT_TRUE(appendToFile(fx.oneWayRootPath(), "foo", "blah"));

    fx.wait();
    // new foo is now uploaded

    Model model;
    auto fooNodeOld = model.makeModelSubfile("foo");
    auto fooNode = model.makeModelSubfile("foo", "fooblah");
    fooNode->addkid(std::move(fooNodeOld));
    model.root->addkid(std::move(fooNode));

    ASSERT_TRUE(fx.checkRef(model));
    ASSERT_TRUE(fx.checkOneWay(model));
}

TEST(Sync, OneWay_Upload_syncDelFalse_overwriteFalse_4)
{
    /* Steps:
     * - Add local file
     * - Wait for upload
     * - Remove local file
     * - Assert: Remote file still there
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_UP, false, false};

    ASSERT_TRUE(createFile(fx.oneWayRootPath(), "foo"));

    fx.wait();
    // foo is now uploaded

    fs::remove(fx.oneWayRootPath() / "foo");

    fx.wait();
    // foo is not deleted on the remote

    Model localModel;

    Model remoteModel;
    remoteModel.root->addkid(remoteModel.makeModelSubfile("foo"));

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
}

TEST(Sync, OneWay_Upload_syncDelFalse_overwriteFalse_5)
{
    /* Steps:
     * - Add local file
     * - Wait for upload
     * - Remove remote file via ref
     * - Edit local file
     * - Assert: Remote file still gone
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_UP, false, false};

    ASSERT_TRUE(createFile(fx.oneWayRootPath(), "foo"));

    fx.wait();
    // foo is now uploaded

    fs::remove(fx.refRootPath() / "foo");

    fx.wait();
    // foo is now removed from remote

    ASSERT_TRUE(appendToFile(fx.oneWayRootPath(), "foo", "blah"));

    fx.wait();
    // new foo is not uploaded

    Model localModel;
    auto localFooNodeOld = localModel.makeModelSubfile("foo");
    auto localFooNode = localModel.makeModelSubfile("foo", "fooblah");
    localFooNode->addkid(std::move(localFooNodeOld));
    localModel.root->addkid(std::move(localFooNode));

    Model remoteModel;

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
}

TEST(Sync, OneWay_Upload_syncDelFalse_overwriteFalse_6)
{
    /* Steps:
     * - Add local file
     * - Wait for upload
     * - Pause sync of oneWay
     * - Edit local file
     * - Edit remote file via ref
     * - Resume sync of oneWay
     * - Assert: New local is not uploaded (remote is newer)
     */
    OneWayFixture fx{SyncDescriptor::TYPE_UP, false, false};

    ASSERT_TRUE(createFile(fx.oneWayRootPath(), "foo"));

    fx.wait();
    // foo is now uploaded

    ASSERT_TRUE(fx.pauseOneWay());

    fx.wait();
    // oneWay is now paused

    ASSERT_TRUE(appendToFile(fx.oneWayRootPath(), "foo", "blah"));

    this_thread::sleep_for(chrono::seconds{3}); // wait a bit

    ASSERT_TRUE(appendToFile(fx.refRootPath(), "foo", "halb"));

    fx.wait();
    // new foo from ref is now uploaded

    fx.resumeOneWay();

    fx.wait();
    // oneWay is now resumed. New foo from oneWay is not uploaded (remote is newer)

    Model localModel;
    auto localFooNodeOld = localModel.makeModelSubfile("foo");
    auto localFooNode = localModel.makeModelSubfile("foo", "fooblah");
    localFooNode->addkid(std::move(localFooNodeOld));
    localModel.root->addkid(std::move(localFooNode));

    Model remoteModel;
    auto remoteFooNodeOld = remoteModel.makeModelSubfile("foo");
    auto remoteFooNode = remoteModel.makeModelSubfile("foo", "foohalb");
    remoteFooNode->addkid(std::move(remoteFooNodeOld));
    remoteModel.root->addkid(std::move(remoteFooNode));

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
}

TEST(Sync, OneWay_Upload_syncDelTrue_overwriteFalse_1)
{
    /* Steps:
     * - Add local file
     * - Wait for upload
     * - Remove local file
     * - Assert: Remote file gone
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_UP, true, false};

    ASSERT_TRUE(createFile(fx.oneWayRootPath(), "foo"));

    fx.wait();
    // foo is now uploaded

    fs::remove(fx.oneWayRootPath() / "foo");

    fx.wait();
    // foo is deleted on the remote

    Model model;

    ASSERT_TRUE(fx.checkRef(model));
    ASSERT_TRUE(fx.checkOneWay(model));
}

TEST(Sync, OneWay_Upload_syncDelTrue_overwriteFalse_2)
{
    /* Steps:
     * - Add local file
     * - Wait for upload
     * - Edit file via ref
     * - Remove local file
     * - Assert: New remote file still there
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_UP, true, false};

    ASSERT_TRUE(createFile(fx.oneWayRootPath(), "foo"));

    fx.wait();
    // foo is now uploaded

    ASSERT_TRUE(appendToFile(fx.refRootPath(), "foo", "blah"));

    fx.wait();
    // new foo is now uploaded

    fs::remove(fx.oneWayRootPath() / "foo");

    fx.wait();
    // foo is not deleted on the remote

    Model localModel;

    Model remoteModel;
    auto remoteFooNodeOld = remoteModel.makeModelSubfile("foo");
    auto remoteFooNode = remoteModel.makeModelSubfile("foo", "fooblah");
    remoteFooNode->addkid(std::move(remoteFooNodeOld));
    remoteModel.root->addkid(std::move(remoteFooNode));

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
}

TEST(Sync, OneWay_Upload_syncDelTrue_overwriteFalse_3)
{
    /* Steps:
     * - Add local file
     * - Wait for upload
     * - Pause oneWay sync
     * - Remove local file
     * - Edit file via ref
     * - Resume oneWay sync
     * - Assert: New remote file still there
     */
    OneWayFixture fx{SyncDescriptor::TYPE_UP, true, false};

    ASSERT_TRUE(createFile(fx.oneWayRootPath(), "foo"));

    fx.wait();
    // foo is now uploaded

    fx.pauseOneWay();

    fx.wait();
    // oneWay is now paused

    fs::remove(fx.oneWayRootPath() / "foo");

    this_thread::sleep_for(chrono::seconds{3}); // wait a bit

    ASSERT_TRUE(appendToFile(fx.refRootPath(), "foo", "blah"));

    fx.wait();
    // new foo is now uploaded

    fx.resumeOneWay();

    fx.wait();
    // foo is not deleted on the remote

    Model localModel;

    Model remoteModel;
    auto remoteFooNodeOld = remoteModel.makeModelSubfile("foo");
    auto remoteFooNode = remoteModel.makeModelSubfile("foo", "fooblah");
    remoteFooNode->addkid(std::move(remoteFooNodeOld));
    remoteModel.root->addkid(std::move(remoteFooNode));

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
}

TEST(Sync, OneWay_Upload_syncDelTrue_overwriteFalse_4)
{
    /* Steps:
     * - Add local file
     * - Wait for upload
     * - Edit file via ref
     * - Remove local file
     * - Create new local foo
     * - Assert: Remote has 3 foo versions (local new foo is most recent)
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_UP, true, false};

    ASSERT_TRUE(createFile(fx.oneWayRootPath(), "foo"));

    fx.wait();
    // foo is now uploaded

    ASSERT_TRUE(appendToFile(fx.refRootPath(), "foo", "blah"));

    fx.wait();
    // new foo is now uploaded

    fs::remove(fx.oneWayRootPath() / "foo");

    fx.wait();
    // foo is not deleted on the remote

    ASSERT_TRUE(appendToFile(fx.oneWayRootPath(), "foo", "halb"));

    fx.wait();
    // new foo is now uploaded

    Model localModel;
    localModel.root->addkid(localModel.makeModelSubfile("foo", "halb"));

    Model remoteModel;
    auto remoteFooNodeOldOld = remoteModel.makeModelSubfile("foo");
    auto remoteFooNodeOld = remoteModel.makeModelSubfile("foo", "blah");
    auto remoteFooNode = remoteModel.makeModelSubfile("foo", "halb");
    remoteFooNodeOld->addkid(std::move(remoteFooNodeOldOld));
    remoteFooNode->addkid(std::move(remoteFooNodeOld));
    remoteModel.root->addkid(std::move(remoteFooNode));

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
}

TEST(Sync, OneWay_Upload_syncDelFalse_overwriteTrue_1)
{
    /* Steps:
     * - Add local file
     * - Wait for upload
     * - Remove file via ref
     * - Edit local file
     * - Assert: New file is uploaded
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_UP, false, true};

    ASSERT_TRUE(createFile(fx.oneWayRootPath(), "foo"));

    fx.wait();
    // foo is now uploaded

    fs::remove(fx.refRootPath() / "foo");

    fx.wait();
    // foo is deleted on the remote

    ASSERT_TRUE(appendToFile(fx.oneWayRootPath(), "foo", "blah"));

    fx.wait();
    // new foo is now uploaded

    Model model;
    model.root->addkid(model.makeModelSubfile("foo", "fooblah"));

    ASSERT_TRUE(fx.checkRef(model));
    ASSERT_TRUE(fx.checkOneWay(model));
}

TEST(Sync, OneWay_Upload_syncDelFalse_overwriteTrue_2)
{
    /* Steps:
     * - Add local file
     * - Wait for upload
     * - Pause oneWay
     * - Edit local file
     * - Edit file via ref
     * - Resume oneWay
     * - Assert: New local file is uploaded
     */
    OneWayFixture fx{SyncDescriptor::TYPE_UP, false, true};

    ASSERT_TRUE(createFile(fx.oneWayRootPath(), "foo"));

    fx.wait();
    // foo is now uploaded

    fx.pauseOneWay();

    fx.wait();
    // oneWay is paused

    ASSERT_TRUE(appendToFile(fx.oneWayRootPath(), "foo", "blaha"));

    this_thread::sleep_for(chrono::seconds{3}); // wait a bit

    ASSERT_TRUE(appendToFile(fx.refRootPath(), "foo", "halb"));

    fx.wait();
    // new foo is uploaded from ref

    fx.resumeOneWay();

    fx.wait();
    // oneWay is resumed

    Model localModel;
    localModel.root->addkid(localModel.makeModelSubfile("foo", "fooblaha"));

    Model remoteModel;
    auto remoteFooNodeOldOld = remoteModel.makeModelSubfile("foo");
    auto remoteFooNodeOld = remoteModel.makeModelSubfile("foo", "foohalb");
    auto remoteFooNode = remoteModel.makeModelSubfile("foo", "fooblaha");
    remoteFooNodeOld->addkid(std::move(remoteFooNodeOldOld));
    remoteFooNode->addkid(std::move(remoteFooNodeOld));
    remoteModel.root->addkid(std::move(remoteFooNode));

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
}

TEST(Sync, OneWay_Upload_syncDelTrue_overwriteTrue_1)
{
    /* Steps:
     * - Add local file
     * - Wait for upload
     * - Pause oneWay
     * - Delete local file
     * - Edit file via ref
     * - Resume oneWay
     * - Assert: Remote file is gone
     */
    OneWayFixture fx{SyncDescriptor::TYPE_UP, true, true};

    ASSERT_TRUE(createFile(fx.oneWayRootPath(), "foo"));

    fx.wait();
    // foo is now uploaded

    fx.pauseOneWay();

    fx.wait();
    // oneWay is paused

    fs::remove(fx.oneWayRootPath() / "foo");

    this_thread::sleep_for(chrono::seconds{3}); // wait a bit

    ASSERT_TRUE(appendToFile(fx.refRootPath(), "foo", "blah"));

    fx.wait();
    // new foo is uploaded from ref

    fx.resumeOneWay();

    fx.wait();
    // oneWay is resumed

    Model model;

    ASSERT_TRUE(fx.checkRef(model));
    ASSERT_TRUE(fx.checkOneWay(model));
}

TEST(Sync, OneWay_Download_syncDelFalse_overwriteFalse_1)
{
    /* Steps:
     * - Add local file
     * - Assert: No remote file
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_DOWN, false, false};

    ASSERT_TRUE(createFile(fx.oneWayRootPath(), "foo"));

    fx.wait();
    // foo is not uploaded

    Model localModel;
    localModel.root->addkid(localModel.makeModelSubfile("foo"));

    Model remoteModel;

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
}

TEST(Sync, OneWay_Download_syncDelFalse_overwriteFalse_2)
{
    /* Steps:
     * - Add remote file
     * - Wait for download
     * - Assert: file downloaded
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_DOWN, false, false};

    ASSERT_TRUE(createFile(fx.refRootPath(), "foo"));

    fx.wait();
    // foo is downloaded

    Model model;
    model.root->addkid(model.makeModelSubfile("foo"));

    ASSERT_TRUE(fx.checkRef(model));
    ASSERT_TRUE(fx.checkOneWay(model));
}

TEST(Sync, OneWay_Download_syncDelFalse_overwriteFalse_3)
{
    /* Steps:
     * - Add remote file
     * - Wait for download
     * - Edit remote file
     * - Assert: New file downloaded
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_DOWN, false, false};

    ASSERT_TRUE(createFile(fx.refRootPath(), "foo"));

    fx.wait();
    // foo is downloaded

    ASSERT_TRUE(appendToFile(fx.refRootPath(), "foo", "blah"));

    fx.wait();
    // new foo is downloaded

    Model model;
    auto fooNodeOld = model.makeModelSubfile("foo");
    auto fooNode = model.makeModelSubfile("foo", "fooblah");
    fooNode->addkid(std::move(fooNodeOld));
    model.root->addkid(std::move(fooNode));

    ASSERT_TRUE(fx.checkRef(model));
    ASSERT_TRUE(fx.checkOneWay(model));
}

TEST(Sync, OneWay_Download_syncDelFalse_overwriteFalse_4)
{
    /* Steps:
     * - Add remote file
     * - Wait for download
     * - Remove remote file
     * - Assert: Local file still there
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_DOWN, false, false};

    ASSERT_TRUE(createFile(fx.refRootPath(), "foo"));

    fx.wait();
    // foo is downloaded

    fs::remove(fx.refRootPath() / "foo");

    fx.wait();
    // foo is not deleted

    Model localModel;
    localModel.root->addkid(localModel.makeModelSubfile("foo"));

    Model remoteModel;

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
}

TEST(Sync, OneWay_Download_syncDelFalse_overwriteFalse_5)
{
    /* Steps:
     * - Add remote file
     * - Wait for download
     * - Remove local file
     * - Edit remote file
     * - Assert: Local file still gone
     */
    const OneWayFixture fx{SyncDescriptor::TYPE_DOWN, false, false};

    ASSERT_TRUE(createFile(fx.refRootPath(), "foo"));

    fx.wait();
    // foo is downloaded

    fs::remove(fx.oneWayRootPath() / "foo");

    fx.wait();
    // foo is not deleted on remote

    ASSERT_TRUE(appendToFile(fx.refRootPath(), "foo", "blah"));

    fx.wait();
    // foo is not downloaded

    Model localModel;

    Model remoteModel;
    auto remoteFooNodeOld = remoteModel.makeModelSubfile("foo");
    auto remoteFooNode = remoteModel.makeModelSubfile("foo", "fooblah");
    remoteFooNode->addkid(std::move(remoteFooNodeOld));
    remoteModel.root->addkid(std::move(remoteFooNode));

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
}

TEST(Sync, OneWay_Download_syncDelFalse_overwriteFalse_6)
{
    /* Steps:
     * - Add remote file
     * - Wait for download
     * - Pause oneWay
     * - Edit remote file
     * - Edit local file
     * - Resume oneWay
     * - Assert: New remote is not downloaded (local is newer)
     */
    OneWayFixture fx{SyncDescriptor::TYPE_DOWN, false, false};

    ASSERT_TRUE(createFile(fx.refRootPath(), "foo"));

    fx.wait();
    // foo is downloaded

    fx.pauseOneWay();

    fx.wait();
    // oneWay is paused

    ASSERT_TRUE(appendToFile(fx.refRootPath(), "foo", "blaha"));

    this_thread::sleep_for(chrono::seconds{3}); // wait a bit

    ASSERT_TRUE(appendToFile(fx.oneWayRootPath(), "foo", "halb"));

    fx.wait();
    // new file is now on remote

    fx.resumeOneWay();

    fx.wait();
    // oneWay is resumed

    Model localModel;
    localModel.root->addkid(localModel.makeModelSubfile("foo", "foohalb"));

    Model remoteModel;
    auto remoteFooNodeOld = remoteModel.makeModelSubfile("foo");
    auto remoteFooNode = remoteModel.makeModelSubfile("foo", "fooblaha");
    remoteFooNode->addkid(std::move(remoteFooNodeOld));
    remoteModel.root->addkid(std::move(remoteFooNode));

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
}

TEST(Sync, OneWay_Download_syncDelTrue_overwriteFalse_1)
{
    /* Steps:
     * - Add remote file
     * - Wait for download
     * - Remove remote file
     * - Assert: Local file is gone
     */
    OneWayFixture fx{SyncDescriptor::TYPE_DOWN, true, false};

    ASSERT_TRUE(createFile(fx.refRootPath(), "foo"));

    fx.wait();
    // foo is downloaded

    fs::remove(fx.refRootPath() / "foo");

    fx.wait();
    // foo is now deleted

    Model model;

    ASSERT_TRUE(fx.checkRef(model));
    ASSERT_TRUE(fx.checkOneWay(model));
}

TEST(Sync, OneWay_Download_syncDelTrue_overwriteFalse_2)
{
    /* Steps:
     * - Add remote file
     * - Wait for download
     * - Edit local file
     * - Delete remote file
     * - Assert: Local file gone
     */
    OneWayFixture fx{SyncDescriptor::TYPE_DOWN, true, false};

    ASSERT_TRUE(createFile(fx.refRootPath(), "foo"));

    fx.wait();
    // foo is downloaded

    ASSERT_TRUE(appendToFile(fx.oneWayRootPath(), "foo", "blaha"));

    this_thread::sleep_for(chrono::seconds{3}); // wait a bit

    fs::remove(fx.refRootPath() / "foo");

    fx.wait();
    // foo is not deleted

    Model model;

    ASSERT_TRUE(fx.checkRef(model));
    ASSERT_TRUE(fx.checkOneWay(model));
}

TEST(Sync, OneWay_Download_syncDelTrue_overwriteFalse_3)
{
    /* Steps:
     * - Add remote file
     * - Wait for download
     * - Pause sync
     * - Delete remote file
     * - Edit local file
     * - Resume sync
     * - Assert: Local file still there
     */
    OneWayFixture fx{SyncDescriptor::TYPE_DOWN, true, false};

    ASSERT_TRUE(createFile(fx.refRootPath(), "foo"));

    fx.wait();
    // foo is downloaded

    fx.pauseOneWay();

    fx.wait();
    // oneWay is paused

    fs::remove(fx.refRootPath() / "foo");

    this_thread::sleep_for(chrono::seconds{3}); // wait a bit

    ASSERT_TRUE(appendToFile(fx.oneWayRootPath(), "foo", "blaha"));

    fx.wait();
    // foo is now deleted on remote

    fx.resumeOneWay();

    fx.wait();
    // oneWay is resumed

    Model localModel;
    localModel.root->addkid(localModel.makeModelSubfile("foo", "fooblaha"));

    Model remoteModel;

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
}

TEST(Sync, OneWay_Download_syncDelFalse_overwriteTrue_1)
{
    /* Steps:
     * - Add remote file
     * - Wait for download
     * - Remove local file
     * - Edit remote file
     * - Assert: New remote file is downloaded
     */
    OneWayFixture fx{SyncDescriptor::TYPE_DOWN, false, true};

    ASSERT_TRUE(createFile(fx.refRootPath(), "foo"));

    fx.wait();
    // foo is downloaded

    fs::remove(fx.oneWayRootPath() / "foo");

    this_thread::sleep_for(chrono::seconds{3}); // wait a bit

    ASSERT_TRUE(appendToFile(fx.refRootPath(), "foo", "blaha"));

    fx.wait();
    // new foo is downloaded

    Model model;
    auto fooNodeOld = model.makeModelSubfile("foo");
    auto fooNode = model.makeModelSubfile("foo", "fooblaha");
    fooNode->addkid(std::move(fooNodeOld));
    model.root->addkid(std::move(fooNode));

    ASSERT_TRUE(fx.checkRef(model));
    ASSERT_TRUE(fx.checkOneWay(model));
}

TEST(Sync, OneWay_Download_syncDelFalse_overwriteTrue_2)
{
    /* Steps:
     * - Add remote file
     * - Wait for download
     * - Pause oneWay
     * - Edit remote file
     * - Edit local file
     * - Resume oneWay
     * - Assert: New remote file replaces local file
     */
    OneWayFixture fx{SyncDescriptor::TYPE_DOWN, false, true};

    ASSERT_TRUE(createFile(fx.refRootPath(), "foo"));

    fx.wait();
    // foo is downloaded

    fx.pauseOneWay();

    fx.wait();
    // oneWay is paused

    ASSERT_TRUE(appendToFile(fx.refRootPath(), "foo", "blaha"));

    this_thread::sleep_for(chrono::seconds{3}); // wait a bit

    ASSERT_TRUE(appendToFile(fx.oneWayRootPath(), "foo", "halb"));

    fx.wait();
    // new foo is downloaded

    fx.resumeOneWay();

    fx.wait();
    // oneWay is paused

    Model model;
    auto fooNodeOld = model.makeModelSubfile("foo");
    auto fooNode = model.makeModelSubfile("foo", "fooblaha");
    fooNode->addkid(std::move(fooNodeOld));
    model.root->addkid(std::move(fooNode));

    ASSERT_TRUE(fx.checkRef(model));
    ASSERT_TRUE(fx.checkOneWay(model));
}

TEST(Sync, OneWay_Download_syncDelTrue_overwriteTrue_1)
{
    /* Steps:
     * - Add remote file
     * - Wait for download
     * - Pause oneWay
     * - Delete remote file
     * - Edit local file
     * - Resume oneWay
     * - Assert: Local file still there
     */
    OneWayFixture fx{SyncDescriptor::TYPE_DOWN, true, true};

    ASSERT_TRUE(createFile(fx.refRootPath(), "foo"));

    fx.wait();
    // foo is downloaded

    fx.pauseOneWay();

    fx.wait();
    // oneWay is paused

    fs::remove(fx.refRootPath() / "foo");

    this_thread::sleep_for(chrono::seconds{3}); // wait a bit

    ASSERT_TRUE(appendToFile(fx.oneWayRootPath(), "foo", "halb"));

    fx.wait();
    // foo is edited on remote

    fx.resumeOneWay();

    fx.wait();
    // oneWay is resumed

    Model localModel;
    localModel.root->addkid(localModel.makeModelSubfile("foo", "foohalb"));

    Model remoteModel;

    ASSERT_TRUE(fx.checkRef(remoteModel));
    ASSERT_TRUE(fx.checkOneWay(remoteModel, StandardClient::CONFIRM_REMOTE));
    ASSERT_TRUE(fx.checkOneWay(localModel, StandardClient::CONFIRM_LOCAL));
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
    
    newnode->nodekey.assign((char*)key, type == FILENODE ? FILENODEKEYLENGTH : FOLDERNODEKEYLENGTH);
    newnode->attrstring = new string;

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

#ifndef _WIN32
#define DEFAULWAIT 20s
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

    waitonsyncs(4s, &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;
    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));


    // move something in the local filesystem and see if we catch up in A1 and A2 (deleter and observer syncs)
    error_code linkage_error;
    fs::create_symlink(clientA1.syncSet[1].localpath / "f_0", clientA1.syncSet[1].localpath / "linked", linkage_error);
    ASSERT_TRUE(!linkage_error) << linkage_error;

    // let them catch up
    waitonsyncs(DEFAULWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));


    fs::remove(clientA1.syncSet[1].localpath / "linked");
    // let them catch up
    waitonsyncs(DEFAULWAIT, &clientA1, &clientA2);

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

    waitonsyncs(4s, &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;
    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));


    // move something in the local filesystem and see if we catch up in A1 and A2 (deleter and observer syncs)
    error_code linkage_error;
    fs::create_symlink(clientA1.syncSet[1].localpath / "f_0", clientA1.syncSet[1].localpath / "linked", linkage_error);
    ASSERT_TRUE(!linkage_error) << linkage_error;

    // let them catch up
    waitonsyncs(DEFAULWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    fs::rename(clientA1.syncSet[1].localpath / "linked", clientA1.syncSet[1].localpath / "linkrenamed", linkage_error);

    // let them catch up
    waitonsyncs(DEFAULWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    fs::remove(clientA1.syncSet[1].localpath / "linkrenamed");

    // let them catch up
    waitonsyncs(DEFAULWAIT, &clientA1, &clientA2);

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

    waitonsyncs(4s, &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;
    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));


    // move something in the local filesystem and see if we catch up in A1 and A2 (deleter and observer syncs)
    error_code linkage_error;
    fs::create_symlink(clientA1.syncSet[1].localpath / "f_0", clientA1.syncSet[1].localpath / "linked", linkage_error);
    ASSERT_TRUE(!linkage_error) << linkage_error;

    // let them catch up
    waitonsyncs(DEFAULWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
    fs::rename(clientA1.syncSet[1].localpath / "f_0", clientA1.syncSet[1].localpath / "linked", linkage_error);

    // let them catch up
    waitonsyncs(DEFAULWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    fs::remove(clientA1.syncSet[1].localpath / "linked");
    ASSERT_TRUE(createFile(clientA1.syncSet[1].localpath, "linked"));

    // let them catch up
    waitonsyncs(DEFAULWAIT, &clientA1, &clientA2);

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

    waitonsyncs(4s, &clientA1, &clientA2);
    clientA1.logcb = clientA2.logcb = true;
    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    // move something in the local filesystem and see if we catch up in A1 and A2 (deleter and observer syncs)
    error_code linkage_error;
    fs::create_symlink(clientA1.syncSet[1].localpath / "f_0", clientA1.syncSet[1].localpath / "linked", linkage_error);
    ASSERT_TRUE(!linkage_error) << linkage_error;

    // let them catch up
    waitonsyncs(DEFAULWAIT, &clientA1, &clientA2);

    //check client 2 is unaffected
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));

    ASSERT_TRUE(createFile(clientA2.syncSet[2].localpath, "linked"));

    // let them catch up
    waitonsyncs(DEFAULWAIT, &clientA1, &clientA2);

    model.findnode("f")->addkid(model.makeModelSubfolder("linked")); //notice: the deleted here is folder because what's actually deleted is a symlink that points to a folder
                                                                     //ideally we could add full support for symlinks in this tests suite

    model.movetosynctrash("f/linked","f");
    model.findnode("f")->addkid(model.makeModelSubfile("linked"));
    model.ensureLocalDebrisTmpLock("f"); // since we downloaded files

    //check client 2 is as expected
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
}

#endif
