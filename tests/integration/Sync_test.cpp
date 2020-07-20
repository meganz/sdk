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

std::vector<uint8_t> randomData(const std::size_t length)
{
    std::vector<uint8_t> data(length);

    std::generate_n(data.begin(), data.size(), [](){ return (uint8_t)std::rand(); });

    return data;
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
        bool changed = false;

        ModelNode() = default;

        ModelNode(const ModelNode& other)
          : type(other.type)
          , name(other.name)
          , data(other.data)
          , kids()
          , parent()
          , changed(other.changed)
        {
            for (auto& child : other.kids)
            {
                addkid(std::make_unique<ModelNode>(*child));
            }
        }

        void generate(const fs::path& path)
        {
            const fs::path ourPath = path / name;

            if (type == file)
            {
                if (changed)
                {
                    ASSERT_TRUE(createFile(ourPath, data));
                    changed = false;
                }
            }
            else
            {
                fs::create_directory(ourPath);

                for (auto& child : kids)
                {
                    child->generate(ourPath);
                }
            }
        }

        string path() 
        {
            string s;
            for (auto p = this; p; p = p->parent) 
                s = "/" + p->name + s;
            return s;
        }

        ModelNode* addkid()
        {
            return addkid(std::make_unique<ModelNode>());
        }

        ModelNode* addkid(unique_ptr<ModelNode>&& p)
        {
            p->parent = this;
            kids.emplace_back(move(p));

            return kids.back().get();
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

    unique_ptr<ModelNode> makeModelSubfile(const string& u8name, const string& data)
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

    Model()
      : root(makeModelSubfolder("root"))
    {
    }

    Model(const Model& other)
      : root(std::make_unique<ModelNode>(*other.root))
    {
    }

    Model& operator=(const Model &rhs)
    {
        Model temp(rhs);

        swap(temp);

        return *this;
    }

    ModelNode* addfile(const string& path,
                       const void* data,
                       const size_t data_length)
    {
        ModelNode* node = addnode(path, ModelNode::file);

        node->data.resize(data_length);
        memcpy(node->data.data(), data, data_length);

        node->changed = true;

        return node;
    }

    ModelNode* addfile(const string& path, const string& data)
    {
        return addfile(path, data.data(), data.size());
    }

    ModelNode* addfile(const string& path, const vector<uint8_t>& data)
    {
        return addfile(path, data.data(), data.size());
    }

    ModelNode* addfile(const string& path)
    {
        return addfile(path, path);
    }

    ModelNode* addfolder(const string& path)
    {
        return addnode(path, ModelNode::folder);
    }

    ModelNode* addnode(const string& path, ModelNode::nodetype type)
    {
        ModelNode* child;
        ModelNode* node = root.get();
        string name;
        size_t current = 0;
        size_t end = path.size();

        while (current < end)
        {
            size_t delimiter = path.find('/', current);

            if (delimiter == path.npos)
            {
                break;
            }

            name = path.substr(current, delimiter - current);

            if (!(child = childnodebyname(node, name)))
            {
                child = node->addkid();

                child->name = name;
                child->type = ModelNode::folder;
            }

            assert(child->type == ModelNode::folder);

            current = delimiter + 1;
            node = child;
        }

        assert(current < end);

        name = path.substr(current);

        if (!(child = childnodebyname(node, name)))
        {
            child = node->addkid();

            child->name = name;
            child->type = type;
        }

        assert(child->type == type);

        return child;
    }

    ModelNode* copynode(const string& src, const string& dst)
    {
        const ModelNode* source = findnode(src);
        ModelNode* destination = addnode(dst, source->type);

        destination->data = source->data;
        destination->kids.clear();

        for (auto& child : source->kids)
        {
            destination->addkid(std::make_unique<ModelNode>(*child));
        }

        return destination;
    }

    void generate(const fs::path& path)
    {
        fs::create_directories(path);

        for (auto& child : root->kids)
        {
            child->generate(path);
        }
    }

    void swap(Model& other)
    {
        using std::swap;

        swap(root, other.root);
    }

    unique_ptr<ModelNode> root;
};

template<class T>
struct PrinterTraits;

template<>
struct PrinterTraits<Model::ModelNode>
{
    using NodeType = Model::ModelNode;

    static bool attached(const NodeType&)
    {
        return true;
    }

    static vector<NodeType*> children(const NodeType& node)
    {
        vector<NodeType*> result;

        for (auto& child : node.kids)
        {
            result.emplace_back(child.get());
        }

        return result;
    }

    static bool ignored(const NodeType&)
    {
        return false;
    }

    static string name(const NodeType& node)
    {
        return node.name;
    }

    static string type()
    {
        return "ModelNode";
    };
}; /* PrinterTraits<Model::ModelNode> */

template<>
struct PrinterTraits<Node>
{
    using NodeType = Node;

    static bool attached(const NodeType& node)
    {
        return node.localnode
               && node.localnode->node == &node;
    }

    static vector<NodeType*> children(const NodeType& node)
    {
        vector<NodeType*> result;

        for (auto& child : node.children)
        {
            result.emplace_back(child);
        }

        return result;
    }

    static bool ignored(const NodeType&)
    {
        return false;
    }

    static string name(const NodeType& node)
    {
        return node.displayname();
    }

    static string type()
    {
        return "Node";
    }
}; /* PrinterTraits<Node> */

template<>
struct PrinterTraits<LocalNode>
{
    using NodeType = LocalNode;

    static bool attached(const NodeType& node)
    {
        return node.node
               && node.node->localnode == &node;
    }

    static vector<NodeType*> children(const NodeType& node)
    {
        vector<NodeType*> result;

        for (auto& child_it : node.children)
        {
            result.emplace_back(child_it.second);
        }

        return result;
    };

    static bool ignored(const NodeType& node)
    {
        return node.excluded();
    }

    static string name(const NodeType& node)
    {
        return node.name;
    };

    static string type()
    {
        return "LocalNode";
    }
}; /* PointerTraits<LocalNode> */

class Printer
{
public:
    template<class T>
    void operator()(const T& node) const
    {
        print(node);
    }

    template<class T>
    void print(const T& node) const
    {
        generateGraph(node);
    }

private:
    template<class T>
    void generateEdgeDef(const T& from, const T& to) const
    {
        cout << "\t"
             << id(from)
             << " -> "
             << id(to)
             << ";\n";
    }

    template<class T>
    void generateEdgeDefs(const T& node) const
    {
        const auto children = PrinterTraits<T>::children(node);

        for (auto& child : children)
        {
            generateEdgeDef(node, *child);
        }

        for (auto& child : children)
        {
            generateEdgeDefs(*child);
        }
    }

    template<class T>
    void generateGraph(const T& node) const
    {
        cout << "DOTBEGIN: "
             << PrinterTraits<T>::type()
             << "\n"
             << "digraph {\n";

        generateNodeDefs(node);
        generateEdgeDefs(node);

        cout << "}\n"
             << "DOTEND\n";
    }

    template<class T>
    void generateNodeDef(const T& node) const
    {
        bool isAttached =
          PrinterTraits<T>::attached(node);
        bool isIgnored =
          PrinterTraits<T>::ignored(node);

        cout << "\t"
             << id(node)
             << " [ label = \""
             << PrinterTraits<T>::name(node)
             << ":a"
             << isAttached
             << ",i"
             << isIgnored
             << "\" ]\n";
    }

    template<class T>
    void generateNodeDefs(const T& node) const
    {
        generateNodeDef(node);

        for (auto& child : PrinterTraits<T>::children(node))
        {
            generateNodeDefs(*child);
        }
    }

    template<class T>
    uintptr_t id(const T& node) const
    {
        return reinterpret_cast<uintptr_t>(&node);
    }
}; /* Printer */

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

    string ensureDir(const fs::path& p)
    {
        fs::create_directories(p);

        string result = p.u8string();

        if (result.back() != fs::path::preferred_separator)
        {
            result += fs::path::preferred_separator;
        }

        return result;
    }

    StandardClient(const fs::path& basepath, const string& name)
        : client_dbaccess_path(ensureDir(basepath / name / ""))
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

    void localLogout(const bool clearCache = false)
    {
        thread_do([=](MegaClient& mc, promise<bool>&) {
            #ifdef _WIN32
                // logout stalls in windows due to the issue above
                mc.purgenodesusersabortsc(false);
            #else
                mc.locallogout(clearCache);
            #endif
        });
    }

    static mutex om;
    bool logcb = false;
    chrono::steady_clock::time_point lastcb = std::chrono::steady_clock::now();

    string lp(LocalNode* ln) { return ln->getLocalPath().toName(*client.fsaccess, FS_UNKNOWN); }

    void onCallback() { lastcb = chrono::steady_clock::now(); };

    void syncupdate_state(Sync*, syncstate_t state) override { if (logcb) { lock_guard<mutex> g(om);  cout << clientname << " syncupdate_state() " << state << endl; } onCallback(); }
    void syncupdate_scanning(bool b) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_scanning()" << b << endl; } onCallback(); }
    //void syncupdate_local_folder_addition(Sync* s, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_local_folder_addition() " << lp(ln) << " " << cp << endl; } onCallback(); }
    //void syncupdate_local_folder_deletion(Sync*, LocalNode* ln) override { if (logcb) { lock_guard<mutex> g(om);  cout << clientname << " syncupdate_local_folder_deletion() " << lp(ln) << endl; } onCallback(); }
    void syncupdate_local_folder_addition(Sync*, LocalNode* ln, const char* cp) override { onCallback(); }
    void syncupdate_local_folder_deletion(Sync*, LocalNode* ln) override { onCallback(); }
    void syncupdate_local_file_addition(Sync*, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_local_file_addition() " << lp(ln) << " " << cp << endl; } onCallback(); }
    void syncupdate_local_file_deletion(Sync*, LocalNode* ln) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_local_file_deletion() " << lp(ln) << endl; } onCallback(); }
    void syncupdate_local_file_change(Sync*, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_local_file_change() " << lp(ln) << " " << cp << endl; } onCallback(); }
    void syncupdate_local_move(Sync*, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_local_move() " << lp(ln) << " " << cp << endl; } onCallback(); }
    void syncupdate_local_lockretry(bool b) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_local_lockretry() " << b << endl; } onCallback(); }
    //void syncupdate_get(Sync*, Node* n, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_get()" << n->displaypath() << " " << cp << endl; } onCallback(); }
    void syncupdate_put(Sync*, LocalNode* ln, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_put()" << lp(ln) << " " << cp << endl; } onCallback(); }
    void syncupdate_remote_file_addition(Sync*, Node* n) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_file_addition() " << n->displaypath() << endl; } onCallback(); }
    void syncupdate_remote_file_deletion(Sync*, Node* n) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_file_deletion() " << n->displaypath() << endl; } onCallback(); }
    //void syncupdate_remote_folder_addition(Sync*, Node* n) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_folder_addition() " << n->displaypath() << endl; } onCallback(); }
    //void syncupdate_remote_folder_deletion(Sync*, Node* n) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_folder_deletion() " << n->displaypath() << endl; } onCallback(); }
    void syncupdate_remote_folder_addition(Sync*, Node* n) override { onCallback(); }
    void syncupdate_remote_folder_deletion(Sync*, Node* n) override { onCallback(); }
    void syncupdate_remote_copy(Sync*, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_copy() " << cp << endl; } onCallback(); }
    void syncupdate_remote_move(Sync*, Node* n1, Node* n2) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_move() " << n1->displaypath() << " " << n2->displaypath() << endl; } onCallback(); }
    void syncupdate_remote_rename(Sync*, Node* n, const char* cp) override { if (logcb) { lock_guard<mutex> g(om); cout << clientname << " syncupdate_remote_rename() " << n->displaypath() << " " << cp << endl; } onCallback(); }
    //void syncupdate_treestate(LocalNode* ln) override { if (logcb) { lock_guard<mutex> g(om);   cout << clientname << " syncupdate_treestate() " << ln->ts << " " << ln->dts << " " << lp(ln) << endl; } onCallback(); }

    bool sync_syncable(Sync* sync, const char *name, LocalPath& localPath, Node*) override
    {
        return sync_syncable(sync, name, localPath);
    }

    bool sync_syncable(Sync*, const char *name, LocalPath& localPath) override
    {
        if (logcb)
        {
            lock_guard<mutex> guard(om);

            cout << clientname
                 << " sync_syncable(): name = "
                 << name
                 << ", localPath = "
                 << localPath.toPath(*client.fsaccess)
                 << endl;
        }

        return !wildcardMatch(name, mExcludedNames);
    }

    vector<string> mExcludedNames;
    
    std::atomic<unsigned> transfersAdded{0}, transfersRemoved{0}, transfersPrepared{0}, transfersFailed{0}, transfersUpdated{0}, transfersComplete{0};

    std::set<Transfer*> mTransfers;

    void transfer_added(Transfer* transfer) override
    {
        onCallback();

        mTransfers.emplace(transfer);

        ++transfersAdded;
    }

    void transfer_removed(Transfer* transfer) override
    {
        onCallback();

        mTransfers.erase(transfer);

        ++transfersRemoved;
    }

    void transfer_prepare(Transfer*) override
    {
        onCallback();
        ++transfersPrepared;
    }

    void transfer_failed(Transfer* transfer, const Error&, dstime = 0) override
    {
        onCallback();

        mTransfers.erase(transfer);

        ++transfersFailed;
    }

    void transfer_update(Transfer*) override
    {
        onCallback();
        ++transfersUpdated;
    }

    void transfer_complete(Transfer* transfer) override
    {
        onCallback();

        mTransfers.erase(transfer);

        ++transfersComplete;
    }

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

    enum resultprocenum
    {
        PRELOGIN,
        LOGIN,
        FETCHNODES,
        PUTNODES,
        UNLINK,
        MOVENODE,
        SETATTR
    };

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

        bool emittedFromSync(resultprocenum rpe) const
        {
            return rpe == MOVENODE
                   || rpe == SETATTR;
        }

        const char* resultFunction(resultprocenum rpe) const
        {
            switch (rpe)
            {
            case FETCHNODES:
                return "fetchnodes_result";
            case LOGIN:
                return "login_result";
            case MOVENODE:
                return "rename_result";
            case PRELOGIN:
                return "prelogin_result";
            case PUTNODES:
                return "putnodes_result";
            case SETATTR:
                return "setattr_result";
            case UNLINK:
                return "unlink_result";
            default:
                break;
            }

            assert(!"Unhandled result proc enumerant");

            // Silence the compiler.
            return "UNHANDLED";
        }

        void processresult(resultprocenum rpe, error e, handle h = UNDEF)
        {
            //cout << "procenum " << rpe << " result " << e << endl;
            auto& entry = m[rpe];

            // make sure this result doesn't originate from a sync.
            if (emittedFromSync(rpe)
                && (entry.empty() || entry.front().h != h))
            {
                cout << "received unsolicited "
                     << resultFunction(rpe)
                     << " call"
                     << endl;

                return;
            }

            assert(entry.size());

            entry.front().f(e);
            entry.pop_front();
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

        auto localpath = n->getLocalPath(false).toName(*client.fsaccess, FS_UNKNOWN);
        string n_localname = n->localname.toName(*client.fsaccess, FS_UNKNOWN);
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

            string parentpath = n->parent->getLocalPath(false).toName(*client.fsaccess, FS_UNKNOWN);
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
            LocalNode& child = *n2.second;

            if (!(child.deleted || child.excluded()))
            {
                // todo: should LocalNodes marked as deleted actually have been removed by now?
                ns.emplace(child.name, &child);
            }
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

            buffer.resize(mn->data.size());

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

            auto root = client.nodebyhandle(si->second.h);
            auto sync = syncByTag(syncid);

            assert(root);
            assert(sync);
            
            Printer p;

            p(*mnode);
            p(*sync->localroot);
            p(*root);

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

    void setattr_result(handle h, error e) override
    {
        resultproc.processresult(SETATTR, e, h);
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

    bool deleteremote(const string& path)
    {
        future<bool> result =
          thread_do([=](StandardClient& client, promise<bool>& result)
                    {
                        client.deleteremote(path, result);
                    });

        return result.get();
    }

    void deleteremote(string path, promise<bool>& pb )
    {
        if (Node* n = drillchildnodebyname(gettestbasenode(), path))
        {
            resultproc.prepresult(UNLINK, [&pb](error e) { pb.set_value(!e); });
            client.unlink(n);
        }
        else
        {
            pb.set_value(false);
        }
    }

    bool deleteremotedebris()
    {
        future<bool> result =
          thread_do([](StandardClient& client, promise<bool>& result)
                    {
                        client.deleteremotedebris(result);
                    });

        return result.get();
    }

    void deleteremotedebris(promise<bool>& result)
    {
        Node* debris =
          drillchildnodebyname(getcloudrubbishnode(), "SyncDebris");

        if (debris)
        {
            deleteremotenode(debris, result);
        }
        else
        {
            result.set_value(true);
        }
    }

    void deleteremotenode(Node* n, promise<bool>& pb)
    {
        deleteremotenodes(vector<Node*>(1, n), pb);
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

    bool movenode(const string& currentPath, const string& newParentPath)
    {
        future<bool> result =
          thread_do([=](StandardClient& client, promise<bool>& result)
                    {
                        client.movenode(currentPath, newParentPath, result);
                    });

        return result.get();
    }

    void movenode(string path, string newparentpath, promise<bool>& pb)
    {
        Node* n = drillchildnodebyname(gettestbasenode(), path);
        Node* p = drillchildnodebyname(gettestbasenode(), newparentpath);
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

    bool setattr(Node& node)
    {
        future<bool> result =
          thread_do([&](StandardClient& client, promise<bool>& result)
                    {
                        client.setattr(node, result);
                    });

        return result.get();
    }

    void setattr(Node& node, promise<bool>& result)
    {
        resultproc.prepresult(SETATTR,
                              [&](error e)
                              {
                                  result.set_value(!e);
                              },
                              node.nodehandle);

        client.setattr(&node);
    }

    bool putnodes(handle parentHandle, NewNode* newNodes, int numNodes)
    {
        future<bool> result =
          thread_do([=](StandardClient& client, promise<bool>& result)
                    {
                        client.putnodes(parentHandle, newNodes, numNodes, result);
                    });

        return result.get();
    }

    void putnodes(handle parentHandle,
                  NewNode* newNodes,
                  int numNodes,
                  promise<bool>& result)
    {
        resultproc.prepresult(PUTNODES,
                              [&](error e)
                              {
                                  result.set_value(!e);
                              });

        client.putnodes(parentHandle, newNodes, numNodes);
    }

    void putnodes_prepareOneFolder(NewNode& node, const string &name)
    {
        client.putnodes_prepareOneFolder(&node, name);
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

    bool login_reset()
    {
        return login_reset("MEGA_EMAIL", "MEGA_PWD");
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

    bool login_reset_makeremotenodes(const string& prefix, int depth = 0, int fanout = 0)
    {
        return login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", prefix, depth, fanout);
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

    bool login_fetchnodes(bool makeBaseFolder = false)
    {
        return login_fetchnodes("MEGA_EMAIL", "MEGA_PWD", makeBaseFolder);
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
    std::vector<StandardClient*> v{ c1, c2, c3, c4 };
    bool onelastsyncdown = true;
    bool last_add_del = false;
    bool last_all_idle = false;
    auto start = chrono::steady_clock::now();

    v.erase(std::remove(v.begin(), v.end(), nullptr), v.end());

    for (;;)
    {
        bool curr_add_del = false;
        bool curr_all_idle;
        bool changed;

        for (auto vn : v)
        {
            auto result = 
              vn->thread_do([&](StandardClient& sc, promise<bool>& result)
                            {
                                bool any_add_del = false;

                                for (auto& sync : sc.client.syncs)
                                {
                                    any_add_del |= sync->deleteq.size() > 0;
                                    any_add_del |= sync->deleteq.size() > 0;
                                }

                                any_add_del |= sc.client.nodenotify.size() > 0;
                                any_add_del |= sc.client.synccreate.size() > 0;
                                any_add_del |= sc.client.todebris.size() > 0;
                                any_add_del |= sc.client.tounlink.size() > 0;
                                any_add_del |= sc.client.transferlist.transfers[GET].size() > 0;
                                any_add_del |= sc.client.transferlist.transfers[PUT].size() > 0;

                                result.set_value(any_add_del);
                            });

            curr_add_del |= result.get();
        }

        changed = curr_add_del ^ last_add_del;

        if (curr_add_del || changed || StandardClient::debugging)
        {
            start = chrono::steady_clock::now();
        }

        if (onelastsyncdown && (chrono::steady_clock::now() - start + d/2) > d)
        {
            start = chrono::steady_clock::now();

            // synced folders that were removed remotely don't have the corresponding local folder removed unless we prompt an extra syncdown.  // todo:  do we need to fix
            for (auto vn : v)
            {
                vn->client.syncdownrequired = true;
            }

            onelastsyncdown = false;
        }

        curr_all_idle =
          std::all_of(v.begin(),
                      v.end(),
                      [&](StandardClient* sc) -> bool
                      {
                          auto now = chrono::steady_clock::now();
                          return (now - start) > d && (now - sc->lastcb) > d;
                      });

        changed = curr_all_idle ^ last_all_idle;
        last_all_idle = curr_all_idle;

        if (curr_all_idle && !changed)
        {
            return;
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
    fs::create_directories(p);
    assert(b);
    return p;
}

//std::atomic<int> fileSizeCount = 20;

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

GTEST_TEST(Sync, BasicSync_RenameLocalFile)
{
    static auto TIMEOUT = std::chrono::seconds(4);

    const fs::path root = makeNewTestRoot(LOCAL_TEST_FOLDER);

    // Primary client.
    StandardClient client0(root, "c0");
    // Observer.
    StandardClient client1(root, "c1");

    // Log callbacks.
    client0.logcb = true;
    client1.logcb = true;

    // Log clients in.
    ASSERT_TRUE(client0.login_reset_makeremotenodes("MEGA_EMAIL", "MEGA_PWD", "x", 0, 0));
    ASSERT_TRUE(client1.login_fetchnodes("MEGA_EMAIL", "MEGA_PWD"));
    ASSERT_EQ(client0.basefolderhandle, client1.basefolderhandle);

    // Set up syncs.
    ASSERT_TRUE(client0.setupSync_mainthread("s0", "x", 0));
    ASSERT_TRUE(client1.setupSync_mainthread("s1", "x", 1));

    // Wait for initial sync to complete.
    waitonsyncs(TIMEOUT, &client0, &client1);

    // Add x/f.
    ASSERT_TRUE(createFile(client0.syncSet[0].localpath, "f"));

    // Wait for sync to complete.
    waitonsyncs(TIMEOUT, &client0, &client1);

    // Confirm model.
    Model model;

    model.root->addkid(model.makeModelSubfolder("x"));
    model.findnode("x")->addkid(model.makeModelSubfile("f"));

    ASSERT_TRUE(client0.confirmModel_mainthread(model.findnode("x"), 0));
    ASSERT_TRUE(client1.confirmModel_mainthread(model.findnode("x"), 1, true));

    // Rename x/f to x/g.
    fs::rename(client0.syncSet[0].localpath / "f",
               client0.syncSet[0].localpath / "g");

    // Wait for sync to complete.
    waitonsyncs(TIMEOUT, &client0, &client1);

    // Update and confirm model.
    model.findnode("x/f")->name = "g";

    ASSERT_TRUE(client0.confirmModel_mainthread(model.findnode("x"), 0));
    ASSERT_TRUE(client1.confirmModel_mainthread(model.findnode("x"), 1, true));
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

class FilterFixture
  : public ::testing::Test
{
public:
    struct Client
      : public StandardClient
    {
        Client(const fs::path& basePath, const string& name)
          : StandardClient(basePath, name)
          , mOnFileAdded()
          , mOnFileComplete()
        {
            localNodesMustHaveNodes = false;
        };

        void file_added(File* file) override
        {
            if (mOnFileAdded)
            {
                mOnFileAdded(*file);
            }
        }

        void file_complete(File* file) override
        {
            if (mOnFileComplete)
            {
                mOnFileComplete(*file);
            }
        }

        Node* nodebyhandle(handle h)
        {
            return client.nodebyhandle(h);
        }

        void syncupdate_filter_error(Sync*, LocalNode* node) override
        {
            if (mOnFilterError)
            {
                mOnFilterError(*node);
            }
        }

        function<void(File&)> mOnFileAdded;
        function<void(File&)> mOnFileComplete;
        function<void(LocalNode&)> mOnFilterError;
    };

    struct LocalFSModel
      : public Model
    {
        LocalFSModel() = default;

        LocalFSModel(const Model& other)
          : Model(other)
        {
        }

        LocalFSModel &operator=(const Model& other)
        {
            Model::operator=(other);
            return *this;
        }
    }; /* LocalFSModel */

    struct LocalNodeModel
      : public Model
    {
        LocalNodeModel() = default;

        LocalNodeModel(const Model& other)
          : Model(other)
        {
        }

        LocalNodeModel& operator=(const Model& other)
        {
            Model::operator=(other);
            return *this;
        }
    }; /* LocalNodeModel */

    struct RemoteNodeModel
      : public Model
    {
        RemoteNodeModel() = default;

        RemoteNodeModel(const Model& other)
          : Model(other)
        {
        }

        RemoteNodeModel& operator=(const Model& other)
        {
            Model::operator=(other);
            return *this;
        }
    }; /* RemoteNodeModel */

    FilterFixture()
      : cd()
      , cdu()
      , cu()
    {
        const fs::path root = makeNewTestRoot(LOCAL_TEST_FOLDER);

        cd  = std::make_unique<Client>(root, "cd");
        cdu = std::make_unique<Client>(root, "cdu");
        cu  = std::make_unique<Client>(root, "cu");

        cd->logcb = true;
        cdu->logcb = true;
        cu->logcb = true;
    }

    bool confirm(Client& client,
                 LocalFSModel& model,
                 const int syncID = 0,
                 const bool ignoreDebris = true)
    {
        return client.confirmModel_mainthread(
                 model.root.get(),
                 syncID,
                 ignoreDebris,
                 StandardClient::CONFIRM_LOCALFS);
    }

    bool confirm(Client& client,
                 LocalNodeModel& model,
                 const int syncID = 0,
                 const bool ignoreDebris = true)
    {
        return client.confirmModel_mainthread(
                 model.root.get(),
                 syncID,
                 ignoreDebris,
                 StandardClient::CONFIRM_LOCALNODE);
    }

    bool confirm(Client& client,
                 Model& model,
                 const int syncID = 0,
                 const bool ignoreDebris = true)
    {
        return client.confirmModel_mainthread(
                 model.root.get(),
                 syncID,
                 ignoreDebris);
    }

    bool confirm(Client& client,
                 RemoteNodeModel& model,
                 const int syncID = 0,
                 const bool ignoreDebris = true)
    {
        return client.confirmModel_mainthread(
                 model.root.get(),
                 syncID,
                 ignoreDebris,
                 StandardClient::CONFIRM_REMOTE);
    }

    string debrisFilePath(const string& debrisName,
                          const string& path) const
    {
        ostringstream ostream;

        ostream << debrisName
                << "/"
                << todaysDate()
                << "/"
                << path;
              
        return ostream.str();
    }

    fs::path root(Client& client) const
    {
        return client.fsBasePath;
    }

    bool setupSync(Client& client,
                   const string& localFolder,
                   const int syncID = 0)
    {
        return setupSync(client, localFolder, client.clientname, syncID);
    }

    bool setupSync(Client& client,
                   const string& localFolder,
                   const string& remoteFolder,
                   const int syncID = 0)
    {
        return client.setupSync_mainthread(localFolder,
                                           remoteFolder,
                                           syncID);
    }

    string todaysDate() const
    {
        size_t minimumLength = strlen("yyyy-mm-dd");

        string result(minimumLength + 1, 'X');

        time_t rawTime = time(nullptr);
        tm* localTime = localtime(&rawTime);

        assert(strftime(&result[0], result.size(), "%F", localTime));
        result.resize(minimumLength);

        return result;
    }

    void waitOnSyncs(Client* c0,
                     Client* c1 = nullptr,
                     Client* c2 = nullptr)
    {
        static chrono::seconds timeout(4);

        waitonsyncs(timeout, c0, c1, c2);
    }

    // download client.
    std::unique_ptr<Client> cd;
    // download / upload client.
    std::unique_ptr<Client> cdu;
    // upload client.
    std::unique_ptr<Client> cu;
}; /* FilterFixture */

TEST_F(FilterFixture, CaseSensitiveFilter)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up filesystem.
    localFS.addfile("a/f");
    localFS.addfile("a/g");
    localFS.addfile("b/F");
    localFS.addfile("b/G");
    localFS.addfile(".megaignore", "-G:f\n-:g\n");
    localFS.generate(root(*cu) / "root");

    // Set up local node tree.
    localTree = localFS;
    localTree.removenode("a/f");
    localTree.removenode("a/g");
    localTree.removenode("b/G");

    // Remote node tree is consistent with local node tree.
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for synchronization.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(FilterFixture, FilterChangeWhileDownloading)
{
    // Random file data.
    const auto data = randomData(16384);

    // Ignore file data.
    const string ignoreFile = "-:f";
   
    // Set up cloud.
    {
        // Build and generate model.
        Model model;

        model.addfile("f", data);
        model.generate(root(*cu) / "root");

        // Log in client.
        ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));

        // Add and start sync.
        ASSERT_TRUE(setupSync(*cu, "root", "x"));

        // Wait for synchronization to complete.
        waitOnSyncs(cu.get());

        // Confirm model.
        ASSERT_TRUE(confirm(*cu, model));

        // Log out client.
        cu.reset();
    }

    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up local FS.
    localFS.addfile(".megaignore", ignoreFile);
    localFS.addfile("f", data);

    // Set up local model.
    localTree = localFS;
    localTree.removenode("f");
    
    // Set up remote model.
    remoteTree = localFS;

    // Log in client.
    ASSERT_TRUE(cdu->login_fetchnodes());

    // Set download speed limit at 1kbps.
    cdu->client.setmaxdownloadspeed(1024);

    // Exclude "x" once it begins downloading.
    cdu->mOnFileAdded =
      [&](File& file)
      {
          string name;

          file.displayname(&name);

          if (name != "f")
          {
              return;
          }

          ASSERT_TRUE(createFile(root(*cdu) / "root" / ".megaignore",
                                 ignoreFile.data(),
                                 ignoreFile.size()));
      };

    // Remove download limit once .megaignore is uploaded.
    cdu->mOnFileComplete =
      [&](File& file)
      {
          string name;

          file.displayname(&name);

          // Make sure .megaignore completes first.
          ASSERT_TRUE(name == ".megaignore"
                      || cdu->client.getmaxdownloadspeed() == 0);

          // Remove limit when .megaignore completes.
          if (name == ".megaignore")
          {
              cdu->client.setmaxdownloadspeed(0);
          }
      };

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cdu, "root", "x"));

    // Wait for synchronization to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

TEST_F(FilterFixture, FilterChangeWhileUploading)
{
    // Random file data.
    const auto data = randomData(16384);

    // Ignore file data.
    const string ignoreFile = "-:f";

    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up local FS.
    localFS.addfile("f");
    localFS.generate(root(*cdu) / "root");
    localFS.addfile(".megaignore", ignoreFile);

    // Set up local tree.
    localTree = localFS;
    localTree.removenode("f");

    // Set up remote tree.
    remoteTree = localFS;

    // Log in client.
    ASSERT_TRUE(cdu->login_reset_makeremotenodes("x"));

    // Set upload speed limit to 1kbps.
    cdu->client.setmaxuploadspeed(1024);
    
    cdu->mOnFileAdded =
      [&](File& file)
      {
          string name;

          file.displayname(&name);

          // remove speed limit when .megaignore starts uploading.
          if (name == ".megaignore")
          {
              cdu->client.setmaxuploadspeed(0);
          }

          // create .megaignore when f starts uploading.
          if (name == "f")
          {
              ASSERT_TRUE(createFile(root(*cdu) / "root" / ".megaignore",
                                     ignoreFile.data(),
                                     ignoreFile.size()));
          }
      };

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cdu, "root", "x"));

    // Wait for synchronization to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

TEST_F(FilterFixture, GlobalFilter)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Configure global filters.
    cu->mExcludedNames.emplace_back("*~");

    // Setup local FS.
    localFS.addfile(".megaignore", "+:b~");
    localFS.addfile("d/a~");
    localFS.addfile("d/b~");
    localFS.addfile("a~");
    localFS.addfile("b~");
    localFS.generate(root(*cu) / "root");

    // Setup local and remote trees.
    localTree = localFS;
    localTree.removenode("d/a~");
    localTree.removenode("a~");
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(FilterFixture, NameFilter)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile(".megaignore",
                    // exclude all *.n* in the tree.
                    "-:*.n*\n"
                    // include all *.ni in the tree.
                    "+:*.ni\n"
                    // include all *.nN in the root.
                    "+N:*.nN\n"
                    // exclude all *.X* in the root.
                    "-N:*.X*\n"
                    // include all *.Xi in the root.
                    "+N:*.Xi\n");

    // excluded by -:*.n*
    localFS.addfile("d/df.n");
    // included by +:*.ni
    localFS.addfile("d/df.ni");
    // excluded by -:*.n*
    localFS.addfile("d/df.nN");
    // included as no matching exclusion rule.
    localFS.addfile("d/df.X");
    // excluded by -:*.n*
    localFS.addfile("f.n");
    // included by +:*.ni
    localFS.addfile("f.ni");
    // excluded by -:*.n*
    localFS.addfile("f.nN");
    // excluded by -N:*.X*
    localFS.addfile("f.X");
    // included by +N:*.Xi
    localFS.addfile("f.Xi");
    // excluded by -:*.n*
    localFS.addfile("d.n/f.ni");

    localFS.generate(root(*cu) / "root");

    // Setup local tree.
    localTree = localFS;

    localTree.removenode("d/df.n");
    localTree.removenode("d/df.nN");
    localTree.removenode("f.n");
    localTree.removenode("f.X");
    localTree.removenode("d.n");

    // Setup remote tree.
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for sync to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(FilterFixture, PathFilter)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile(".megaignore",
                    // exclude path d*/d*
                    "-p:d*/d*\n"
                    // include path di*/di*
                    "+p:di*/di*\n"
                    // include path dL
                    "+p:dL\n"
                    // include everything under dJ
                    "+p:dJ*\n");

    // excluded by -p:d*/d*
    localFS.addfile("d/d/f");
    // included as no matching rule.
    localFS.addfile("d/f");
    // included by +p:di*/di*
    localFS.addfile("di/di/f");
    // included as no matching rule.
    localFS.addfile("di/f");
    // excluded by -p:d*/d*
    localFS.addfile("dL/d/f");
    // included by +p:dL
    localFS.addfile("dL/f");
    // included by +p:dJ*
    localFS.addfile("dJ/d/f");
    localFS.addfile("dJ/f");

    localFS.generate(root(*cu) / "root");

    // Setup local tree.
    localTree = localFS;
    localTree.removenode("d/d");
    localTree.removenode("dL/d");

    // Setup remote tree.
    remoteTree = localTree;
        
    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(FilterFixture, TargetSpecificFilter)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up local filesystem.
    {
        const string ignoreFile =
          // Exclude directories matching *a.
          "-d:*a\n"
          // Exclude files matching *b.
          "-f:*b\n"
          // Exclude anything matching *c.
          "-:*c\n"
          // Include everything containing an x.
          "+:*x*\n";

        localFS.addfile("da/fa", "fa");
        localFS.addfile("da/fb", "fb");
        localFS.addfile("da/fc", "fc");
        localFS.addfile("da/fxb", "fxb");
        localFS.addfile("da/fxc", "fxc");
        localFS.addfile(".megaignore", ignoreFile);
        localFS.addfile("fa");
        localFS.addfile("fb");
        localFS.addfile("fxb");
        localFS.addfile("fc");
        localFS.addfile("fxc");
        localFS.copynode("da", "db");
        localFS.copynode("da", "dc");
        localFS.copynode("da", "dxa");
        localFS.copynode("da", "dxc");

        localFS.generate(root(*cu) / "root");
    }

    // Set up local node tree.
    localTree = localFS;

    // Excluded by -d:*a
    localTree.removenode("da");

    // Excluded by -f:*b
    localTree.removenode("db/fb");
    localTree.removenode("dxa/fb");
    localTree.removenode("dxc/fb");
    localTree.removenode("fb");

    // Excluded by -:*c
    localTree.removenode("db/fc");
    localTree.removenode("dc");
    localTree.removenode("dxa/fc");
    localTree.removenode("dxc/fc");
    localTree.removenode("fc");

    // Remote node tree is consistent with local node tree.
    remoteTree = localTree;

    // Log in the client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start the sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(FilterFixture, ToggleFunctionality)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup global filters.
    // These are always active.
    cdu->mExcludedNames.emplace_back("*~");

    // Setup models.
    localFS.addfile("d/f");
    localFS.addfile("g");
    localFS.addfile("h~");
    localFS.addfile(".megaignore", "-:d\n-:g\n");
    localFS.generate(root(*cdu) / "root");

    localTree = localFS;
    localTree.removenode("h~");
    remoteTree = localTree;

    // Disable ignore file functionality.
    // (Shortcut here as the client's not active.)
    cdu->client.ignoreFilesEnabled = false;

    // Log in the client.
    ASSERT_TRUE(cdu->login_reset_makeremotenodes(cdu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cdu, "root"));

    // Wait for sync.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Enable ignore file functionality.
    cdu->thread_do([](MegaClient& client, promise<bool>&)
                   {
                       // MegaApi performs these steps.
                       client.ignoreFilesEnabled = true;
                       client.restoreFilterState();
                   });
    
    // Filters are applied.
    // d and g are no longer visible.
    localTree.removenode("d");
    localTree.removenode("g");

    // Wait for sync.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Disable ignore file functionality.
    cdu->thread_do([](MegaClient& client, promise<bool>&)
                   {
                       client.ignoreFilesEnabled = false;
                       client.purgeFilterState();
                   });

    // Once again, everything's included except for those nodes excluded by
    // global filters.
    localTree = localFS;
    localTree.removenode("h~");

    // Wait for sync.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

TEST_F(FilterFixture, TriggersFilterErrorEvent)
{
    Model model;

    // Set up model.
    model.addfile(".megaignore", "bad");
    model.generate(root(*cu) / "root");

    auto eventHandler =
      [&](LocalNode& node)
      {
          const string expectedName =
            (root(*cu) / "root").u8string();

          // Node should be the ignore file's parent.
          if (expectedName == node.name)
          {
              cu->mOnFilterError = nullptr;
          }
      };

    // Log in the client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Hook filter_error event.
    cu->mOnFilterError = eventHandler;

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Let the sync try and synchronize.
    waitOnSyncs(cu.get());

    // Was the event triggered during initial scan?
    ASSERT_FALSE(cu->mOnFilterError);

    // Correct the ignore file.
    model.addfile(".megaignore", "#");
    model.generate(root(*cu) / "root");

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get());

    // Confirm model.
    ASSERT_TRUE(confirm(*cu, model));
    
    // Hook the event.
    cu->mOnFilterError = eventHandler;

    // Break the ignore file again.
    model.addfile(".megaignore", "verybad");
    model.generate(root(*cu) / "root");

    // Let the sync think.
    waitOnSyncs(cu.get());

    // Was the event triggered during normal operation?
    ASSERT_FALSE(cu->mOnFilterError);

    // Do we only trigger the event when there's no existing error?
    cu->mOnFilterError = eventHandler;

    model.addfile(".megaignore", "reallybad");
    model.generate(root(*cu) / "root");

    // Wait for sync to idle.
    waitOnSyncs(cu.get());

    // The event shouldn't have been triggered.
    ASSERT_TRUE(cu->mOnFilterError);
}

class LocalToCloudFilterFixture
  : public FilterFixture
{
public:
    string debrisFilePath(const string& path) const
    {
        return FilterFixture::debrisFilePath("SyncDebris", path);
    }
}; /* LocalToCloudFilterFixture */

TEST_F(LocalToCloudFilterFixture, DoesntDownloadIgnoredNodes)
{
    // Set up cloud.
    {
        Model model;

        model.addfile("d/f");
        model.addfile("f");
        model.generate(root(*cu) / "root");

        ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));
        ASSERT_TRUE(setupSync(*cu, "root", "x"));
        waitOnSyncs(cu.get());

        ASSERT_TRUE(confirm(*cu, model));

        cu.reset();
    }
    
    // Set up local FS.
    LocalFSModel localFS;

    localFS.addfile(".megaignore", "-:d\n-:f\n");
    localFS.generate(root(*cd) / "root");

    // Set up local and remote trees.
    LocalNodeModel localTree = localFS;
    RemoteNodeModel remoteTree = localFS;

    localTree.removenode("d");
    localTree.removenode("f");

    remoteTree.addfile("d/f");
    remoteTree.addfile("f");

    // Log in client.
    ASSERT_TRUE(cd->login_fetchnodes());

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cd, "root", "x"));

    // Wait for sync and confirm models.
    waitOnSyncs(cd.get());

    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, DoesntDownloadWhenBlocked)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up cloud.
    {
        Model model;

        // Populate FS.
        model.addfile("da/fa");
        model.addfile("da/fb");
        model.addfile("db/fa");
        model.addfile("db/fb");
        model.addfile("fa");
        model.addfile("fb");
        model.generate(root(*cu) / "root");

        // Log in upload client.
        ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));

        // Add and start sync.
        ASSERT_TRUE(setupSync(*cu, "root", "x"));

        // Wait for upload to complete.
        waitOnSyncs(cu.get());

        // Make sure everything's where it should be.
        ASSERT_TRUE(confirm(*cu, model));

        // Set up remote tree.
        remoteTree = model;
    }

    // Set up local FS.
    localFS.addfile(".megaignore", "bad");
    localFS.generate(root(*cd) / "root");

    // Local tree is consistent with local FS.
    localTree = localFS;

    // Log in client.
    ASSERT_TRUE(cd->login_fetchnodes());

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cd, "root", "x"));

    // Let the sync try and synchronize.
    waitOnSyncs(cd.get());

    // Verify models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, DoesntMoveIgnoredNodes)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile("0/fx");
    localFS.addfolder("1");
    localFS.generate(root(*cu) / "root");

    // Setup local node tree.
    localTree = localFS;

    // Setup remote note tree.
    remoteTree = localFS;

    // Log in the client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for sync to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Filter out 0/fx.
    localFS.addfile("0/.megaignore", "-:*x");
    localFS.generate(root(*cu) / "root");

    // 0/fx should remain in the cloud.
    // 1/fx should be added to the cloud.
    remoteTree = localFS;
    remoteTree.copynode("0/fx", "1/fx");

    // 0/fx should become 1/fx in both local models.
    localFS.copynode("0/fx", "1/fx");
    localFS.removenode("0/fx");
    localTree = localFS;

    // Rename 0/fx to 1/fx.
    fs::rename(root(*cu) / "root" / "0"/ "fx",
               root(*cu) / "root" / "1"/ "fx");

    // Wait for sync to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, DoesntMoveWhenBlocked)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up local FS.
    localFS.addfile("a/.megaignore", "#");
    localFS.addfile("a/fa");
    localFS.addfile("a/fb");
    localFS.addfolder("b");
    localFS.generate(root(*cu) / "root");

    // Set up local and remote trees.
    localTree = localFS;
    remoteTree = localFS;

    // Log in the client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start the sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for our tree to upload.
    waitOnSyncs(cu.get());

    // Make sure everything looks as we expect.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Break the ignore file.
    // Move a/fa to b/fa.
    localFS.addfile("a/.megaignore", "bad");
    localFS.generate(root(*cu) / "root");
    localFS.movenode("a/fa", "b");

    fs::rename(root(*cu) / "root" / "a" / "fa",
               root(*cu) / "root" / "b" / "fa");

    // Local tree should remain consistent with FS.
    localTree = localFS;

    // Remote tree should have both a/fa and b/fa.
    remoteTree.copynode("a/fa" , "b/fa");

    // Try and synchronize.
    waitOnSyncs(cu.get());

    // Do the models agree?
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Move a to b/a.
    localFS.movenode("a", "b");

    fs::rename(root(*cu) / "root" / "a",
               root(*cu) / "root" / "b" / "a");

    // Original a will be from the cloud.
    localFS.addfile("a/.megaignore", "#");
    localFS.addfile("a/fa");
    localFS.addfile("a/fb");

    // Local tree should remain consistent with FS.
    localTree = localFS;

    // Remote tree should have both a and b/a.
    remoteTree.copynode("a", "b/a");

    // But not b/a/.megaignore, b/a/fa or b/a/fb.
    remoteTree.removenode("b/a/.megaignore");
    remoteTree.removenode("b/a/fa");
    remoteTree.removenode("b/a/fb");

    // Give client some time to try and synchronize.
    waitOnSyncs(cu.get());

    // Do the models agree?
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, DoesntRenameIgnoredNodes)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile("fx");
    localFS.generate(root(*cu) / "root");

    // Setup local node tree.
    localTree = localFS;

    // Setup remote note tree.
    remoteTree = localFS;

    // Log in the client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for sync to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Filter out fx.
    localFS.addfile(".megaignore", "-:*x");
    localFS.generate(root(*cu) / "root");

    // fu should be added to the cloud.
    // fx should remain in the cloud.
    remoteTree = localFS;
    remoteTree.copynode("fx", "fu");

    // fx should beecome fu in both local models.
    localFS.copynode("fx", "fu");
    localFS.removenode("fx");
    localTree = localFS;

    // Rename fx to fu.
    fs::rename(root(*cu) / "root" / "fx",
               root(*cu) / "root" / "fu");

    // Wait for sync to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, DoesntRenameWhenBlocked)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up local FS.
    localFS.addfile("d/f");
    localFS.addfile("f");
    localFS.generate(root(*cdu) / "root");

    // Local tree is consistent with local FS.
    localTree = localFS;

    // Remote tree is consistent with local tree.
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cdu->login_reset_makeremotenodes(cdu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cdu, "root"));

    // Wait for sync to complete.
    waitOnSyncs(cdu.get());

    // Make sure everything uploaded.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Add a broken ignore file.
    localFS.addfile(".megaignore", "bad");
    localFS.generate(root(*cdu) / "root");

    // Rename d to dd.
    localFS.copynode("d", "dd");

    fs::rename(root(*cdu) / "root" / "d",
               root(*cdu) / "root" / "dd");

    // d will not be redownloaded as the tree is blocked.
    localFS.removenode("d");

    // Rename f to ff.
    localFS.copynode("f", "ff");

    fs::rename(root(*cdu) / "root" / "f",
               root(*cdu) / "root" / "ff");

    // f will not be redownloaded as the tree is blocked.
    localFS.removenode("f");

    // Local tree is somewhat stale as the tree is blocked.
    // It is unchanged except for the .megaignore file.
    localTree.addfile(".megaignore", "bad");

    // Let the sync think for awhile.
    waitOnSyncs(cdu.get());

    // Confirm the models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    //ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, DoesntRubbishIgnoredNodes)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile("fx");
    localFS.generate(root(*cu) / "root");

    // Setup local node tree.
    localTree = localFS;

    // Setup remote note tree.
    remoteTree = localFS;

    // Log in the client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for sync to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Filter out fx.
    localFS.addfile(".megaignore", "-:*x");
    localFS.generate(root(*cu) / "root");

    // fx should remain in the cloud.
    remoteTree = localFS;

    // fx should no longer be visible in ether local model.
    localFS.removenode("fx");
    localTree = localFS;

    // Remove fx from the FS.
    ASSERT_TRUE(fs::remove(root(*cu) / "root" / "fx"));

    // Wait for sync to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, DoesntRubbishWhenBlocked)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up local FS.
    localFS.addfile("d/f");
    localFS.addfile("f");
    localFS.generate(root(*cdu) / "root");

    // Local tree is consistent with local FS.
    localTree = localFS;

    // Remote node tree is consistent with local node tree.
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cdu->login_reset_makeremotenodes(cdu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cdu, "root"));

    // Wait for uploads to complete.
    waitOnSyncs(cdu.get());

    // Check that everything's as we expect.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Add a broken ignore file.
    localFS.addfile(".megaignore", "bad");
    localFS.generate(root(*cdu) / "root");

    // Remove d.
    localFS.removenode("d");
    fs::remove_all(root(*cdu) / "root" / "d");

    // Remove f.
    localFS.removenode("f");
    fs::remove(root(*cdu) / "root" / "f");

    // Local tree still contains d and f.
    // Local tree now contains .megaignore.
    // Remote tree is unchanged.
    localTree.addfile(".megaignore", "bad");

    // Try and synchronize changes.
    waitOnSyncs(cdu.get());

    // Confirm models.
    // d and f should still be present in the cloud.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, DoesntUploadIgnoredNodes)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfolder("du");
    localFS.addfolder("dx");
    localFS.addfile("fu");
    localFS.addfile("fx");
    localFS.addfile(".megaignore", "-:*x");
    localFS.generate(root(*cu) / "root");

    // Setup local tree
    localTree = localFS;
    localTree.removenode("dx");
    localTree.removenode("fx");

    // Setup remote tree.
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get());

    // Confirm model expectations.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, DoesntUploadWhenBlocked)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    fs::create_directories(root(*cu) / "root");

    ASSERT_TRUE(setupSync(*cu, "root"));

    // Get initial sync scan out of the way.
    waitOnSyncs(cu.get());

    // Set up local FS.
    localFS.addfile("0/.megaignore", "bad");
    localFS.generate(root(*cu) / "root");

    // Local tree is consistent with FS.
    localTree = localFS;

    // Remote tree contains 0 but none of its contents.
    remoteTree = localTree;
    remoteTree.removenode("0/.megaignore");

    // Try and synchronize.
    waitOnSyncs(cu.get());

    // Is everything as we'd expect?
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, FilterAdded)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile("fu");
    localFS.addfile("fx");
    localFS.generate(root(*cu) / "root");

    // Setup local and remote trees.
    localTree = localFS;
    remoteTree = localFS;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for and confirm sync.
    waitOnSyncs(cu.get());

    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Add filter.
    localFS.addfile(".megaignore", "-:*x");
    localFS.addfile("fxx");
    localFS.generate(root(*cu) / "root");

    // fx+ should not be visible in local tree.
    localTree = localFS;
    localTree.removenode("fx");
    localTree.removenode("fxx");

    // fxx should not be visible in remote tree.
    remoteTree = localFS;
    remoteTree.removenode("fxx");

    // Wait for and confirm sync.
    waitOnSyncs(cu.get());

    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, FilterChanged)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile(".megaignore", "-:*x");
    localFS.addfile("fu");
    localFS.addfile("fx");
    localFS.generate(root(*cu) / "root");

    // Setup local and remote trees.
    localTree = localFS;
    localTree.removenode("fx");
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for and confirm sync.
    waitOnSyncs(cu.get());

    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Update filter.
    localFS.addfile(".megaignore", "-:*u");
    localFS.generate(root(*cu) / "root");

    // fu should be ignored.
    // fx should not be ignored.
    localTree = localFS;
    localTree.removenode("fu");

    // f[ux] should both be present in remote tree.
    remoteTree = localFS;

    // Wait for and confirm sync.
    waitOnSyncs(cu.get());

    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, FilterDeferredChange)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile("0/.megaignore", "-:f");
    localFS.addfile("0/f");
    localFS.addfile("1/.megaignore", "-:g");
    localFS.addfile("1/g");
    localFS.addfile(".megaignore", "-:?");
    localFS.generate(root(*cu) / "root");

    // Setup local and remote trees.
    localTree = localFS;
    localTree.removenode("0");
    localTree.removenode("1");
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for sync and confirm models.
    waitOnSyncs(cu.get());

    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Change 0/.megaignore.
    // Filter reload will be deferred.
    localFS.addfile("0/.megaignore", "#-:f");
    localFS.generate(root(*cu) / "root");

    // Remove 1/.megaignore.
    // Filter clear will be deferred.
    localFS.removenode("1/.megaignore");

    ASSERT_TRUE(fs::remove(root(*cu) / "root" / "1" / ".megaignore"));

    // Wait for sync.
    // This should be a no-op as our changes are to ignored nodes.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Remove .megaignore.
    // This should perform any pending filter reloads.
    ASSERT_TRUE(fs::remove(root(*cu) / "root" / ".megaignore"));

    // Update models.
    localFS.removenode(".megaignore");

    localTree = localFS;
    remoteTree = localFS;

    // Wait for sync.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, FilterMovedAcrossHierarchy)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile("0/.megaignore", "-:x");
    localFS.addfile("0/u");
    localFS.addfile("0/x");
    localFS.addfile("1/u");
    localFS.addfile("1/x");
    localFS.generate(root(*cu) / "root");

    // Setup local and remote trees.
    localTree = localFS;
    localTree.removenode("0/x");
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for sync and confirm models.
    waitOnSyncs(cu.get());

    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Move 0/.megaignore to 1.
    fs::rename(root(*cu) / "root" / "0" / ".megaignore",
               root(*cu) / "root" / "1" / ".megaignore");

    localFS.movenode("0/.megaignore", "1");

    // Update local and remote trees.
    localTree = localFS;
    localTree.removenode("1/x");
    remoteTree = localFS;

    // Wait for synchronization.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, FilterMovedBetweenSyncs)
{
    LocalFSModel s0LocalFS;
    LocalFSModel s1LocalFS;
    LocalNodeModel s0LocalTree;
    LocalNodeModel s1LocalTree;
    RemoteNodeModel s0RemoteTree;
    RemoteNodeModel s1RemoteTree;

    // Sync 0
    {
        // Set up local FS.
        s0LocalFS.addfile(".megaignore", "-:x");
        s0LocalFS.addfile("x");
        s0LocalFS.generate(root(*cdu) / "s0");

        // Set up local tree.
        s0LocalTree = s0LocalFS;
        s0LocalTree.removenode("x");

        // Set up remote tree.
        s0RemoteTree = s0LocalTree;
    }

    // Sync 1
    {
        // Set up local FS.
        s1LocalFS.addfile("x");
        s1LocalFS.generate(root(*cdu) / "s1");

        // Set up local and remote trees.
        s1LocalTree = s1LocalFS;
        s1RemoteTree = s1LocalTree;
    }

    // Log in client.
    ASSERT_TRUE(cdu->login_reset());

    // Create sync directories.
    {
        // Will be freed by putnodes_result(...).
        NewNode* nodes = new NewNode[2];

        cdu->putnodes_prepareOneFolder(nodes[0], "s0");
        cdu->putnodes_prepareOneFolder(nodes[1], "s1");

        Node* root = cdu->gettestbasenode();

        ASSERT_TRUE(cdu->putnodes(root->nodehandle, nodes, 2));

        ASSERT_TRUE(cdu->drillchildnodebyname(root, "s0"));
        ASSERT_TRUE(cdu->drillchildnodebyname(root, "s1"));
    }

    // Add and start syncs.
    ASSERT_TRUE(setupSync(*cdu, "s0", "s0", 0));
    ASSERT_TRUE(setupSync(*cdu, "s1", "s1", 1));

    // Wait for synchronization to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, s0LocalFS, 0));
    ASSERT_TRUE(confirm(*cdu, s0LocalTree, 0));
    ASSERT_TRUE(confirm(*cdu, s0RemoteTree, 0));

    ASSERT_TRUE(confirm(*cdu, s1LocalFS, 1));
    ASSERT_TRUE(confirm(*cdu, s1LocalTree, 1));
    ASSERT_TRUE(confirm(*cdu, s1RemoteTree, 1));

    // Move cdu/s0/.megaignore to cdu/s1/.megaignore.
    fs::rename(root(*cdu) / "s0" / ".megaignore",
               root(*cdu) / "s1" / ".megaignore");

    // Wait for synchronization to complete.
    waitOnSyncs(cdu.get());

    // .megaignore no longer exists in cdu/s0.
    // as a consequence, cdu/s0/x is no longer ignored.
    s0LocalFS.removenode(".megaignore");

    s0LocalTree.removenode(".megaignore");
    s0LocalTree.addfile("x");

    s0RemoteTree = s0LocalTree;

    // .megaignore has been added to cdu/s1.
    // as a consequence, cdu/s1/x is now ignored.
    s1LocalFS.addfile(".megaignore", "-:x");

    s1LocalTree = s1LocalFS;
    s1LocalTree.removenode("x");

    s1RemoteTree = s1LocalFS;

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, s0LocalFS, 0));
    ASSERT_TRUE(confirm(*cdu, s0LocalTree, 0));
    ASSERT_TRUE(confirm(*cdu, s0RemoteTree, 0));

    ASSERT_TRUE(confirm(*cdu, s1LocalFS, 1));
    ASSERT_TRUE(confirm(*cdu, s1LocalTree, 1));
    ASSERT_TRUE(confirm(*cdu, s1RemoteTree, 1));

    // Add a new .megaignore to cdu/s0.
    // Add cdu/s0/y for it to ignore.
    s0LocalFS.addfile(".megaignore", "-:y");
    s0LocalFS.addfile("y");
    s0LocalFS.generate(root(*cdu) / "s0");

    s0LocalTree = s0LocalFS;
    s0LocalTree.removenode("y");
    s0RemoteTree = s0LocalTree;

    // Add cdu/s1/y.
    s1LocalFS.addfile("y");
    s1LocalFS.generate(root(*cdu) / "s1");

    s1LocalTree.addfile("y");
    s1RemoteTree = s1LocalFS;

    // Wait for synchronization to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, s0LocalFS, 0));
    ASSERT_TRUE(confirm(*cdu, s0LocalTree, 0));
    ASSERT_TRUE(confirm(*cdu, s0RemoteTree, 0));

    ASSERT_TRUE(confirm(*cdu, s1LocalFS, 1));
    ASSERT_TRUE(confirm(*cdu, s1LocalTree, 1));
    ASSERT_TRUE(confirm(*cdu, s1RemoteTree, 1));

    // Move cdu/s0/.megaignore to cdu/s1/.megaignore.
    fs::rename(root(*cdu) / "s0" / ".megaignore",
               root(*cdu) / "s1" / ".megaignore");

    // Wait for synchronization to complete.
    waitOnSyncs(cdu.get());

    // .megaignore no longer exists in cdu/s0.
    // as a consequence, cdu/s0/y is no longer ignored.
    s0LocalFS.removenode(".megaignore");

    s0LocalTree.removenode(".megaignore");
    s0LocalTree.addfile("y");

    s0RemoteTree = s0LocalTree;

    // cdu/s1/.megaignore has been overwritten.
    // as a consequence, cdu/s1/x is no longer ignored.
    // as a consequence, cdu/s1/y is ignored.
    s1LocalFS.addfile(".megaignore", "-:y");

    s1LocalTree = s1LocalFS;
    s1LocalTree.removenode("y");

    s1RemoteTree = s1LocalFS;

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, s0LocalFS, 0));
    ASSERT_TRUE(confirm(*cdu, s0LocalTree, 0));
    ASSERT_TRUE(confirm(*cdu, s0RemoteTree, 0));

    ASSERT_TRUE(confirm(*cdu, s1LocalFS, 1));
    ASSERT_TRUE(confirm(*cdu, s1LocalTree, 1));
    ASSERT_TRUE(confirm(*cdu, s1RemoteTree, 1));
}

