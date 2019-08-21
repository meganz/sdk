#pragma once

#include <memory>

#include <mega/node.h>
#include <mega/sync.h>
#include <mega/filefingerprint.h>

namespace mt {

class FsNode;

mega::handle nextFsId();

std::unique_ptr<mega::Sync> makeSync(const std::string& localname, mega::handlelocalnode_map& fsidnodes);

std::unique_ptr<mega::LocalNode> makeLocalNode(mega::Sync& sync, mega::LocalNode& parent,
                                               mega::handlelocalnode_map& fsidnodes,
                                               mega::nodetype_t type, const std::string& name,
                                               const mega::FileFingerprint& ffp = {});

void collectAllFsNodes(std::map<std::string, const mt::FsNode*>& nodes, const mt::FsNode& node);

std::uint16_t nextRandomInt();

mega::byte nextRandomByte();

} // mt
