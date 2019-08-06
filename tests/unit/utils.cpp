#include "utils.h"

#include <random>

#include "FsNode.h"

namespace mt {

namespace {

std::mt19937 gRandomGenerator{1};

void collectAllFsNodesImpl(std::map<std::string, const mt::FsNode*>& nodes, const mt::FsNode& node)
{
    const auto path = node.getPath();
    assert(nodes.find(path) == nodes.end());
    nodes[path] = &node;
    if (node.getType() == mega::FOLDERNODE)
    {
        for (const auto child : node.getChildren())
        {
            collectAllFsNodesImpl(nodes, *child);
        }
    }
}

} // anonymous

mega::handle nextFsId()
{
    static mega::handle fsId{0};
    return fsId++;
}

std::unique_ptr<mega::Sync> makeSync(const std::string& localname, mega::handlelocalnode_map& fsidnodes)
{
    auto sync = std::unique_ptr<mega::Sync>{new mega::Sync};
    sync->localroot.name = localname;
    sync->localroot.localname = localname;
    sync->localroot.slocalname = &sync->localroot.localname;
    sync->localroot.fsid = nextFsId();
    sync->localroot.fsid_it = fsidnodes.end();
    sync->localroot.type = mega::FOLDERNODE;
    sync->localroot.parent = nullptr;
    sync->localroot.node = nullptr;
    sync->state = mega::SYNC_CANCELED;
    sync->client = nullptr;
    sync->localroot.sync = sync.get();
    sync->tmpfa = nullptr;
    sync->statecachetable = nullptr;
    return sync;
}

std::unique_ptr<mega::LocalNode> makeLocalNode(mega::Sync& sync, mega::LocalNode& parent,
                                               mega::nodetype_t type, std::string name,
                                               const mega::FileFingerprint& ffp)
{
    if (ffp.isvalid)
    {
        assert(type == mega::FILENODE);
    }
    auto l = std::unique_ptr<mega::LocalNode>{new mega::LocalNode};
    l->sync = &sync;
    l->parent = &parent;
    l->fsid = nextFsId();
    l->type = type;
    l->name = name;
    l->localname = name;
    l->slocalname = &l->localname;
    l->node = nullptr;
    assert(parent.children.find(&l->name) == parent.children.end());
    parent.children[&l->name] = l.get();
    static_cast<mega::FileFingerprint&>(*l) = ffp;
    return l;
}

void collectAllFsNodes(std::map<std::string, const mt::FsNode*>& nodes, const mt::FsNode& node)
{
    const auto path = node.getPath();
    assert(nodes.find(path) == nodes.end());
    nodes[path] = &node;
    if (node.getType() == mega::FOLDERNODE)
    {
        for (const auto child : node.getChildren())
        {
            collectAllFsNodes(nodes, *child);
        }
    }
}

void collectAllLocalNodes(mega::handlelocalnode_map& nodes, mega::LocalNode& l)
{
    const auto result = nodes.insert(std::make_pair(l.fsid, &l));
    assert(result.second);
    l.fsid_it = result.first;
    if (l.type == mega::FOLDERNODE)
    {
        for (auto& childPair : l.children)
        {
            collectAllLocalNodes(nodes, *childPair.second);
        }
    }
}

std::uint16_t nextRandomInt()
{
    std::uniform_int_distribution<std::uint16_t> dist{0, std::numeric_limits<std::uint16_t>::max()};
    return dist(gRandomGenerator);
}

mega::byte nextRandomByte()
{
    std::uniform_int_distribution<mega::byte> dist{0, std::numeric_limits<mega::byte>::max()};
    return dist(gRandomGenerator);
}

} // mt