TEST_F(LocalToCloudFilterFixture, FilterMovedDownHierarchy)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile(".megaignore", "-:x");
    localFS.addfile("0/u");
    localFS.addfile("0/x");
    localFS.addfile("1/u");
    localFS.addfile("1/x");
    localFS.generate(root(*cu) / "root");

    // Setup local and remote trees.
    localTree = localFS;
    localTree.removenode("0/x");
    localTree.removenode("1/x");
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for sync and confirm models.
    waitOnSyncs(cu.get());

    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Move 0/.megaignore to root.
    fs::rename(root(*cu) / "root" / ".megaignore",
               root(*cu) / "root" / "0" / ".megaignore");

    localFS.movenode(".megaignore", "0");

    // 1/x is now visible in the local and remote trees.
    localTree = localFS;
    localTree.removenode("0/x");
    remoteTree = localTree;

    // Wait for synchronization.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, FilterMovedUpHierarchy)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile("0/.megaignore", "-:x");
    localFS.addfile("0/u");
    localFS.addfile("0/x");
    localFS.addfile("1/u");
    localFS.addfile("1/x");
    localFS.generate(root(*cu) / "root");

    // Setup local and remote trees.
    localTree = localFS;
    localTree.removenode("0/x");
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for sync and confirm models.
    waitOnSyncs(cu.get());

    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Move 0/.megaignore to root.
    fs::rename(root(*cu) / "root" / "0" / ".megaignore",
               root(*cu) / "root" / ".megaignore");

    localFS.movenode("0/.megaignore", "");

    // [01]/x is now ignored in the local tree.
    localTree = localFS;
    localTree.removenode("0/x");
    localTree.removenode("1/x");

    // all nodes except for 0/x are visible in the remote tree.
    remoteTree = localFS;
    remoteTree.removenode("0/x");

    // Wait for synchronization.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, FilterOverwritten)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile(".megaignore", "-:*x");
    localFS.addfile("fu");
    localFS.addfile("fx");
    localFS.addfile("megaignore", "-:*u");
    localFS.generate(root(*cu) / "root");

    // Setup local and remote trees.
    localTree = localFS;
    localTree.removenode("fx");
    remoteTree = localTree;

    // Log in to client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Move megaignore over .megaignore
    fs::rename(root(*cu) / "root" / "megaignore",
               root(*cu) / "root" / ".megaignore");

    localFS.removenode(".megaignore");
    localFS.copynode("megaignore", ".megaignore");
    localFS.removenode("megaignore");

    // fu should become ignored.
    // fx should become visible in local tree.
    localTree = localFS;
    localTree.removenode("fu");

    // f[ux] should be visible in the remote tree.
    remoteTree = localFS;

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, FilterRemoved)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile(".megaignore", "-:*x");
    localFS.addfile("fx");
    localFS.generate(root(*cu) / "root");

    // Setup local and remote trees.
    localTree = localFS;
    localTree.removenode("fx");
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for and confirm sync.
    waitOnSyncs(cu.get());

    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Remove filter from FS.
    localFS.removenode(".megaignore");

    ASSERT_TRUE(fs::remove(root(*cu) / "root" / ".megaignore"));

    // fx should be present in both local and remote trees.
    localTree = localFS;
    remoteTree = localFS;

    // Wait for and confirm sync.
    waitOnSyncs(cu.get());

    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

