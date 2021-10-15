#include <cassert>
#include <cctype>
#include <cstdint>
#include <regex>
#include <sstream>
#include <string>

#include "mega/filesystem.h"
#include "mega/logging.h"
#include "mega/syncfilter.h"
#include "mega/utils.h"

namespace mega
{

class Matcher;
class Target;

// For convenience.
using MatcherPtr = std::unique_ptr<Matcher>;

class SizeFilter
{
public:
    SizeFilter();

    // True if this filter includes the size s.
    bool match(const std::uint64_t s) const;

    // Lower bound of filter.
    std::uint64_t lower;

    // Upper bound of filter.
    std::uint64_t upper;
}; // SizeFilter

class StringFilter
{
public:
    virtual ~StringFilter() = default;

    // True if this filter is applicable to type.
    bool applicable(const nodetype_t type) const;

    // True if this filter is an inclusion.
    bool inclusion() const;

    // True if this filter is inheritable.
    //
    // I.e. Is the rule in effect in directories below the one where
    // it was defined?
    bool inheritable() const;

    // True if this filter matches the string pair p.
    virtual bool match(const RemotePathPair& p) const = 0;

protected:
    StringFilter(MatcherPtr matcher,
                 const Target& target,
                 const bool inclusion,
                 const bool inheritable);

    // True if this filter matches the string s.
    bool match(const string& s) const;

private:
    MatcherPtr mMatcher;
    const Target& mTarget;
    const bool mInclusion;
    const bool mInheritable;
}; /* StringFilter */

class NameFilter
  : public StringFilter
{
public:
    NameFilter(MatcherPtr matcher,
               const Target& target,
               const bool inclusion,
               const bool inheritable);

    bool match(const RemotePathPair& p) const;
}; /* NameFilter */

class PathFilter
  : public StringFilter
{
public:
    PathFilter(MatcherPtr matcher,
               const Target& target,
               const bool inclusion,
               const bool inheritable);

    bool match(const RemotePathPair& p) const;
}; /* PathFilter */

class Matcher
{
public:
    virtual ~Matcher() = default;

    // True if this matcher matches the string s.
    virtual bool match(const string& s) const = 0;

protected:
    Matcher() = default;
}; /* Matcher */

class GlobMatcher
  : public Matcher
{
public:
    GlobMatcher(const string& pattern, const bool caseSensitive);

    // True if the wildcard pattern matches the string s.
    bool match(const string& s) const override;

private:
    const string mPattern;
    const bool mCaseSensitive;
}; /* GlobMatcher */

class RegexMatcher
  : public Matcher
{
public:
    RegexMatcher(const string& pattern, const bool caseSensitive);

    // True if the regex pattern matches the string s.
    bool match(const string& s) const override;

private:
    std::regex mRegexp;
}; /* RegexMatcher */

class Target
{
public:
    virtual ~Target() = default;

