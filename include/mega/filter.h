/**
 * @file mega/filter.h
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
#ifndef MEGA_FILTER_H
#define MEGA_FILTER_H 1

#include "types.h"

namespace mega
{

// Forward Declarations (from Filesystem)
struct MEGA_API FileAccess;
struct MEGA_API InputStreamAccess;

enum FilterStrategy
{
    FS_GLOB,
    FS_REGEX,
    NUM_FILTER_STRATEGIES
}; /* FilterStrategy */

enum FilterTarget
{
    FT_ALL,
    FT_DIRECTORIES,
    FT_FILES,
    NUM_FILTER_TARGETS
}; /* FilterTarget */

enum FilterType
{
    FT_NAME,
    FT_PATH,
    NUM_FILTER_TYPES
}; /* FilterType */

class MEGA_API Filter
{
public:
    virtual ~Filter();

    // Returns true if this filter is applicable to the specified node type.
    bool applicable(const nodetype_t type) const;

    // Returns true if this filter is inheritable.
    bool inheritable() const;

    // Returns true if this filter matches the string s.
    virtual bool match(const string& s) const = 0;

    // Returns the filter's matching strategy.
    virtual FilterStrategy strategy() const = 0;

    // Returns the textual representation of this filter.
    const string& text() const; 

    // Returns the filter's target.
    FilterTarget target() const;

    // Returns the filter's type.
    FilterType type() const;

protected:
    Filter(const string& text,
           const bool caseSensitive,
           const bool inheritable,
           const FilterTarget target,
           const FilterType type);

    MEGA_DISABLE_COPY(Filter);
    MEGA_DEFAULT_MOVE(Filter);

    // Contains the textual representation of this filter.
    const string mText;

    // Specifies whether this rule is case sensitive or not.
    const bool mCaseSensitive;

    // Specifies whether this rule is inherited or not.
    const bool mInheritable;

    // Is this filter applicable to directories, files or both?
    const FilterTarget mTarget;

    // Specifies whether this is a name or path filter.
    const FilterType mType;
}; /* Filter */

// Convenience types.
using FilterPtr = unique_ptr<Filter>;
using FilterVector = vector<FilterPtr>;

class MEGA_API FilterClass
{
public:
    FilterClass();

    MEGA_DISABLE_COPY(FilterClass);
    MEGA_DEFAULT_MOVE(FilterClass);

    // Adds a filter to the class.
    void add(FilterPtr&& filter);

    // Clears all filters in this class.
    void clear();

    // Checks whether this class has any filters.
    bool empty() const;

    // Returns true if this class matches the name/path pair p.
    bool match(const string_pair &p,
               const nodetype_t type,
               const bool onlyInheritable) const;

private:
    // Name filters.
    FilterVector mNames;

    // Path filters.
    FilterVector mPaths;
}; /* FilterClass */

class MEGA_API FilterChain
{
public:
    FilterChain();

    MEGA_DISABLE_COPY(FilterChain);
    MEGA_DEFAULT_MOVE(FilterChain);

    // Adds the filter specified by text.
    bool add(const string& text);

    // Erases all filters in the chain.
    void clear();

    // Checks if the chain is empty.
    bool empty() const;

    // Checks if the name/path pair is to be excluded.
    bool excluded(const string_pair& p,
                  const nodetype_t type,
                  const bool onlyInheritable) const;

    // Checks if the name/path pair is to be included.
    bool included(const string_pair& p,
                  const nodetype_t type,
                  const bool onlyInheritable) const;

    // Loads a filter chain from disk.
    // Chain is changed only if all filters could be added successfully.
    bool load(InputStreamAccess& isAccess);
    bool load(FileAccess& ifAccess);

private:
    // Exclusion filters.
    FilterClass mExclusions;

    // Inclusion filters.
    FilterClass mInclusions;
}; /* FilterChain */

// Useful for debugging / logging.
string toString(const Filter& filter);
const string& toString(const FilterStrategy strategy);
const string& toString(const FilterTarget target);
const string& toString(const FilterType type);

} /* mega */

#endif /* ! MEGA_FILTER_H */