TEST_F(LocalToCloudFilterFixture, MoveToIgnoredRubbishesRemote)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile("0/f");
    localFS.addfile("1/.megaignore", "-:f");
    localFS.generate(root(*cu) / "root");

    // Setup local and remote tree.
    localTree = localFS;
    remoteTree = localFS;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Ensure remote debris is clear.
    ASSERT_TRUE(cu->deleteremotedebris());

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for sync to complete and confirm models.
    waitOnSyncs(cu.get());

    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Move 0/f to 1/f.
    fs::rename(root(*cu) / "root" / "0" / "f",
               root(*cu) / "root" / "1" / "f");

    localFS.movenode("0/f", "1");

    // Neither 0/f or 1/f are present in local or remote tree.
    localTree = localFS;
    localTree.removenode("1/f");
    remoteTree = localTree;

    // Wait for sync to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Verify that 0/f was moved into the remote debris.
    Node* u = cu->drillchildnodebyname(cu->getcloudrubbishnode(),
                                       debrisFilePath("f"));
    ASSERT_TRUE(u);
}

TEST_F(LocalToCloudFilterFixture, RenameToIgnoredRubbishesRemote)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile(".megaignore", "-:x");
    localFS.addfile("u");
    localFS.generate(root(*cu) / "root");

    // Setup local and remote tree.
    localTree = localFS;
    remoteTree = localFS;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Ensure remote debris is clear.
    ASSERT_TRUE(cu->deleteremotedebris());

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for sync to complete and confirm models.
    waitOnSyncs(cu.get());

    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Rename u to x.
    fs::rename(root(*cu) / "root" / "u",
               root(*cu) / "root" / "x");

    localFS.copynode("u", "x");
    localFS.removenode("u");

    // u is no longer present in either the local or remote tree.
    localTree.removenode("u");
    remoteTree = localTree;

    // Wait for sync to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Verify that u was moved into the remote debris.
    Node* u = cu->drillchildnodebyname(cu->getcloudrubbishnode(),
                                       debrisFilePath("u"));
    ASSERT_TRUE(u);
}