    // True if this target is applicable to type.
    virtual bool applicable(const nodetype_t type) const = 0;

protected:
    Target() = default;
}; /* Target */

class AllTarget
  : public Target
{
public:
    // Always returns true.
    bool applicable(const nodetype_t) const override;

    // Returns an AllTarget instance.
    static const AllTarget& instance();

private:
    AllTarget() = default;
}; /* AllTarget */

class DirectoryTarget
  : public Target
{
public:
    // True if type is FOLDERNODE.
    bool applicable(const nodetype_t type) const override;

    // Returns a DirectoryTarget instance.
    static const DirectoryTarget& instance();

private:
    DirectoryTarget() = default;
}; /* DirectoryTarget */

class FileTarget
  : public Target
{
public:
    // True if type is FILENODE.
    bool applicable(const nodetype_t type) const override;

    // Returns a FileTarget instance.
    static const FileTarget& instance();

private:
    FileTarget() = default;
}; /* FileTarget */

// Parses the size filter "text" and updates (creates) "filter."
static bool add(const string& text, SizeFilterPtr& filter);

// Parses the string filter "text" and adds it to the "filters" vector.
static bool add(const string& text, StringFilterPtrVector& filters);

// Returns true if the substring m..n is empty.
static bool isEmpty(const char* m, const char* n);

// Logs a normalization error and return false.
static FilterLoadResult normalizationError(const string& text);

// Returns appropriate regex flags.
static std::regex::flag_type regexFlags(const bool caseSensitive);

// Logs a syntax error and returns false.
static bool syntaxError(const string& text);

// Uppercases the string text.
static string toUpper(string text);

// Logs an unrepresentable limit error and returns false.
static bool unrepresentableLimitError(const string& text);

// Predefined name exclusions.
//
// Extracted from MEGASync.
const string_vector DefaultFilterChain::mPredefinedNameExclusions = {
    "Thumbs.db",
    "desktop.ini",
    "~*",
    ".*",
    "*~.*",
    "*.crdownload",
    // Avoid trigraph interpretation: ??- is ~.
    "*.sb-????????""-??????",
    "*.tmp"
};

DefaultFilterChain::DefaultFilterChain(FileSystemAccess& fsAccess)
  : mExcludedNames(mPredefinedNameExclusions)
  , mExcludedPaths()
  , mFsAccess(fsAccess)
  , mLock()
  , mLowerLimit(0u)
  , mUpperLimit(0u)
{
}

bool DefaultFilterChain::create(const LocalPath& targetPath)
{
    // Compute the path for the target's ignore file.
    auto filePath = targetPath;
    filePath.appendWithSeparator(IGNORE_FILE_NAME, false);

    // Get access to the filesystem.
    auto fileAccess = mFsAccess.newfileaccess(false);

    // Try and open the file for writing.
    if (!fileAccess->fopen(filePath, false, true))
        return false;

    // Generate the ignore file's content.
    auto content = generate(targetPath);

    // Write the content to disk.
    return fileAccess->fwrite((const byte*)content.data(),
                              (unsigned)content.size(),
                              0);
}

void DefaultFilterChain::excludedNames(const string_vector& names)
{
    lock_guard<mutex> guard(mLock);

    mExcludedNames.clear();

    // Translate UTF8 paths into cloud format.
    for (auto& name : normalize(names))
    {
        LOG_debug << "Excluded name: " << name;

        mExcludedNames.emplace_back(name);
        mFsAccess.unescapefsincompatible(&mExcludedNames.back());
    }
}

void DefaultFilterChain::excludedPaths(const string_vector& paths)
{
    lock_guard<mutex> guard(mLock);

    mExcludedPaths.clear();

    // Translate UTF8 paths to local format.
    for (auto& path : normalize(paths))
    {
        LocalPath localPath = LocalPath::fromPath(path, mFsAccess);

        localPath.ensureWinExtendedPathLenPrefix();

        LOG_debug << "Excluded path: " << localPath.toPath();

        mExcludedPaths.emplace_back(std::move(localPath));
    }
}

void DefaultFilterChain::lowerLimit(std::uint64_t lower)
{
    lock_guard<mutex> guard(mLock);

    mLowerLimit = lower;
}

void DefaultFilterChain::upperLimit(std::uint64_t limit)
{
    lock_guard<mutex> guard(mLock);

    mUpperLimit = limit;
}

vector<LocalPath> DefaultFilterChain::applicablePaths(LocalPath targetPath) const
{
    vector<LocalPath> paths;

    // Make sure the namespace prefix is present.
    //
    // This is necessary so that we can correctly determine whether a given
    // sync root contains some path.
    targetPath.ensureWinExtendedPathLenPrefix();

    // Determine which path exclusions are applicable to the target.
    for (auto& path : mExcludedPaths)
    {
        size_t index;

        // Does the path identify something below the target?
        if (!targetPath.isContainingPathOf(path, &index))
            continue;

        // Path exclusions should be relative to the target.
        paths.emplace_back(path.subpathFrom(index));
    }

    return paths;
}

string DefaultFilterChain::generate(const LocalPath& targetPath) const
{
    lock_guard<mutex> guard(mLock);

    ostringstream ostream;

    // Size filters.
    if (mLowerLimit)
        ostream << "minsize: " << mLowerLimit << "\n";

    if (mUpperLimit)
        ostream << "maxsize: " << mUpperLimit << "\n";

    // Name filters.
    for (auto& name : mExcludedNames)
            ostream << "-:" << name << "\n";

    // Path filters.
    if (mExcludedPaths.empty())
        return ostream.str();

    auto paths = applicablePaths(targetPath);

    for (auto& path : toRemotePaths(paths))
        ostream << "-p:" << path.toName(mFsAccess) << "\n";

    return ostream.str();
}

string_vector DefaultFilterChain::normalize(const string_vector& strings) const
{
    string_vector result;

    for (auto& string : strings)
    {
        auto temp = string;

        mFsAccess.normalize(&temp);

        if (!temp.empty())
            result.emplace_back(std::move(temp));
    }

    return result;
}

RemotePath DefaultFilterChain::toRemotePath(const LocalPath& path) const
{
    LocalPath component;
    RemotePath result;
    size_t index = 0u;

    // Translate each component into remote form.
    while (path.nextPathComponent(index, component))
        result.appendWithSeparator(component.toName(mFsAccess), false);

    return result;
}

vector<RemotePath> DefaultFilterChain::toRemotePaths(const vector<LocalPath>& paths) const
{
    vector<RemotePath> result;

    // Translate each path into remote form.
    for (auto& path : paths)
    {
        auto temp = toRemotePath(path);

        // Skip empty paths.
        if (!temp.empty())
            result.emplace_back(std::move(temp));
    }

    return result;
}

FilterResult::FilterResult()
  : included(false)
  , matched(false)
{
}

FilterResult::FilterResult(const bool included)
  : included(included)
  , matched(true)
{
}

FilterChain::FilterChain()
  : mFingerprint()
  , mStringFilters()
  , mSizeFilter()
{
}

FilterChain::FilterChain(const FilterChain& other) = default;

FilterChain::FilterChain(FilterChain&& other) = default;

FilterChain::~FilterChain() = default;

FilterChain& FilterChain::operator=(const FilterChain& rhs) = default;

FilterChain& FilterChain::operator=(FilterChain&& rhs) = default;

bool FilterChain::changed(const FileFingerprint& fingerprint) const
{
    return mFingerprint != fingerprint;
}

void FilterChain::clear()
{
    mFingerprint = FileFingerprint();
    mSizeFilter.reset();
    mStringFilters.clear();
}

FilterLoadResult FilterChain::load(FileSystemAccess& fsAccess, const LocalPath& path)
{
    // Create a file access so we can access the filesystem.
    auto fileAccess = fsAccess.newfileaccess(false);

    // Open the ignore file for reading.
    if (!fileAccess->fopen(path, true, false)
        || fileAccess->type != FILENODE)
    {
        // Couldn't open the file. Assume it's been deleted.
        return FLR_DELETED;
    }

    // Ignore file exists so try and load it.
    return load(*fileAccess);
}

FilterLoadResult FilterChain::load(FileAccess& fileAccess)
{
    // Has the ignore file changed?
    if (!mFingerprint.genfingerprint(&fileAccess))
    {
        // No point trying to load the file as it hasn't changed.
        return FLR_SKIPPED;
    }

    // Convenience.
    using FSA = FileSystemAccess;

    string_vector lines;

    // Read the filters, line by line.
    // Empty lines are omitted by readLines(...).
    if (!readLines(fileAccess, lines))
    {
        return FLR_FAILED;
    }

    // Temporay storage for newly loaded filters.
    StringFilterPtrVector stringFilters;
    SizeFilterPtr sizeFilter;

    // Add all the filters.
    for (const auto& line : lines)
    {
        // Mutable copy for normalization.
        string l = line;

        // Normalize the line.
        FSA::normalize(&l);

        // Were we able to normalize the line?
        if (l.empty())
        {
            // Nope so report the error.
            return normalizationError(line);
        }

        // Skip comments.
        if (l[0] == '#')
        {
            continue;
        }

        // Try and add the filter.
        if (l[0] == 'm')
        {
            if (!add(l, sizeFilter))
            {
                // Changes are not committed.
                return FLR_FAILED;
            }
        }
        else if (!add(l, stringFilters))
        {
            return FLR_FAILED;
        }
    }

    // Move new filters into place.
    mStringFilters = std::move(stringFilters);
    mSizeFilter = std::move(sizeFilter);

    // Changes are committed.
    return FLR_SUCCESS;
}

FilterResult FilterChain::match(const RemotePathPair& p,
                                const nodetype_t type,
                                const bool onlyInheritable) const
{
    static const string iconResource = "Icon\x0d";

    // Always ignore custom icon metadata.
    if (p.first == iconResource)
    {
        return FilterResult(false);
    }

    auto i = mStringFilters.rbegin();
    auto j = mStringFilters.rend();

    for ( ; i != j; ++i)
    {
        if (onlyInheritable && !(*i)->inheritable())
        {
            continue;
        }

        if (((*i)->applicable(type) || type == TYPE_UNKNOWN)  // TYPE_UNKNOWN because we need to be able to exclude scan-blocked items, and that is the type they appear as.
           && (*i)->match(p))
        {
            return FilterResult((*i)->inclusion());
        }
    }

    return FilterResult();
}

FilterResult FilterChain::match(const m_off_t s) const
{
    // Sanity.
    assert(s >= 0);

    // Can't match if we have no filter.
    if (!mSizeFilter)
    {
        return FilterResult();
    }

    // Silence warnings.
    const auto t = static_cast<std::uint64_t>(s);

    // Attempt the match.
    return FilterResult(mSizeFilter->match(t));
}

#ifdef _WIN32
const LocalPath IgnoreFileName::mLocalName = LocalPath::fromPlatformEncoded(L".megaignore");
#else // _WIN32
const LocalPath IgnoreFileName::mLocalName = LocalPath::fromPlatformEncoded(".megaignore");
#endif // ! _WIN32

const RemotePath IgnoreFileName::mRemoteName(".megaignore");

IgnoreFileName::operator const LocalPath&() const
{
    return mLocalName;
}

IgnoreFileName::operator const RemotePath&() const
{
    return mRemoteName;
}

IgnoreFileName::operator const string&() const
{
    return mRemoteName;
}

bool IgnoreFileName::operator==(const LocalPath& rhs) const
{
    return mLocalName == rhs;
}

bool IgnoreFileName::operator==(const RemotePath& rhs) const
{
    return mRemoteName == rhs;
}

bool IgnoreFileName::operator==(const string& rhs) const
{
    return mRemoteName == rhs;
}

SizeFilter::SizeFilter()
  : lower(0)
  , upper(0)
{
}

bool SizeFilter::match(const std::uint64_t s) const
{
    if (!lower)
    {
        return s <= upper;
    }

    if (!upper)
    {
        return s >= lower;
    }

    if (lower < upper)
    {
        return s >= lower && s <= upper;
    }

    return s >= lower || s <= upper;
}

bool StringFilter::applicable(const nodetype_t type) const
{
    return mTarget.applicable(type);
}

bool StringFilter::inclusion() const
{
    return mInclusion;
}

bool StringFilter::inheritable() const
{
    return mInheritable;
}

StringFilter::StringFilter(MatcherPtr matcher,
                           const Target& target,
                           const bool inclusion,
                           const bool inheritable)
  : mMatcher(std::move(matcher))
  , mTarget(target)
  , mInclusion(inclusion)
  , mInheritable(inheritable)
{
}

bool StringFilter::match(const string& s) const
{
    return mMatcher->match(s);
}

NameFilter::NameFilter(MatcherPtr matcher,
                       const Target& target,
                       const bool inclusion,
                       const bool inheritable)
  : StringFilter(std::move(matcher),
                 target,
                 inclusion,
                 inheritable)
{
}

bool NameFilter::match(const RemotePathPair& p) const
{
    return StringFilter::match(p.first);
}

PathFilter::PathFilter(MatcherPtr matcher,
                       const Target& target,
                       const bool inclusion,
                       const bool inheritable)
  : StringFilter(std::move(matcher),
                 target,
                 inclusion,
                 inheritable)
{
}

bool PathFilter::match(const RemotePathPair& p) const
{
    return StringFilter::match(p.second);
}

GlobMatcher::GlobMatcher(const string &pattern, const bool caseSensitive)
  : mPattern(caseSensitive ? pattern : toUpper(pattern))
  , mCaseSensitive(caseSensitive)
{
}

bool GlobMatcher::match(const string& s) const
{
    if (mCaseSensitive)
    {
        return wildcardMatch(s, mPattern);
    }

    return wildcardMatch(toUpper(s), mPattern);
}

RegexMatcher::RegexMatcher(const string& pattern, const bool caseSensitive)
  : mRegexp(pattern, regexFlags(caseSensitive))
{
}

bool RegexMatcher::match(const string& s) const
{
    return std::regex_match(s, mRegexp);
}

bool AllTarget::applicable(const nodetype_t) const
{
    return true;
}

const AllTarget& AllTarget::instance()
{
    static AllTarget instance;

    return instance;
}

bool DirectoryTarget::applicable(const nodetype_t type) const
{
    return type == FOLDERNODE;
}

const DirectoryTarget& DirectoryTarget::instance()
{
    static DirectoryTarget instance;

    return instance;
}

bool FileTarget::applicable(const nodetype_t type) const
{
    return type == FILENODE;
}

const FileTarget& FileTarget::instance()
{
    static FileTarget instance;

    return instance;
}

bool add(const string& text, SizeFilterPtr& filter)
{
    // Handy constants.
    static const std::string maxsize = "maxsize";
    static const std::string minsize = "minsize";

    std::istringstream istream(text);

    std::string directive;

    // Extract the directive.
    std::getline(istream, directive, ':');

    // Could we extract the directive?
    if (!istream.good())
    {
        return syntaxError(text);
    }

    // Is the user specifying the upper bound?
    const auto upper = directive == maxsize;

    // Are they specifying the lower bound?
    if (!upper && directive != minsize)
    {
        // Neither lower nor upper bound.
        return syntaxError(text);
    }

    // Is the limit a bare number?
    if (!std::isdigit(istream.peek()))
    {
        // Limit isn't a number or has sign markers.
        return syntaxError(text);
    }

    std::uint64_t limit;

    // Extract the limit.
    istream >> limit;

    // Were we able to extract the limit?
    if (!istream)
    {
        return syntaxError(text);
    }

    // Has the user specified a unit suffix?
    if (!istream.eof())
    {
        std::uint64_t shift = 0;

        switch (std::tolower(istream.get()))
        {
        case 'k':
            // Kilobytes.
            shift = 10;
            break;
        case 'm':
            // Megabytes.
            shift = 20;
            break;
        case 'g':
            // Gigabytes.
            shift = 30;
            break;
        default:
            // Invalid!
            break;
        }

        // Suffix should be the last thing!
        if (!shift || !istream.eof())
        {
            return syntaxError(text);
        }

        // Can we actually represent the limit?
        const auto temp = limit << shift;

        if (limit != temp >> shift)
        {
            // Can't represent the limit.
            return unrepresentableLimitError(text);
        }

        // Shift the limit for real this time.
        limit <<= shift;
    }

    // Create the filter if necessary.
    if (!filter)
    {
        filter.reset(new SizeFilter());
    }

    // Update the appropriate bound.
    if (upper)
    {
        filter->upper = limit;
    }
    else
    {
        filter->lower = limit;
    }

    return true;
}

bool add(const string& text, StringFilterPtrVector& filters)
{
    enum FilterType
    {
        FT_NAME,
        FT_PATH
    }; /* FilterType */

    enum MatchStrategy
    {
        MS_GLOB,
        MS_REGEXP
    }; /* MatchStrategy */

    const char* m = text.data();
    const char* n = m + text.size();
    const Target* target;
    FilterType type;
    MatchStrategy strategy;
    bool caseSensitive = false;
    bool inclusion;
    bool inheritable = true;

    // What class of filter is this?
    switch (*m++)
    {
    case '+':
        // Inclusion filter.
        inclusion = true;
        break;
    case '-':
        // Exclusion filter.
        inclusion = false;
        break;
    default:
        // Invalid filter class.
        return syntaxError(text);
    }

    // What kind of node does this filter apply to?
    switch (*m)
    {
    case 'a':
        // Applies to all node types.
        ++m;
        target = &AllTarget::instance();
        break;
    case 'd':
        // Applies only to directories.
        ++m;
        target = &DirectoryTarget::instance();
        break;
    case 'f':
        // Applies only to files.
        ++m;
        target = &FileTarget::instance();
        break;
    default:
        // Default applies to all node types.
        target = &AllTarget::instance();
        break;
    }

    // What type of filter is this?
    switch (*m)
    {
    case 'N':
        // Local name filter.
        ++m;
        inheritable = false;
        type = FT_NAME;
        break;
    case 'n':
        // Subtree name filter.
        ++m;
        type = FT_NAME;
        break;
    case 'p':
        // Path filter.
        ++m;
        type = FT_PATH;
        break;
    default:
        // Default to subtree name filter.
        type = FT_NAME;
        break;
    }

    // What matching strategy does this filter use?
    switch (*m)
    {
    case 'G':
        // Case-sensitive glob match.
        caseSensitive = true;
        ++m;
        strategy = MS_GLOB;
        break;
    case 'g':
        // Case-insensitive glob match.
        ++m;
        strategy = MS_GLOB;
        break;
    case 'R':
        // Case-sensitive regexp match.
        caseSensitive = true;
        ++m;
        strategy = MS_REGEXP;
        break;
    case 'r':
        // Case-insensitive regexp match.
        ++m;
        strategy = MS_REGEXP;
        break;
    default:
        // Default to case-insensitive glob match.
        strategy = MS_GLOB;
        break;
    }

    // Make sure we're at the start of the pattern.
    if (*m++ != ':')
    {
        return syntaxError(text);
    }

    // Is the pattern effectively empty?
    if (isEmpty(m, n))
    {
        return syntaxError(text);
    }

    // Create the filter's matcher.
    MatcherPtr matcher;

    try
    {
        switch (strategy)
        {
        case MS_GLOB:
            matcher.reset(new GlobMatcher(m, caseSensitive));
            break;
        case MS_REGEXP:
            // This'll throw if the regex is malformed.
            matcher.reset(new RegexMatcher(m, caseSensitive));
            break;
        }
    }
    catch (std::regex_error&)
    {
        return syntaxError(text);
    }

    // Create the filter.
    StringFilterPtr filter;

    switch (type)
    {
    case FT_NAME:
        filter.reset(new NameFilter(std::move(matcher),
                                    *target,
                                    inclusion,
                                    inheritable));
        break;
    case FT_PATH:
        filter.reset(new PathFilter(std::move(matcher),
                                    *target,
                                    inclusion,
                                    inheritable));
        break;
    }

    // Add the filter to the chain.
    filters.emplace_back(std::move(filter));

    return true;
}


bool isEmpty(const char* m, const char* n)
{
    const char* w = m;

    while (m < n)
    {
        w += std::isspace(*m++) > 0;
    }

    return n == w;
}

FilterLoadResult normalizationError(const string& text)
{
    LOG_verbose << "Normalization error parsing: " << text;

    return FLR_FAILED;
}

std::regex::flag_type regexFlags(const bool caseSensitive)
{
    using std::regex_constants::extended;
    using std::regex_constants::icase;
    using std::regex_constants::optimize;

    const std::regex::flag_type flags = extended | optimize;

    if (caseSensitive)
    {
        return flags;
    }

    return flags | icase;
}

bool syntaxError(const string& text)
{
    LOG_verbose << "Syntax error parsing: " << text;

    return false;
}

string toUpper(string text)
{
    for (char& character : text)
    {
        character = (char)std::toupper((unsigned char)character);
    }

    return text;
}

bool unrepresentableLimitError(const string& text)
{
    LOG_verbose << "Unrepresentable size limit: " << text;

    return false;
}

} /* mega */

