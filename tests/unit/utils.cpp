#include "utils.h"

#include <random>

#include "constants.h"
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

void initializeLocalNode(mega::LocalNode& l, mega::Sync& sync, mega::LocalNode* parent, mega::handlelocalnode_map& fsidnodes,
                         mega::nodetype_t type, const std::string& name, const mega::FileFingerprint& ffp)
{
    if (ffp.isvalid)
    {
        assert(type == mega::FILENODE);
    }
    l.sync = &sync;
    l.parent = parent;
    l.type = type;
    l.name = name;
    l.localname = name;
    l.slocalname = &l.localname;
    l.fsid_it = fsidnodes.end();
    l.setfsid(nextFsId(), fsidnodes);
    if (parent)
    {
        assert(parent->children.find(&l.name) == parent->children.end());
        parent->children[&l.name] = &l;
    }
    static_cast<mega::FileFingerprint&>(l) = ffp;
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
    initializeLocalNode(sync->localroot, *sync, nullptr, fsidnodes,  mega::FOLDERNODE, localname, {});
    sync->localdebris = sync->localroot.localname + "/" + mt::gLocalDebris;
    return sync;
}

std::unique_ptr<mega::LocalNode> makeLocalNode(mega::Sync& sync, mega::LocalNode& parent,
                                               mega::handlelocalnode_map& fsidnodes,
                                               mega::nodetype_t type, const std::string& name,
                                               const mega::FileFingerprint& ffp)
{
    if (ffp.isvalid)
    {
        assert(type == mega::FILENODE);
    }
    auto l = std::unique_ptr<mega::LocalNode>{new mega::LocalNode};
    initializeLocalNode(*l, sync, &parent, fsidnodes, type, name, ffp);
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

std::uint16_t nextRandomInt()
{
    std::uniform_int_distribution<std::uint16_t> dist{0, std::numeric_limits<std::uint16_t>::max()};
    return dist(gRandomGenerator);
}

mega::byte nextRandomByte()
{
    std::uniform_int_distribution<unsigned short> dist{0, std::numeric_limits<mega::byte>::max()};
    return static_cast<mega::byte>(dist(gRandomGenerator));
}

} // mt