TEST_F(LocalToCloudFilterFixture, UnblocksWhenIgnoreFileCorrected)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Log in client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes(cu->clientname));

    // Add and start sync.
    fs::create_directories(root(*cu) / "root");
    ASSERT_TRUE(setupSync(*cu, "root"));

    // Wait for initial scan to complete.
    waitOnSyncs(cu.get());

    // Set up local FS.
    localFS.addfile(".megaignore", "bad");
    localFS.addfile("d/f");
    localFS.addfile("f");
    localFS.generate(root(*cu) / "root");

    // Local tree only contains the ignore file.
    // Remote tree contains nothing.
    localTree.addfile(".megaignore", "bad");

    // Try and synchronize.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Correct ignore file.
    localFS.addfile(".megaignore", "-:f");
    localFS.generate(root(*cu) / "root");

    // Local tree contains d.
    localTree.addfile(".megaignore", "-:f");
    localTree.addfolder("d");

    // Remote node tree matches local node tree.
    remoteTree = localTree;

    // Wait for sync to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Break the ignore file again.
    localFS.addfile(".megaignore", "bad");
    localFS.generate(root(*cu) / "root");

    // Try and synchronize.
    waitOnSyncs(cu.get());

    // Remove the ignore file.
    localFS.removenode(".megaignore");
    fs::remove(root(*cu) / "root" / ".megaignore");

    // Local tree is now consistent with FS.
    localTree = localFS;

    // Remote tree now contains d and f.
    remoteTree = localTree;

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));
}

