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
    DefaultFilterChain();

    DefaultFilterChain(DefaultFilterChain& other);

    DefaultFilterChain& operator=(DefaultFilterChain& rhs);

    // Creates a new ignore file in the target directory.
    //
    // Note that this function only writes an ignore file if one does not
    // already exist in the target directory.
    //
    // Returns true if:
    // - An ignore file already exists in the target directory.
    // - We successfully created an ignore file in the target directory.
    //
    // Returns false otherwise.
    bool create(const LocalPath& targetPath, bool appendName, FileSystemAccess& fsAccess, bool setSyncIgnoreFileFlag);

    // Specify what names should be excluded.
    //
    // Wildcard patterns are valid.
    void excludedNames(const string_vector& names, FileSystemAccess& fsAccess);

    // Specify what paths should be excluded.
    void excludedPaths(const string_vector& paths);

    // Specify that a given path should be excluded.
    void excludePath(const string& path);

    // Generates the content for an ignore file.
    string generate(const LocalPath& targetPath, FileSystemAccess& fsAccess, bool includeBOM, bool setSyncIgnoreFileFlag) const;

    // Specify the lower size limit.
    //
    // Zero is valid and represents "no lower limit."
    void lowerLimit(std::uint64_t lower);

    // Resets the chain to its default state.
    void reset();

    // Specify the upper size limit.
    //
    // Zero is valid and represents "no upper limit."
    void upperLimit(std::uint64_t upper);

private:
    // Computes a list of path exclusions applicable to targetPath.
    //
    // In order for an exclusion to be applicable to some target, the path
    // it excludes must be contained by that target.
    //
    // Note that the paths returned by this function are relative to target.
    vector<LocalPath> applicablePaths(LocalPath targetPath) const;

    // Returns a copy of strings where each individual string has been
    // normalized.
    //
    // Strings that can not be normalized are not included in the result.
    string_vector normalize(const string_vector& strings) const;

    // Converts a relative local path to remote format.
    //
    // Path components are unescaped and are separated by '/'.
    RemotePath toRemotePath(const LocalPath& path, FileSystemAccess& fsAccess) const;

    // Converts a list of relative local paths to remote format.
    vector<RemotePath> toRemotePaths(const vector<LocalPath>& localPaths, FileSystemAccess& fsAccess) const;

    // Predefined name exclusions.
    static const string_vector mPredefinedNameExclusions;

    // Names that should be excluded.
    //
    // These names are stored in "cloud" format.
    //
    // Wildcard patterns are valid.
    vector<string> mExcludedNames;

    // Absolute paths that should be excluded.
    //
    // These names are stored in "local" format.
    //
    // These names are translated into "cloud" format as necessary when
    // writing an ignore file for a specific sync root.
    vector<LocalPath> mExcludedPaths;

    // How we synchronize access to instances of this class.
    mutable mutex mLock;

    // Lower size limit.
    //
    // Zero is a sentinel for "no limit."
    std::uint64_t mLowerLimit;

    // Upper size limit.
    //
    // Zero is a sentinel for "no limit."
    std::uint64_t mUpperLimit;
}; // DefaultFilterChain

enum FilterLoadResult
{
    // The ignore file is no longer present.
    FLR_DELETED,
    // The ignore file failed to load.
    FLR_FAILED,
    // The ignore file was loaded successfully.
    FLR_SUCCESS
}; // FilterLoadResult

class MEGA_API FilterChain
{
public:
    // Removes all filters in this chain.
    void clear();

    // Loads filters from a file.
    FilterLoadResult load(FileSystemAccess& fsAccess, const LocalPath& path);
    FilterLoadResult load(FileAccess& fileAccess);

    // Attempts to locate a match for the path pair p.
    ExclusionState match(const RemotePathPair& p,
                       const nodetype_t type,
                       const bool onlyInheritable) const;

    // Attempts to locate a match for the size s.
    ExclusionState match(const m_off_t s) const;

    // Fingerprint of the last loaded ignore file.
    FileFingerprint mFingerprint;
    bool mLoadSucceeded = false;

    bool mSyncThisMegaignore = false;

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

