#include "megafs.h"

#include <mega/common/node_info.h>
#include <mega/common/testing/path.h>
#include <mega/common/testing/utility.h>
#include <mega/fuse/common/inode_id.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/types.h>
#include <mega/utils.h>

#include <fstream>

namespace mega
{
namespace fuse
{
namespace testing
{

using namespace common;
using namespace common::testing;

handle fsidOf(const Path& path)
{
    static FSACCESS_CLASS fsAccess;

    return fsAccess.fsidOf(path.localPath(), false, true, FSLogging::logOnError);
}

NodeHandle id(const NodeInfo& info)
{
    return info.mHandle;
}

InodeID id(const InodeInfo& info)
{
    return info.mID;
}

bool makeFile(const Path& path, const std::string& data)
{
    std::ofstream ostream(path.string(), std::ios::binary | std::ios::trunc);

    if (!ostream)
        return false;

    ostream.write(data.data(), static_cast<std::streamsize>(data.size()));

    if (!ostream)
        return false;

    ostream.flush();

    if (!ostream)
        return false;

    ostream.close();

    return !!ostream;
}

bool makeFile(const Path& path, std::size_t size)
{
    return makeFile(path, randomBytes(size));
}

NodeHandle parentID(const NodeInfo& info)
{
    return info.mParentHandle;
}

InodeID parentID(const InodeInfo& info)
{
    return info.mParentID;
}

std::string readFile(const Path& path)
{
    std::error_code error;

    auto size = fs::file_size(path, error);

    if (error)
        return std::string();

    std::ifstream istream(path.path(), std::ios::binary);

    if (!istream)
        return std::string();

    std::string buffer(static_cast<std::size_t>(size), '\0');

    istream.read(&buffer[0], static_cast<std::streamsize>(size));

    buffer.resize(static_cast<std::size_t>(istream.gcount()));

    return buffer;
}

std::string toString(NodeHandle handle)
{
    return toNodeHandle(handle);
}

std::uint64_t toUint64(InodeID id)
{
    return id.toU64();
}

std::uint64_t toUint64(NodeHandle handle)
{
    return handle.as8byte();
}

} // testing
} // fuse
} // mega