class CloudToLocalFilterFixture
  : public FilterFixture
{
public:
    string debrisFilePath(const string& path) const
    {
        return FilterFixture::debrisFilePath(MEGA_DEBRIS_FOLDER, path);
    }
}; /* CloudToLocalFilterFixture */

TEST_F(CloudToLocalFilterFixture, DoesntDownloadIgnoredNodes)
{
    // Set up cloud.
    {
        Model model;

        model.addfile(".megaignore", "-:f");
        model.addfile("d/f");
        model.addfile("d/g");
        model.addfile("f");
        model.addfile("g");
        model.generate(root(*cu) / "root");

        // Disable filtering functionality.
        cu->client.ignoreFilesEnabled = false;

        // Upload.
        ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));
        ASSERT_TRUE(setupSync(*cu, "root", "x"));

        waitOnSyncs(cu.get());

        // Confirm.
        ASSERT_TRUE(confirm(*cu, model));

        // Logout.
        cu.reset();
    }

    // Set up models.
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    localFS.addfile(".megaignore", "-:f");
    localFS.addfile("d/g");
    localFS.addfile("g");

    localTree = localFS;

    remoteTree = localFS;
    remoteTree.addfile("d/f");
    remoteTree.addfile("d/g");
    remoteTree.addfile("f");
    remoteTree.addfile("g");

    // Log in client.
    ASSERT_TRUE(cd->login_fetchnodes());

    // Add and start sync.
    fs::create_directories(root(*cd) / "root");
    ASSERT_TRUE(setupSync(*cd, "root", "x"));

    // Wait for synchronization to complete.
    waitOnSyncs(cd.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, DoesntDownloadWhenBlocked)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up cloud.
    {
        Model model;

        model.addfile(".megaignore", "bad");
        model.addfile("d/f");
        model.addfile("f");
        model.generate(root(*cu) / "root");

        // Disable ignore file functionality.
        cu->client.ignoreFilesEnabled = false;

        // Log in client.
        ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));

        // Add and start sync.
        ASSERT_TRUE(setupSync(*cu, "root", "x"));
        
        // Wait for upload to complete.
        waitOnSyncs(cu.get());

        // Make sure everything's where it should be.
        ASSERT_TRUE(confirm(*cu, model));

        // Log out client.
        cu.reset();

        // Remote tree is consistent with model.
        remoteTree = model;
    }

    // Set up local FS.
    localFS.addfile(".megaignore", "bad");

    // Local node tree is consistent with local FS.
    localTree = localFS;

    // Log in client.
    ASSERT_TRUE(cdu->login_fetchnodes());

    // Add and start sync.
    fs::create_directories(root(*cdu) / "root");
    ASSERT_TRUE(setupSync(*cdu, "root", "x"));

    // Try and synchronize.
    waitOnSyncs(cdu.get());

    // Sync should be blocked on the ignore file.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, DoesntMoveIgnoredNodes)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile("d/fx");
    localFS.generate(root(*cdu) / "root");

    // Setup local node tree.
    localTree = localFS;

    // Setup remote note tree.
    remoteTree = localFS;

    // Log in the client.
    ASSERT_TRUE(cdu->login_reset_makeremotenodes(cdu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cdu, "root"));

    // Wait for sync to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Filter out fx.
    localFS.addfile(".megaignore", "-:*x");
    localFS.generate(root(*cdu) / "root");

    // fx should remain in the cloud.
    remoteTree = localFS;

    // fx should now be ignored in the local tree.
    localTree = localFS;
    localTree.removenode("d/fx");

    // Wait for sync to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Move cdu/d/fx to cdu/fx.
    {
        ASSERT_TRUE(cu->login_fetchnodes());
        ASSERT_TRUE(cu->movenode("cdu/d/fx", "cdu"));

        cu.reset();
    }

    // Update models.
    remoteTree.movenode("d/fx", "");

    // Wait for sync to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, DoesntMoveWhenBlocked)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up local filesystem.
    localFS.addfile("da/f");
    localFS.addfile("f");
    localFS.addfolder("db");
    localFS.generate(root(*cdu) / "root");

    // Local node tree matches local FS.
    localTree = localFS;

    // Remote node tree matches local node tree.
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cdu->login_reset_makeremotenodes(cdu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cdu, "root"));

    // Wait for synchronization to complete.
    waitOnSyncs(cdu.get());

    // Everything as we expect?
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Add a broken ignore file.
    localFS.addfile(".megaignore", "bad");
    localFS.generate(root(*cdu) / "root");

    // Local node tree matches local FS.
    localTree = localFS;

    // Let the sync try and synchronize.
    waitOnSyncs(cdu.get());

    {
        ASSERT_TRUE(cu->login_fetchnodes());

        // Move cdu/da to cdu/db/da.
        remoteTree.movenode("da", "db");
        ASSERT_TRUE(cu->movenode("cdu/da", "cdu/db"));

        // Move cdu/f to cdu/db/f.
        remoteTree.movenode("f", "db");
        ASSERT_TRUE(cu->movenode("cdu/f", "cdu/db"));

        cu.reset();
    }

    // Wait for sync.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, DoesntRenameIgnoredNodes)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile("x");
    localFS.generate(root(*cdu) / "root");

    // Setup local node tree.
    localTree = localFS;

    // Setup remote note tree.
    remoteTree = localFS;

    // Log in the client.
    ASSERT_TRUE(cdu->login_reset_makeremotenodes(cdu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cdu, "root"));

    // Wait for sync to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Filter out fx.
    localFS.addfile(".megaignore", "-:x");
    localFS.generate(root(*cdu) / "root");

    // x should remain in the cloud.
    remoteTree = localFS;

    // x should now be ignored in the local tree.
    localTree = localFS;
    localTree.removenode("x");

    // Wait for sync to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Rename cdu/x to cdu/y.
    {
        ASSERT_TRUE(cu->login_fetchnodes());

        Node* node =
          cu->drillchildnodebyname(cu->gettestbasenode(), "cdu/x");
        ASSERT_TRUE(node);

        node->attrs.map['n'] = "y";
        ASSERT_TRUE(cu->setattr(*node));

        cu.reset();
    }

    // Update models.
    localFS.addfile("y", "x");
    localTree.addfile("y", "x");
    remoteTree.copynode("x", "y");
    remoteTree.removenode("x");

    // Wait for sync to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, DoesntRenameWhenBlocked)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up local filesysten.
    localFS.addfile("d/f");
    localFS.addfile("f");
    localFS.generate(root(*cdu) / "root");

    // Local tree matches local FS.
    localTree = localFS;

    // Remote tree matches local tree.
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cdu->login_reset_makeremotenodes(cdu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cdu, "root"));

    // Wait for synchronization to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Add a broken ignore file.
    localFS.addfile(".megaignore", "bad");
    localFS.generate(root(*cdu) / "root");

    // Local tree is consistent with local FS.
    localTree = localFS;

    // Let the sync try and complete.
    waitOnSyncs(cdu.get());

    // Remotely rename cdu/d, cdu/f.
    {
        ASSERT_TRUE(cu->login_fetchnodes());

        // Remotely rename cdu/d to cdu/dd.
        Node* node =
          cu->drillchildnodebyname(cu->gettestbasenode(), "cdu/d");
        ASSERT_TRUE(node);

        node->attrs.map['n'] = "dd";
        ASSERT_TRUE(cu->setattr(*node));

        remoteTree.copynode("d", "dd");
        remoteTree.removenode("d");

        // Remotely rename cdu/f to cdu/ff.
        node = cu->drillchildnodebyname(cu->gettestbasenode(), "cdu/f");
        ASSERT_TRUE(node);

        node->attrs.map['n'] = "ff";
        ASSERT_TRUE(cu->setattr(*node));

        remoteTree.copynode("f", "ff");
        remoteTree.removenode("f");

        cu.reset();
    }

    // Wait for sync.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, DoesntRubbishIgnoredNodes)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local FS.
    localFS.addfile("x");
    localFS.generate(root(*cdu) / "root");

    // Setup local node tree.
    localTree = localFS;

    // Setup remote note tree.
    remoteTree = localFS;

    // Log in the client.
    ASSERT_TRUE(cdu->login_reset_makeremotenodes(cdu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cdu, "root"));

    // Wait for sync to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Filter out fx.
    localFS.addfile(".megaignore", "-:x");
    localFS.generate(root(*cdu) / "root");

    // x should remain in the cloud.
    remoteTree = localFS;

    // x should now be ignored in the local tree.
    localTree = localFS;
    localTree.removenode("x");

    // Wait for sync to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Remove cdu/x.
    {
        ASSERT_TRUE(cu->login_fetchnodes());
        ASSERT_TRUE(cu->deleteremote("cdu/x"));

        cu.reset();
    }

    // Update models.
    remoteTree.removenode("x");

    // Wait for sync to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, DoesntRubbishWhenBlocked)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up local filesystem.
    localFS.addfile("d/f");
    localFS.addfile("f");
    localFS.generate(root(*cdu) / "root");

    // Local node tree matches local FS.
    localTree = localFS;

    // Remote node tree matches local node tree.
    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cdu->login_reset_makeremotenodes(cdu->clientname));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cdu, "root"));

    // Wait for synchronization to complete.
    waitOnSyncs(cdu.get());

    // Check everything synchronized correctly.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Add a broken ignore file.
    localFS.addfile(".megaignore", "bad");
    localFS.generate(root(*cdu) / "root");

    // Local node tree matches local FS.
    localTree = localFS;

    // Try and synchronize.
    waitOnSyncs(cdu.get());

    {
        ASSERT_TRUE(cu->login_fetchnodes());

        // Remove cdu/d.
        remoteTree.removenode("d");
        ASSERT_TRUE(cu->deleteremote("cdu/d"));

        // Remove cdu/f.
        remoteTree.removenode("f");
        ASSERT_TRUE(cu->deleteremote("cdu/f"));

        cu.reset();
    }
    
    // Wait for sync.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, DoesntUploadIgnoredNodes)
{
    const string ignoreFile = "-:da\n-:f\n";

    // Set up cloud.
    {
        Model model;

        model.addfile(".megaignore", ignoreFile);
        model.generate(root(*cu) / "root");

        // Disable filtering functionality.
        cu->client.ignoreFilesEnabled = false;

        // Upload.
        ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));
        ASSERT_TRUE(setupSync(*cu, "root", "x"));

        waitOnSyncs(cu.get());

        // Confirm.
        ASSERT_TRUE(confirm(*cu, model));

        // Logout.
        cu.reset();
    }

    // Set up models.
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    localFS.addfile("da/f");
    localFS.addfile("da/g");
    localFS.addfile("db/f");
    localFS.addfile("db/g");
    localFS.addfile("f");
    localFS.addfile("g");
    localFS.generate(root(*cd) / "root");
    localFS.addfile(".megaignore", ignoreFile);

    localTree = localFS;
    localTree.removenode("da");
    localTree.removenode("db/f");
    localTree.removenode("f");

    remoteTree = localTree;

    // Log in client.
    ASSERT_TRUE(cd->login_fetchnodes());

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cd, "root", "x"));

    // Wait for synchronization to complete.
    waitOnSyncs(cd.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, DoesntUploadWhenBlocked)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up local FS.
    localFS.addfile(".megaignore", "#");
    localFS.addfile("d/f");
    localFS.addfile("f");
    localFS.generate(root(*cu) / "root");

    // Local node tree is consistent with local FS.
    localTree = localFS;

    // Remote node tree is consistent with local node tree.
    remoteTree = localTree;

    // Log in clients.
    ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));
    ASSERT_TRUE(cd->login_fetchnodes());

    // Add and start syncs.
    fs::create_directories(root(*cd) / "root");
    ASSERT_TRUE(setupSync(*cd, "root", "x"));
    ASSERT_TRUE(setupSync(*cu, "root", "x"));

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get(), cd.get());

    // Verify sync completed correctly.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Break the ignore file on the uploader side.
    LocalFSModel uLocalFS = localFS;

    uLocalFS.addfile(".megaignore", "bad");
    uLocalFS.generate(root(*cu) / "root");

    // Alter x/d/f and x/f.
    uLocalFS.addfile("d/f", "ff");
    uLocalFS.addfile("f", "ff");
    uLocalFS.generate(root(*cu) / "root");

    // Add x/g and x/d/g.
    uLocalFS.addfile("g");
    uLocalFS.addfile("d/g");
    uLocalFS.generate(root(*cu) / "root");

    // Wait for the uploader to try and synchronize.
    waitOnSyncs(cu.get(), cd.get());

    // Confirm models.

    // Only local FS should change for uploader.
    ASSERT_TRUE(confirm(*cu, uLocalFS));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // Nothing should've changed for the downloader.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, FilterAdded)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Setup local fs.
    localFS.addfile("x");
    localFS.generate(root(*cu) / "root");

    // Setup local and remote tree.
    localTree = localFS;
    remoteTree = localFS;

    // Log in "upload" client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));

    // Log in "download" client.
    ASSERT_TRUE(cd->login_fetchnodes());

    // Add and start syncs.
    ASSERT_TRUE(setupSync(*cu, "root", "x"));

    fs::create_directories(root(*cd) / "root");
    ASSERT_TRUE(setupSync(*cd, "root", "x"));

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get(), cd.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));

    // Add .megaignore to "upload" client.
    localFS.addfile(".megaignore", "-:x");
    localFS.generate(root(*cu) / "root");

    // x will become ignored.
    localTree = localFS;
    localTree.removenode("x");

    // .megaignore's now in the cloud.
    remoteTree = localFS;

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get(), cd.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, FilterChanged)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up local FS.
    localFS.addfile(".megaignore", "-:x");
    localFS.addfile("x");
    localFS.addfile("y");
    localFS.generate(root(*cu) / "root");

    // Set up local tree.
    localTree = localFS;
    localTree.removenode("x");

    // Set up remote tree.
    remoteTree = localTree;

    // Log in clients.
    ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));
    ASSERT_TRUE(cd->login_fetchnodes());

    // Add and start syncs.
    ASSERT_TRUE(setupSync(*cu, "root", "x"));

    fs::create_directories(root(*cd) / "root");
    ASSERT_TRUE(setupSync(*cd, "root", "x"));
    
    // Wait for synchronization to complete.
    waitOnSyncs(cu.get(), cd.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    // x is not present under cd.
    localFS.removenode("x");

    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));

    // Update ignore file on uploader side.
    localFS.addfile(".megaignore", "-:y");
    localFS.generate(root(*cu) / "root");

    // Update models.
    localFS.addfile("x");

    // x is no longer ignored.
    // y is now ignored.
    localTree = localFS;
    localTree.removenode("y");

    // remote contains everything.
    remoteTree = localFS;

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get(), cd.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cu, localFS));
    ASSERT_TRUE(confirm(*cu, localTree));
    ASSERT_TRUE(confirm(*cu, remoteTree));

    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, FilterDeferredChange)
{
    Model model;

    // Set up remote model.
    model.addfile(".megaignore", "-:d");
    model.addfile("d/.megaignore", "-:x");
    model.addfile("d/x");
    model.addfile("d/y");
    model.generate(root(*cu) / "root");

    // Disable ignore files on the uploading client.
    // This is so we can make remote changes to the ignored ignore file.
    cu->client.ignoreFilesEnabled = false;

    // Log in "uploading" client.
    ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));

    // Add and start sync.
    ASSERT_TRUE(setupSync(*cu, "root", "x"));

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get());

    // Confirm model.
    ASSERT_TRUE(confirm(*cu, model));

    // Set up local FS.
    LocalFSModel localFS;

    localFS.addfile(".megaignore", "-:d");

    // Set up local model.
    LocalNodeModel localTree = localFS;

    // Set up remote model.
    RemoteNodeModel remoteTree = model;

    remoteTree.addfile("d/.megaignore", "-:x");
    remoteTree.addfile("d/x");
    remoteTree.addfile("d/y");

    // Log in "downloading" client.
    ASSERT_TRUE(cd->login_fetchnodes());

    // Add sync and start sync.
    fs::create_directories(root(*cd) / "root");
    ASSERT_TRUE(setupSync(*cd, "root", "x"));

    // Wait for synchronization to complete.
    waitOnSyncs(cd.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));

    // Change x/d/.megaignore to include x and exclude y.
    model.addfile("d/.megaignore", "-:y");
    model.generate(root(*cu) / "root");

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get(), cd.get());

    // Update models.
    remoteTree = model;

    // Confirm uploader model.
    ASSERT_TRUE(confirm(*cu, model));

    // Confirm downloader models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));

    // Change x/.megaignore to allow x/d.
    model.addfile(".megaignore", "#-:d");
    model.generate(root(*cu) / "root");

    // Wait for synchronization to complete.
    waitOnSyncs(cu.get(), cd.get());

    // Update models.
    localFS = model;
    localFS.removenode("d/y");
    localTree = localFS;
    remoteTree = model;

    // Confirm uploader model.
    ASSERT_TRUE(confirm(*cu, model));

    // Confirm downloader models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, FilterMovedAcrossHierarchy)
{
    // Set up cloud.
    {
        Model model;

        // Setup model.
        model.addfile("a/.megaignore", "-:fa");
        model.addfile("a/fa");
        model.addfile("b/fa");
        model.generate(root(*cu) / "root");

        // Disable ignore file functionality.
        cu->client.ignoreFilesEnabled = false;

        // Upload tree.
        ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));
        ASSERT_TRUE(setupSync(*cu, "root", "x"));

        waitOnSyncs(cu.get());

        // Confirm model.
        ASSERT_TRUE(confirm(*cu, model));

        // Logout.
        cu.reset();
    }

    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;
 
    // Setup local FS.
    localFS.addfile("a/.megaignore", "-:fa");
    localFS.addfile("b/fa");

    // Setup local tree.
    localTree = localFS;

    // Setup remote tree.
    remoteTree = localFS;
    remoteTree.addfile("a/fa");

    // Log in client.
    ASSERT_TRUE(cd->login_fetchnodes());

    // Add and start sync.
    fs::create_directories(root(*cd) / "root");
    ASSERT_TRUE(setupSync(*cd, "root", "x"));

    // Wait for synchronization to complete.
    waitOnSyncs(cd.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));

    // Move x/a/.megaignore to x/b/.megaignore.
    {
        ASSERT_TRUE(cdu->login_fetchnodes());
        ASSERT_TRUE(cdu->movenode("x/a/.megaignore", "x/b"));
        cdu.reset();
    }

    // Wait for sync.
    waitOnSyncs(cd.get());

    // Update models.
    // a/.megaignore -> b/.megaignore.
    // a/fa should become included.
    // b/fa should become excluded.
    localFS.addfile("a/fa");
    localFS.movenode("a/.megaignore", "b");

    localTree = localFS;
    localTree.removenode("b/fa");

    remoteTree.movenode("a/.megaignore", "b");

    // Confirm models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, FilterMovedDownHierarchy)
{
    // Set up cloud.
    {
        Model model;

        // Setup model.
        model.addfile(".megaignore", "-:fa");
        model.addfile("a/fa");
        model.addfile("b/fa");
        model.generate(root(*cu) / "root");

        // Disable ignore file functionality.
        cu->client.ignoreFilesEnabled = false;

        // Upload tree.
        ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));
        ASSERT_TRUE(setupSync(*cu, "root", "x"));

        waitOnSyncs(cu.get());

        // Confirm model.
        ASSERT_TRUE(confirm(*cu, model));

        // Logout.
        cu.reset();
    }

    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;
 
    // Setup local FS.
    localFS.addfile(".megaignore", "-:fa");
    localFS.addfolder("a");
    localFS.addfolder("b");

    // Setup local tree.
    localTree = localFS;

    // Setup remote tree.
    remoteTree = localFS;
    remoteTree.addfile("a/fa");
    remoteTree.addfile("b/fa");

    // Log in client.
    ASSERT_TRUE(cd->login_fetchnodes());

    // Add and start sync.
    fs::create_directories(root(*cd) / "root");
    ASSERT_TRUE(setupSync(*cd, "root", "x"));

    // Wait for synchronization to complete.
    waitOnSyncs(cd.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));

    // Move x/.megaignore to x/a/.megaignore.
    {
        ASSERT_TRUE(cdu->login_fetchnodes());
        ASSERT_TRUE(cdu->movenode("x/.megaignore", "x/a"));
        cdu.reset();
    }

    // Wait for sync.
    waitOnSyncs(cd.get());

    // Update models.
    // .megaignore -> a/.megaignore.
    // a/fa should remain excluded.
    // b/fa should become included.
    localFS.addfile("b/fa");
    localFS.movenode(".megaignore", "a");

    localTree = localFS;

    remoteTree = localFS;
    remoteTree.addfile("a/fa");

    // Confirm models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, FilterMovedUpHierarchy)
{
    // Set up cloud.
    {
        Model model;

        // Setup model.
        model.addfile("a/.megaignore", "-:fa");
        model.addfile("a/fa");
        model.addfile("b/fa");
        model.generate(root(*cu) / "root");

        // Disable ignore file functionality.
        cu->client.ignoreFilesEnabled = false;

        // Upload tree.
        ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));
        ASSERT_TRUE(setupSync(*cu, "root", "x"));

        waitOnSyncs(cu.get());

        // Confirm model.
        ASSERT_TRUE(confirm(*cu, model));

        // Logout.
        cu.reset();
    }

    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;
 
    // Setup local FS.
    localFS.addfile("a/.megaignore", "-:fa");
    localFS.addfile("b/fa");

    // Setup local tree.
    localTree = localFS;

    // Setup remote tree.
    remoteTree = localFS;
    remoteTree.addfile("a/fa");

    // Log in client.
    ASSERT_TRUE(cd->login_fetchnodes());

    // Add and start sync.
    fs::create_directories(root(*cd) / "root");
    ASSERT_TRUE(setupSync(*cd, "root", "x"));

    // Wait for synchronization to complete.
    waitOnSyncs(cd.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));

    // Move x/a/.megaignore to x/.megaignore.
    {
        ASSERT_TRUE(cdu->login_fetchnodes());
        ASSERT_TRUE(cdu->movenode("x/a/.megaignore", "x"));
        cdu.reset();
    }

    // Wait for sync.
    waitOnSyncs(cd.get());

    // Update models.
    // a/.megaignore -> .megaignore.
    // [ab]/fa should both be excluded.
    localFS.movenode("a/.megaignore", "");

    localTree = localFS;
    localTree.removenode("b/fa");

    remoteTree = localFS;
    remoteTree.addfile("a/fa");

    // Confirm models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, FilterRemoved)
{
    // Set up cloud.
    {
        Model model;

        // Setup model.
        model.addfile(".megaignore", "-:fa");
        model.addfile("fa");
        model.generate(root(*cu) / "root");

        // Disable ignore file functionality.
        cu->client.ignoreFilesEnabled = false;

        // Upload tree.
        ASSERT_TRUE(cu->login_reset_makeremotenodes("x"));
        ASSERT_TRUE(setupSync(*cu, "root", "x"));

        waitOnSyncs(cu.get());

        // Confirm model.
        ASSERT_TRUE(confirm(*cu, model));

        // Logout.
        cu.reset();
    }

    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;
 
    // Setup local FS.
    localFS.addfile(".megaignore", "-:fa");

    // Setup local tree.
    localTree = localFS;

    // Setup remote tree.
    remoteTree = localFS;
    remoteTree.addfile("fa");

    // Log in client.
    ASSERT_TRUE(cd->login_fetchnodes());

    // Add and start sync.
    fs::create_directories(root(*cd) / "root");
    ASSERT_TRUE(setupSync(*cd, "root", "x"));

    // Wait for synchronization to complete.
    waitOnSyncs(cd.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));

    // Remove x/.megaignore.
    {
        ASSERT_TRUE(cdu->login_fetchnodes());
        ASSERT_TRUE(cdu->deleteremote("x/.megaignore"));
        cdu.reset();
    }

    // Wait for sync.
    waitOnSyncs(cd.get());

    // Update models.
    // .megaignore -> gone.
    // fa should now be included.
    localFS.removenode(".megaignore");
    localFS.addfile("fa");

    localTree = localFS;
    remoteTree = localFS;

    // Confirm models.
    ASSERT_TRUE(confirm(*cd, localFS));
    ASSERT_TRUE(confirm(*cd, localTree));
    ASSERT_TRUE(confirm(*cd, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, MoveToIgnoredRubbishesRemote)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up local FS.
    localFS.addfile("d/.megaignore", "-:f");
    localFS.addfile("f");
    localFS.generate(root(*cdu) / "root");

    // Set up local tree.
    localTree = localFS;

    // Set up remote tree.
    remoteTree = localFS;

    // Log in client.
    ASSERT_TRUE(cdu->login_reset_makeremotenodes(cdu->clientname));
    
    // Add and start sync.
    ASSERT_TRUE(setupSync(*cdu, "root"));
    
    // Wait for synchronization to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Move f to d/f.
    {
        ASSERT_TRUE(cu->login_fetchnodes());
        ASSERT_TRUE(cu->movenode("cdu/f", "cdu/d"));

        cu.reset();
    }

    // f has moved into the local debris.
    localFS.copynode("f", debrisFilePath("f"));
    localFS.removenode("f");

    localTree = localFS;

    // f has moved to d/f in the cloud.
    remoteTree.movenode("f", "d");

    // Wait for synchronization to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS, 0, false));
    ASSERT_TRUE(confirm(*cdu, localTree, 0, false));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

TEST_F(CloudToLocalFilterFixture, RenameToIgnoredRubbishesRemote)
{
    LocalFSModel localFS;
    LocalNodeModel localTree;
    RemoteNodeModel remoteTree;

    // Set up local FS.
    localFS.addfile(".megaignore", "-:y");
    localFS.addfile("x");
    localFS.generate(root(*cdu) / "root");

    // Set up local tree.
    localTree = localFS;

    // Set up remote tree.
    remoteTree = localFS;

    // Log in client.
    ASSERT_TRUE(cdu->login_reset_makeremotenodes(cdu->clientname));
    
    // Add and start sync.
    ASSERT_TRUE(setupSync(*cdu, "root"));
    
    // Wait for synchronization to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS));
    ASSERT_TRUE(confirm(*cdu, localTree));
    ASSERT_TRUE(confirm(*cdu, remoteTree));

    // Rename cdu/x to cdu/y.
    {
        ASSERT_TRUE(cu->login_fetchnodes());

        Node* node =
          cu->drillchildnodebyname(cu->gettestbasenode(), "cdu/x");
        ASSERT_TRUE(node);

        node->attrs.map['n'] = "y";
        ASSERT_TRUE(cu->setattr(*node));

        cu.reset();
    }

    // x has moved into the local debris.
    localFS.copynode("x", debrisFilePath("x"));
    localFS.removenode("x");

    localTree = localFS;

    // x has become y in the cloud.
    remoteTree.copynode("x", "y");
    remoteTree.removenode("x");

    // Wait for synchronization to complete.
    waitOnSyncs(cdu.get());

    // Confirm models.
    ASSERT_TRUE(confirm(*cdu, localFS, 0, false));
    ASSERT_TRUE(confirm(*cdu, localTree, 0, false));
    ASSERT_TRUE(confirm(*cdu, remoteTree));
}

#endif

