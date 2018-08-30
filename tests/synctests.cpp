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

#include <mega.h>
#include "gtest/gtest.h"
#include <stdio.h>
#include <filesystem>
#include <map>
#include <future>
//#include <mega/tsthooks.h>
#include <fstream>

bool suppressfiles = false;

using namespace ::mega;
using namespace ::std;
namespace fs = ::std::filesystem;
namespace {

typedef ::mega::byte byte;

#if defined(WIN32) && defined(NO_READLINE)
WinConsole* wc = NULL;
#endif

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
    };

    unique_ptr<ModelNode> makeModelSubfolder(const string& utf8Name)
    {
        unique_ptr<ModelNode> n(new ModelNode);
        n->name = utf8Name;
        return n;
    }

    unique_ptr<ModelNode> makeModelSubfile(const string& utf8Name)
    {
        unique_ptr<ModelNode> n(new ModelNode);
        n->name = utf8Name;
        n->type = ModelNode::file;
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
        return false;
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
    HANDLE functionEventMC, functionEventSC;
    WAIT_CLASS waiter;
#ifdef GFX_CLASS
    GFX_CLASS gfx;
#endif

    MegaClient client;
    bool clientthreadexit = false;
    bool fatalerror = false;
    string clientname;
    std::function<void(MegaClient&, promise<bool>&)> nextfunctionMC;
    std::promise<bool> nextfunctionMCpromise;
    std::function<void(StandardClient&, promise<bool>&)> nextfunctionSC;
    std::promise<bool> nextfunctionSCpromise;
    std::condition_variable functionDone;
    std::mutex functionDoneMutex;


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
        : functionEventMC(CreateEvent(NULL, TRUE, FALSE, NULL))
        , functionEventSC(CreateEvent(NULL, TRUE, FALSE, NULL))
        , client(this, &waiter, new HTTPIO_CLASS, new FSACCESS_CLASS,
#ifdef DBACCESS_CLASS
            new DBACCESS_CLASS(&(ensureDir(basepath / name).u8string() + "\\")),   // client takes ownership of this one
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
        , fsBasePath(basepath / name)
        , clientthread([this]() { threadloop(); })
    {
        //client.clientname = clientname + " ";
    }

    ~StandardClient()
    {
        // shut down any syncs on the same thread, or they stall the client destruction (CancelIo instead of CancelIoEx on the WinDirNotify)
        thread_do([](MegaClient& mc, promise<bool>&) { mc.locallogout(); /* mc.purgenodesusersabortsc();*/ }); 

        clientthreadexit = true;
        SetEvent(functionEventMC);
        clientthread.join();
    }

    static mutex om;
    bool logcb = false;
    chrono::steady_clock::time_point lastcb = std::chrono::steady_clock::now();
    string lp(LocalNode* ln) { string lp;  ln->getlocalpath(&lp); client.fsaccess->local2name(&lp); return lp; }
    void syncupdate_state(Sync*, syncstate_t state) override { if (logcb) { lock_guard g(om);  cout << clientname << " syncupdate_state() " << state << endl; } }
    void syncupdate_scanning(bool b) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_scanning()" << b << endl; } }
    //void syncupdate_local_folder_addition(Sync* s, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_local_folder_addition() " << lp(ln) << " " << cp << endl; }}
    //void syncupdate_local_folder_deletion(Sync*, LocalNode* ln) override { if (logcb) { lock_guard g(om);  cout << clientname << " syncupdate_local_folder_deletion() " << lp(ln) << endl; }}
    void syncupdate_local_folder_addition(Sync*, LocalNode* ln, const char* cp) override { lastcb = chrono::steady_clock::now(); }
    void syncupdate_local_folder_deletion(Sync*, LocalNode* ln) override { lastcb = chrono::steady_clock::now(); }
    void syncupdate_local_file_addition(Sync*, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_local_file_addition() " << lp(ln) << " " << cp << endl; }}
    void syncupdate_local_file_deletion(Sync*, LocalNode* ln) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_local_file_deletion() " << lp(ln) << endl; }}
    void syncupdate_local_file_change(Sync*, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_local_file_change() " << lp(ln) << " " << cp << endl; }}
    void syncupdate_local_move(Sync*, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_local_move() " << lp(ln) << " " << cp << endl; }}
    void syncupdate_local_lockretry(bool b) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_local_lockretry() " << b << endl; }}
    //void syncupdate_get(Sync*, Node* n, const char* cp) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_get()" << n->displaypath() << " " << cp << endl; }}
    void syncupdate_put(Sync*, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_put()" << lp(ln) << " " << cp << endl; }}
    //void syncupdate_remote_file_addition(Sync*, Node* n) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_remote_file_addition() " << n->displaypath() << endl; }}
    //void syncupdate_remote_file_deletion(Sync*, Node* n) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_remote_file_deletion() " << n->displaypath() << endl; }}
    //void syncupdate_remote_folder_addition(Sync*, Node* n) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_remote_folder_addition() " << n->displaypath() << endl; }}
    //void syncupdate_remote_folder_deletion(Sync*, Node* n) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_remote_folder_deletion() " << n->displaypath() << endl; }}
    void syncupdate_remote_folder_addition(Sync*, Node* n) override { lastcb = chrono::steady_clock::now(); }
    void syncupdate_remote_folder_deletion(Sync*, Node* n) override { lastcb = chrono::steady_clock::now(); }
    void syncupdate_remote_copy(Sync*, const char* cp) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_remote_copy() " << cp << endl; }}
    //void syncupdate_remote_move(Sync*, Node* n1, Node* n2) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_remote_move() " << n1->displaypath() << " " << n2->displaypath() << endl; }}
    //void syncupdate_remote_rename(Sync*, Node* n, const char* cp) override { if (logcb) { lock_guard g(om); cout << clientname << " syncupdate_remote_rename() " << n->displaypath() << " " << cp << endl; }}
    //void syncupdate_treestate(LocalNode* ln) override { if (logcb) { lock_guard g(om);   cout << clientname << " syncupdate_treestate() " << ln->ts << " " << ln->dts << " " << lp(ln) << endl; }}



    void threadloop()
        try
    {
        while (!clientthreadexit)
        {
            waiter.addhandle(functionEventMC, Waiter::HAVESTDIN);
            waiter.addhandle(functionEventSC, Waiter::HAVESTDIN);
            int r = client.wait();
            if (WAIT_OBJECT_0 == WaitForSingleObject(functionEventMC, 0))
            {
                nextfunctionMC(client, nextfunctionMCpromise);
                unique_lock<mutex> guard(functionDoneMutex);
                ResetEvent(functionEventMC);
                functionDone.notify_all();
                r = Waiter::NEEDEXEC;
            }
            if (WAIT_OBJECT_0 == WaitForSingleObject(functionEventSC, 0))
            {
                nextfunctionSC(*this, nextfunctionSCpromise);
                unique_lock<mutex> guard(functionDoneMutex);
                ResetEvent(functionEventSC);
                functionDone.notify_all();
                r = Waiter::NEEDEXEC;
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
        nextfunctionMCpromise = promise<bool>();
        nextfunctionMC = std::move(f);
        SetEvent(functionEventMC);
        std::mutex functionDoneMutex;
        unique_lock<mutex> guard(functionDoneMutex);
        while (!functionDone.wait_until(guard, chrono::steady_clock::now() + 600s, [this]() { return WAIT_TIMEOUT == WaitForSingleObject(functionEventMC, 0); }))
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
        nextfunctionSCpromise = promise<bool>();
        nextfunctionSC = std::move(f);
        SetEvent(functionEventSC);
        unique_lock<mutex> guard(functionDoneMutex);
        while (!functionDone.wait_until(guard, chrono::steady_clock::now() + 600s, [this]() { return WAIT_TIMEOUT == WaitForSingleObject(functionEventSC, 0); }))
        {
            if (!debugging)
            {
                nextfunctionSCpromise.set_value(false);
                break;
            }
        }
        return nextfunctionSCpromise.get_future();
    }

    enum resultprocenum { LOGIN, FETCHNODES, PUTNODES, UNLINK };

    void loginFromEnv(const string& userenv, const string& pwdenv, promise<bool>& pb)
    {
        string user = getenv(userenv.c_str());
        string pwd = getenv(pwdenv.c_str());

        ASSERT_FALSE(user.empty());
        ASSERT_FALSE(pwd.empty());

        byte pwkey[SymmCipher::KEYLENGTH];
        ASSERT_TRUE(API_OK == client.pw_key(pwd.c_str(), pwkey));

        resultproc.prepresult(LOGIN, [this, &pb](error e) { pb.set_value(!e); });
        client.login(user.c_str(), pwkey);
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
        PrnGen::genblock(buf, FOLDERNODEKEYLENGTH);
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
        map<resultprocenum, deque<std::function<void(error)>>> m;

        void prepresult(resultprocenum rpe, std::function<void(error)>&& f)
        {
            auto& entry = m[rpe];
            entry.emplace_back(move(f));
        }

        void processresult(resultprocenum rpe, error e)
        {
            //cout << "procenum " << rpe << " result " << e << endl;
            auto& entry = m[rpe];
            if (!entry.empty())
            {
                entry.front()(e);
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

    bool setupSync_inthread(int syncid, const string& subfoldername, const fs::path& localpath)
    {
        if (Node* n = client.nodebyhandle(basefolderhandle))
        {
            if (Node* m = drillchildnodebyname(n, subfoldername))
            {
                string local, orig = localpath.u8string();
                client.fsaccess->path2local(&orig, &local);
                error e = client.addsync(&local, DEBRISFOLDER, NULL, m, 0, syncid);  // use syncid as tag
                if (!e)
                {
                    syncSet[syncid] = SyncInfo{ m->nodehandle, localpath };
                    return true;
                }
            }
        }
        return false;
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
        for (auto& n : n->children) ns.emplace(n->displayname(), n);

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
            for (auto& n : ns) cout << " " << n.first;
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

        multimap<string, Model::ModelNode*> ms;
        multimap<string, LocalNode*> ns;
        for (auto& m : mn->kids) ms.emplace(m->name, m.get());
        for (auto& n : n->children) if (!n.second->deleted) ns.emplace(n.second->name, n.second); // todo: should LocalNodes marked as deleted actually have been removed by now?  

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
            for (auto& n : ns) cout << " " << n.first;
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
            cout << identifier << " after matching " << matched << " child nodes [";
            for (auto& ml : matchedlist) cout << ml << " ";
            cout << "](with " << descendants << " descendants) in " << mn->path() << ", ended up with unmatched model nodes:";
            for (auto& m : ms) cout << " " << m.first;
            cout << " and unmatched filesystem paths:";
            for (auto& p : ps) cout << " " << p.second;
            cout << endl;
            return false;
        };
    }

    Node* getBaseNode()
    {
        if (Node* root = client.nodebyhandle(client.rootnodes[0]))
        {
            return client.childnodebyname(root, "mega_test_sync", false);
        }
        return nullptr;
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

    bool confirmModel(int syncid, Model::ModelNode* mnode)
    {
        auto si = syncSet.find(syncid);
        if (si == syncSet.end())
        {
            cout << "syncid " << syncid << " not found " << endl;
            return false;
        }

        // compare model aganst nodes representing remote state
        int descendants = 0;
        if (!recursiveConfirm(mnode, client.nodebyhandle(si->second.h), descendants, "Sync " + to_string(syncid), 0))
        {
            cout << "syncid " << syncid << " comparison against remote nodes failed" << endl;
            return false;
        }

        // compare model against LocalNodes
        descendants = 0; 
        if (Sync* sync = syncByTag(syncid))
        {
            if (!recursiveConfirm(mnode, &sync->localroot, descendants, "Sync " + to_string(syncid), 0))
            {
                cout << "syncid " << syncid << " comparison against LocalNodes failed" << endl;
                return false;
            }
        }

        // compare model against local filesystem
        descendants = 0;
        if (!recursiveConfirm(mnode, si->second.localpath, descendants, "Sync " + to_string(syncid), 0))
        {
            cout << "syncid " << syncid << " comparison against local filesystem failed" << endl;
            return false;
        }

        return true;
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

    void deleteremote(string path, promise<bool>& pb )
    {
        Node* n = getBaseNode();
        while (n && !path.empty())
        {
            auto pos = path.find("/");
            n = client.childnodebyname(n, path.substr(0, pos).c_str(), false);
            path.erase(0, pos == string::npos ? path.size() : pos + 1);
        }
        if (n)
        {
            resultproc.prepresult(UNLINK, [this, &pb](error e) { pb.set_value(!e); });
            client.unlink(n);
        }
        else
        {
            pb.set_value(false);
        }
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
                lock_guard g(StandardClient::om);
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
                break;

            Sleep(500);
        }

    }

    bool login_reset(const string& user, const string& pw)
    {
        future<bool> p1;
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

    bool login_fetchnodes(const string& user, const string& pw)
    {
        future<bool> p2;
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
        future<bool> p2;
        p2 = thread_do([=](StandardClient& sc, promise<bool>& pb) { sc.loginFromSession(session, pb); });
        if (!waitonresults(&p2)) return false;
        
        assert(!onFetchNodes);
        onFetchNodes = [=](StandardClient& mc, promise<bool>& pb)
        {
            promise<bool> tp;
            mc.ensureTestBaseFolder(false, tp);
            pb.set_value(tp.get_future().get() ? mc.setupSync_inthread(syncid, remotesyncrootfolder, localsyncpath) : false);
        };

        p2 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.fetchnodes(pb); });
        if (!waitonresults(&p2)) return false;
        p2 = thread_do([](StandardClient& sc, promise<bool>& pb) { sc.ensureTestBaseFolder(false, pb); });
        if (!waitonresults(&p2)) return false;
        return true;
    }

    bool setupSync_mainthread(const std::string& localsyncrootfolder, const std::string& remotesyncrootfolder, int syncid)
    {
        fs::path syncdir = fsBasePath / localsyncrootfolder;
        fs::create_directory(syncdir);
        future<bool> fb = thread_do([=](StandardClient& mc, promise<bool>& pb) { pb.set_value(mc.setupSync_inthread(syncid, remotesyncrootfolder, syncdir)); });
        return fb.get();
    }

    bool confirmModel_mainthread(Model::ModelNode* mnode, int syncid)
    {
        future<bool> fb;
        fb = thread_do([syncid, mnode](StandardClient& sc, promise<bool>& pb) { pb.set_value(sc.confirmModel(syncid, mnode)); });
        return fb.get();
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
                if (!(mc.client.todebris.empty() && mc.client.tounlink.empty() && mc.client.synccreate.empty()))
                {
                    any_add_del = true;
                }
            });
        }

        bool allactive = true;
        {
            //lock_guard g(StandardClient::om);
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

        if (onelastsyncdown && (chrono::steady_clock::now() - start + 1s) > d)
        {
            // synced folders that were removed remotely don't have the corresponding local folder removed unless we prompt an extra syncdown.  // todo:  do we need to fix
            for (auto vn : v) if (vn) vn->client.syncdownrequired = true;
            onelastsyncdown = false;
        }

        for (auto vn : v) if (vn)
        {
            if (allactive && ((chrono::steady_clock::now() - start) > d) && ((chrono::steady_clock::now() - vn->lastcb) > d))
                return;
        }

        Sleep(400);
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
        newpath = trashpath / (p.filename().stem().u8string() + "_" + to_string(i) + p.extension().u8string());
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

bool buildLocalFolders(fs::path targetfolder, const string& prefix, int n, int recurselevel, int filesperfolder)
{
    if (suppressfiles) filesperfolder = 0;

    fs::path p = targetfolder / prefix;
    if (!fs::create_directory(p))
        return false;

    for (int i = 0; i < filesperfolder; ++i)
    {
        string filename = "file" + to_string(i) + "_" + prefix;
        fs::path fp = p / filename;
        ofstream fs(fp.string());
        fs << filename;
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

GTEST_TEST(BasicSync, DelRemoteFolder)
{
    // delete a remote folder and confirm the client sending the request and another also synced both correctly update the disk
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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
    waitonsyncs(4s, &clientA1, &clientA2);

    // check everything matches in both syncs (model has expected state of remote and local)
    ASSERT_TRUE(model.movetosynctrash("f/f_2/f_2_1", "f"));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}

GTEST_TEST(BasicSync, DelLocalFolder)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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
    waitonsyncs(4s, &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(model.movetosynctrash("f/f_2/f_2_1", "f"));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
    ASSERT_TRUE(model.removesynctrash("f"));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
}

GTEST_TEST(BasicSync, MoveLocalFolder)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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

GTEST_TEST(BasicSync, MoveLocalFolderBetweenSyncs)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2
    StandardClient clientA3(localtestroot, "clientA3");   // user 1 client 3

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
    ASSERT_TRUE(clientA3.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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



GTEST_TEST(BasicSync, AddLocalFolder)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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
    waitonsyncs(120s, &clientA1, &clientA2);  // two minutes should be long enough to get past API_ETEMPUNAVAIL == -18 for sync2 downloading the files uploaded by sync1

    // check everything matches (model has expected state of remote and local)
    model.findnode("f/f_2")->addkid(model.buildModelSubdirs("newkid", 2, 2, 2));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    model.ensureLocalDebrisTmpLock("f"); // since we downloaded files
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}


GTEST_TEST(BasicSync, MAX_NEWNODES1)
{
    // create more nodes than we can upload in one putnodes.
    // this tree is 5x5 and the algorithm ends up creating nodes one at a time so it's pretty slow (and doesn't hit MAX_NEWNODES as a result)
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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
    waitonsyncs(300s, &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    model.findnode("f")->addkid(model.buildModelSubdirs("g", 5, 5, 0));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}

GTEST_TEST(BasicSync, MAX_NEWNODES2)
{
    // create more nodes than we can upload in one putnodes.
    // this tree is 5x5 and the algorithm ends up creating nodes one at a time so it's pretty slow (and doesn't hit MAX_NEWNODES as a result)
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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
    waitonsyncs(300s, &clientA1, &clientA2);

    // check everything matches (model has expected state of remote and local)
    model.findnode("f")->addkid(model.buildModelSubdirs("g", 3000, 1, 0));
    ASSERT_TRUE(clientA1.confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}


GTEST_TEST(BasicSync, MoveExistingIntoNewLocalFolder)
{
    // historic case:  in the local filesystem, create a new folder then move an existing file/folder into it
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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

GTEST_TEST(BasicSync, MoveSeveralExistingIntoDeepNewLocalFolders)
{
    // historic case:  in the local filesystem, create a new folder then move an existing file/folder into it
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset_makeremotenodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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


GTEST_TEST(BasicSync, SyncDuplicateNames)
{
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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
}

GTEST_TEST(BasicSync, RemoveLocalNodeBeforeSessionResume)
{
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    auto pclientA1 = make_unique<StandardClient>(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(pclientA1->login_reset_makeremotenodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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
    byte session[64];
    int sessionsize = pclientA1->client.dumpsession(session, sizeof session);

    // logout (but keep caches)
    fs::path sync1path = pclientA1->syncSet[1].localpath;
    pclientA1.reset();

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

/*
// SN tagging needed for this one

GTEST_TEST(BasicSync, RemoteFolderCreationRaceSamename)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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
}
*/

/*
// SN tagging needed for this one

GTEST_TEST(BasicSync, LocalFolderCreationRaceSamename)
{
    // confirm change is synced to remote, and also seen and applied in a second client that syncs the same folder
    // SN tagging needed for this one
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    StandardClient clientA1(localtestroot, "clientA1");   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(clientA1.login_reset("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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
}
*/

GTEST_TEST(BasicSync, ResumeSyncFromSessionAfterNonclashingLocalAndRemoteChanges )
{
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    unique_ptr<StandardClient> pclientA1(new StandardClient(localtestroot, "clientA1"));   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(pclientA1->login_reset_makeremotenodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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

    // save session A1
    byte session[64];
    int sessionsize = pclientA1->client.dumpsession(session, sizeof session);

    // logout A1 (but keep caches on disk)
    fs::path sync1path = pclientA1->syncSet[1].localpath;
    pclientA1.reset();

    // add remote folders via A2
    future<bool> p1 = clientA2.thread_do([](StandardClient& sc, promise<bool>& pb) { sc.makeCloudSubdirs("newremote", 2, 2, pb, "f/f_1/f_1_0"); });
    model1.findnode("f/f_1/f_1_0")->addkid(model1.buildModelSubdirs("newremote", 2, 2, 0));
    model2.findnode("f/f_1/f_1_0")->addkid(model2.buildModelSubdirs("newremote", 2, 2, 0));
    ASSERT_TRUE(waitonresults(&p1));

    // remove remote folders via A2
    p1 = clientA2.thread_do([](StandardClient& sc, promise<bool>& pb) { sc.deleteremote("f/f_0", pb); });
    model1.movetosynctrash("f/f_0", "f");
    model2.movetosynctrash("f/f_0", "f");
    ASSERT_TRUE(waitonresults(&p1));

    // add local folders in A1
    ASSERT_TRUE(buildLocalFolders(sync1path / "f_1/f_1_2", "newlocal", 2, 2, 2));
    model1.findnode("f/f_1/f_1_2")->addkid(model1.buildModelSubdirs("newlocal", 2, 2, 0));
    model2.findnode("f/f_1/f_1_2")->addkid(model2.buildModelSubdirs("newlocal", 2, 2, 0));

    // remove local folders in A1
    error_code e;
    ASSERT_TRUE(fs::remove_all(sync1path / "f_2", e) != static_cast<std::uintmax_t>(-1)) << e;
    model1.removenode("f/f_2");
    model2.movetosynctrash("f/f_2", "f");

    // get sync2 activity out of the way
    waitonsyncs(4s, &clientA2);

    // resume A1 session (with sync), see if A2 nodes and localnodes get in sync again
    pclientA1.reset(new StandardClient(localtestroot, "clientA1"));
    ASSERT_TRUE(pclientA1->login_fetchnodes_resumesync(string((char*)session, sessionsize), sync1path.u8string(), "f", 1));
    ASSERT_EQ(pclientA1->basefolderhandle, clientA2.basefolderhandle);
    waitonsyncs(60s, pclientA1.get(), &clientA2);
    waitonsyncs(60s, pclientA1.get(), &clientA2);

    // check everything matches (model has expected state of remote and local)
    ASSERT_TRUE(pclientA1->confirmModel_mainthread(model1.findnode("f"), 1));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model2.findnode("f"), 2));
}

GTEST_TEST(BasicSync, ResumeSyncFromSessionAfterClashingLocalAddRemoteDelete)
{
    fs::path localtestroot = makeNewTestRoot("c:\\tmp\\synctests");
    unique_ptr<StandardClient> pclientA1(new StandardClient(localtestroot, "clientA1"));   // user 1 client 1
    StandardClient clientA2(localtestroot, "clientA2");   // user 1 client 2

    ASSERT_TRUE(pclientA1->login_reset_makeremotenodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1", "f", 3, 3));
    ASSERT_TRUE(clientA2.login_fetchnodes("MEGAAUTOTESTUSER1", "MEGAAUTOTESTPWD1"));
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
    byte session[64];
    int sessionsize = pclientA1->client.dumpsession(session, sizeof session);
    fs::path sync1path = pclientA1->syncSet[1].localpath;

    // logout A1 (but keep caches on disk)
    pclientA1.reset();

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
    model.findnode("f/f_1/f_1_2")->addkid(model.buildModelSubdirs("newlocal", 2, 2, 0));
    ASSERT_TRUE(model.movetosynctrash("f/f_1", "f"));
    ASSERT_TRUE(pclientA1->confirmModel_mainthread(model.findnode("f"), 1));
    ASSERT_TRUE(model.removesynctrash("f", "f_1/f_1_2/newlocal"));
    ASSERT_TRUE(clientA2.confirmModel_mainthread(model.findnode("f"), 2));
}



class MegaCLILogger : public ::mega::Logger {
public:
    virtual void log(const char *time, int loglevel, const char *source, const char *message)
    {
#ifdef _WIN32
        OutputDebugStringA(message);
        OutputDebugStringA("\r\n");
#endif

        if (loglevel <= logWarning)
        {
            std::cout << message << std::endl;
        }
    }
};

MegaCLILogger logger;
};  // unnamed namespace

int main (int argc, char *argv[])
{
    remove("synctests.log");

#ifdef _WIN32
    SimpleLogger::setLogLevel(logDebug);  // warning and stronger to console; info and weaker to VS output window
    SimpleLogger::setOutputClass(&logger);
#else
    SimpleLogger::setAllOutputs(&std::cout);
#endif


#if defined(WIN32) && defined(NO_READLINE)
    wc = new CONSOLE_CLASS;
    wc->setShellConsole();
#endif

    ::testing::InitGoogleTest(&argc, argv);
    auto x = RUN_ALL_TESTS();
    return x;
}
