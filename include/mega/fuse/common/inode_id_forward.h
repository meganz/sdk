#pragma once

#include <map>
#include <set>
#include <vector>

namespace mega
{
namespace fuse
{

class InodeID;

template<typename T>
using FromInodeIDMap = std::map<InodeID, T>;

using InodeIDSet = std::set<InodeID>;

using InodeIDVector = std::vector<InodeID>;

} // fuse
} // mega

