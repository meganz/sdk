#pragma once

#include <mega/common/node_info_forward.h>
#include <mega/common/testing/path_forward.h>
#include <mega/common/testing/utility.h>
#include <mega/common/type_traits.h>
#include <mega/fuse/common/inode_id_forward.h>
#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/testing/client_forward.h>

namespace mega
{

class Error;
class NodeHandle;

namespace fuse
{
namespace testing
{

// Convenience.
template<typename T>
using IsInfoLike = common::IsOneOf<T, InodeInfo, common::NodeInfo>;

template<typename I, typename T>
using EnableIfInfoLike = std::enable_if<IsInfoLike<I>::value, T>;

handle fsidOf(const common::testing::Path& path);

NodeHandle id(const common::NodeInfo& info);
InodeID id(const InodeInfo& info);

bool makeFile(const common::testing::Path& path, const std::string& data);
bool makeFile(const common::testing::Path& path, std::size_t size);

NodeHandle parentID(const common::NodeInfo& info);
InodeID parentID(const InodeInfo& info);

std::string readFile(const common::testing::Path& path);

std::string toString(NodeHandle handle);

std::uint64_t toUint64(InodeID id);
std::uint64_t toUint64(NodeHandle handle);

} // testing
} // fuse
} // mega
