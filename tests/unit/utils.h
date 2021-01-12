/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
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

#pragma once

#include <memory>

#include <mega/megaclient.h>
#include <mega/node.h>
#include <mega/sync.h>
#include <mega/filefingerprint.h>

namespace mt {

class FsNode;

mega::handle nextFsId();

std::shared_ptr<mega::MegaClient> makeClient(mega::MegaApp& app, mega::FileSystemAccess& fsaccess);

mega::Node& makeNode(mega::MegaClient& client, mega::nodetype_t type, mega::handle handle, mega::Node* parent = nullptr);

#ifdef ENABLE_SYNC
std::unique_ptr<mega::UnifiedSync> makeSync(mega::MegaClient& client, const std::string& localname);

std::unique_ptr<mega::LocalNode> makeLocalNode(mega::Sync& sync, mega::LocalNode& parent,
                                               mega::nodetype_t type, const std::string& name,
                                               const mega::FileFingerprint& ffp = {});
#endif

void collectAllFsNodes(std::map<mega::LocalPath, const mt::FsNode*>& nodes, const mt::FsNode& node);

std::uint16_t nextRandomInt();

mega::byte nextRandomByte();

} // mt
