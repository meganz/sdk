/**
 * @file mega/syncfilter.h
 * @brief Classes representing file filters.
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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
#ifndef MEGA_SYNC_FILTER_H
#define MEGA_SYNC_FILTER_H 1

#include <memory>

#include "types.h"

namespace mega
{

// Forward Declarations (from Filesystem)
struct FileSystemAccess;
class LocalPath;

struct FileAccess;

// Forward Declaration
class SizeFilter;
class StringFilter;

// Convenience.
using SizeFilterPtr = std::shared_ptr<SizeFilter>;
using StringFilterPtr = std::shared_ptr<StringFilter>;
using StringFilterPtrVector = std::vector<StringFilterPtr>;

class MEGA_API DefaultFilterChain
{
public:
    bool empty() const;

    bool write(FileSystemAccess& fsAccess, const LocalPath& rootPath) const;

    string_vector excludedNames = {};
    string_vector excludedPaths = {};
    std::uint64_t lowerSizeLimit = 0;
    std::uint64_t upperSizeLimit = 0;
}; // DefaultFilterChain

class MEGA_API FilterResult
{
public:
    FilterResult();

    explicit FilterResult(const bool included);

    MEGA_DEFAULT_COPY(FilterResult);
    MEGA_DEFAULT_MOVE(FilterResult);

    bool included;
    bool matched;
}; /* FilterResult */

class MEGA_API FilterChain
{
public:
    FilterChain();

    FilterChain(const FilterChain& other);
    
    FilterChain(FilterChain&& other);

    ~FilterChain();

    FilterChain& operator=(const FilterChain& rhs);

    FilterChain& operator=(FilterChain&& rhs);

    // Removes all filters in this chain.
    void clear();

    // True if this chain contains no filters.
    bool empty() const;

    // Loads filters from a file.
    bool load(FileAccess& fileAccess);

    // Attempts to locate a match for the path pair p.
    FilterResult match(const RemotePathPair& p,
                       const nodetype_t type,
                       const bool onlyInheritable) const;

    // Attempts to locate a match for the size s.
    FilterResult match(const m_off_t s) const;

private:
    // Name and/or path filters.
    StringFilterPtrVector mStringFilters;

    // File size filter.
    SizeFilterPtr mSizeFilter;
}; /* FilterChain */

class IgnoreFileName
{
    static const LocalPath mLocalName;
    static const RemotePath mRemoteName;

public:
    operator const LocalPath&() const;

    operator const RemotePath&() const;

    operator const string&() const;

    bool operator==(const LocalPath& rhs) const;
    bool operator==(const RemotePath& rhs) const;
    bool operator==(const string& rhs) const;
}; // IgnoreFileName

template<typename T>
bool operator==(const T& lhs, const IgnoreFileName& rhs)
{
    return rhs == lhs;
}

constexpr IgnoreFileName IGNORE_FILE_NAME;

} /* mega */

#endif /* ! MEGA_SYNC_FILTER_H */

