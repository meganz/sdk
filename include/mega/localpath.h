/**
 * @brief Manage local paths (standars and URIs)
 */

#ifndef LOCALPATH_H
#define LOCALPATH_H

#include "mega/types.h"

#include <memory>
#include <optional>
#include <string>

namespace mega
{

// Enumeration for filesystem families
enum FileSystemType
{
    FS_UNKNOWN = -1,
    FS_APFS = 0,
    FS_HFS = 1,
    FS_EXT = 2,
    FS_FAT32 = 3,
    FS_EXFAT = 4,
    FS_NTFS = 5,
    FS_FUSE = 6,
    FS_SDCARDFS = 7,
    FS_F2FS = 8,
    FS_XFS = 9,
    FS_CIFS = 10,
    FS_NFS = 11,
    FS_SMB = 12,
    FS_SMB2 = 13,
    FS_LIFS = 14,
};

#ifdef WIN32
using string_type = std::wstring;
#else
using string_type = std::string;
#endif

class LocalPath;

enum class PathType
{
    ABSOLUTE_PATH,
    RELATIVE_PATH,
    URI_PATH,
};

class AbstractLocalPath
{
public:
    virtual ~AbstractLocalPath() {}

    virtual auto asPlatformEncoded(const bool stripPrefix) const -> string_type = 0;
    virtual std::string platformEncoded() const = 0;

    virtual bool empty() const = 0;
    virtual void clear() = 0;
    virtual LocalPath leafName() const = 0;
    virtual std::string leafOrParentName() const = 0;
    virtual void append(const LocalPath& additionalPath) = 0;
    virtual void appendWithSeparator(const LocalPath& additionalPath,
                                     const bool separatorAlways) = 0;
    virtual void prependWithSeparator(const LocalPath& additionalPath) = 0;
    virtual LocalPath prependNewWithSeparator(const LocalPath& additionalPath) const = 0;
    virtual void trimNonDriveTrailingSeparator() = 0;
    virtual bool findPrevSeparator(size_t& separatorBytePos,
                                   const FileSystemAccess& fsaccess) const = 0;
    virtual bool beginsWithSeparator() const = 0;
    virtual bool endsInSeparator() const = 0;

    virtual size_t getLeafnameByteIndex() const = 0;
    virtual LocalPath subpathFrom(const size_t bytePos) const = 0;

    virtual void changeLeaf(const LocalPath& newLeaf) = 0;

    virtual LocalPath parentPath() const = 0;

    virtual LocalPath insertFilenameSuffix(const std::string& suffix) const = 0;

    virtual bool isContainingPathOf(const LocalPath& path,
                                    size_t* subpathIndex = nullptr) const = 0;
    virtual bool nextPathComponent(size_t& subpathIndex, LocalPath& component) const = 0;
    virtual bool hasNextPathComponent(const size_t index) const = 0;

    virtual std::string toPath(const bool normalize) const = 0;

    virtual std::string toName(const FileSystemAccess& fsaccess) const = 0;

    virtual bool isRootPath() const = 0;

    virtual bool extension(std::string& extension) const = 0;
    virtual std::string extension() const = 0;

    virtual bool related(const LocalPath& other) const = 0;

    virtual bool invariant() const = 0;

    virtual std::unique_ptr<AbstractLocalPath> clone() const = 0;
    virtual PathType getPathType() const = 0;
};

/**
 * @brief Abstract base class providing platform-dependent URI handling
 *
 * Each platform should implement this interface to determine whether a given string
 * is recognized as a URI and to retrieve a representative name from that URI.
 */
class MEGA_API PlatformURIHelper
{
public:
    virtual ~PlatformURIHelper(){};
    // Returns true if string is a URI
    virtual bool isURI(const string_type& URI) = 0;
    // Returns the name of file/directory pointed by the URI
    virtual string_type getName(const string_type& uri) = 0;
};

/**
 * @brief Provides an interface to handle URIs as an identifier for files and directories.
 *
 * The URIHandler class offers static methods to detect if a given string is a URI and to extract a
 * name from that URI. This functionality should be implemented by a platform specific
 * implementation PlatformURIHelper
 */
class MEGA_API URIHandler
{
public:
    // Check if a path is recognized as a URI
    static bool isURI(const string_type& uri);

    // Retrieve the name for a given path or URI
    static string_type getName(const string_type& uri);

    // platformHelper should be kept alive during all program execution and ownership isn't taken
    static void setPlatformHelper(PlatformURIHelper* platformHelper);

private:
    static PlatformURIHelper* mPlatformHelper;
};

/**
 * @brief Class to manage device paths
 *
 * This class provide two implementations one for standard paths and other for URIs
 * For URI implementation works properly, an implementation for PlatformURIHelper should be provided
 * Standard path is implemented with a string
 * URI implementation has a string to store the URI and a vector of string to handle the leaves of
 * the tree
 */
class LocalPath
{
public:
    LocalPath() = default;
    LocalPath(LocalPath&&) noexcept = default;
    LocalPath& operator=(LocalPath&&) noexcept = default;

    LocalPath(const LocalPath& p)
    {
        if (p.mImplementation)
        {
            mImplementation = p.mImplementation->clone();
        }
    }

    LocalPath operator=(const LocalPath& p)
    {
        if (this != &p)
        {
            if (p.mImplementation)
            {
                mImplementation = p.mImplementation->clone();
            }
            else
            {
                mImplementation.reset();
            }
        }

        return *this;
    }

    ~LocalPath() {}

    AbstractLocalPath* getImpl() const
    {
        return mImplementation.get();
    }

    void setImpl(std::unique_ptr<AbstractLocalPath>&& imp)
    {
        mImplementation = std::move(imp);
    }

    // path2local / local2path are much more natural here than in FileSystemAccess
    // convert MEGA path (UTF-8) to local format
    // there is still at least one use from outside this class
    static void path2local(const std::string*, std::string*);
    static void local2path(const std::string*, std::string*, const bool normalize);
#if defined(_WIN32)
    static void local2path(const std::wstring*, std::string*, const bool normalize);
    static void path2local(const std::string*, std::wstring*);
#endif

    // Create a Localpath from a utf8 string where no character conversions or escaping is
    // necessary.
    static LocalPath fromAbsolutePath(const std::string& path);
    static LocalPath fromRelativePath(const std::string& path);
    // Build LocalPath from URI, path can have following structure
    // URI#subFolde1#subFolder2#file
    // Example:
    // "content://com.android.externalstorage.documents/tree/primary%3Adescarga%2Fvarias/#F1#"
    static LocalPath fromURIPath(const string_type& path);
    static bool isURIPath(const std::string& path);

    // Create a LocalPath from a utf8 string, making any character conversions (escaping) necessary
    // for characters that are disallowed on that filesystem. fsaccess is used to do the
    // conversion.
    static LocalPath fromRelativeName(std::string path,
                                      const FileSystemAccess& fsaccess,
                                      const FileSystemType fsType);

    // Create a LocalPath from a string that was already converted to be appropriate for a local
    // file path.
    static LocalPath fromPlatformEncodedAbsolute(const std::string& localname);
    static LocalPath fromPlatformEncodedRelative(const std::string& localname);
#ifdef WIN32
    static LocalPath fromPlatformEncodedAbsolute(std::wstring&& localname);
    static LocalPath fromPlatformEncodedRelative(std::wstring&& localname);
#endif
    // UTF-8 normalization
    static void utf8_normalize(std::string*);

    // Generates a name for a temporary file
    static LocalPath tmpNameLocal();

#ifdef _WIN32
    typedef wchar_t separator_t;
    static constexpr separator_t localPathSeparator = L'\\';
    static constexpr char localPathSeparator_utf8 = '\\';
#else
    typedef char separator_t;
    static constexpr separator_t localPathSeparator = '/';
    static constexpr char localPathSeparator_utf8 = '/';
#endif

    bool isAbsolute() const
    {
        if (mImplementation)
        {
            return mImplementation->getPathType() == PathType::ABSOLUTE_PATH;
        }
        return false;
    }

    bool isURI() const
    {
        if (mImplementation)
        {
            return mImplementation->getPathType() == PathType::URI_PATH;
        }
        return false;
    }

    bool operator==(const LocalPath& p) const;
    bool operator!=(const LocalPath& p) const;
    bool operator<(const LocalPath& p) const;

    // Returns a string_type (wstring in windows) to the string's internal representation.
    //
    // Mostly useful when we need to call platform-specific functions and
    // don't want to incur the cost of a copy.
    // Call this function with stripPrefix to false if you don't want any modification in string's
    // internal representation, otherwise prefix will be stripped in Windows (except for URI PATHS)
    auto asPlatformEncoded(const bool stripPrefix) const -> string_type;
    std::string platformEncoded() const;

    bool empty() const;
    void clear();

    LocalPath leafName() const;

    /*
     * Return the last component of the path (internally uses absolute path, no matter how the
     * instance was initialized) that could be used as an actual name.
     *
     * Examples:
     *   "D:\\foo\\bar.txt"  "bar.txt"
     *   "D:\\foo\\"         "foo"
     *   "D:\\foo"           "foo"
     *   "D:\\"              "D"
     *   "D:"                "D"
     *   "D"                 "D"
     *   "D:\\.\\"           "D"
     *   "D:\\."             "D"
     *   ".\\foo\\"          "foo"
     *   ".\\foo"            "foo"
     *   ".\\"               (as in "C:\\foo\\bar\\.\\")                             "bar"
     *   "."                 (as in "C:\\foo\\bar\\.")                               "bar"
     *   "..\\..\\"          (as in "C:\\foo\\bar\\..\\..\\")                        "C"
     *   "..\\.."            (as in "C:\\foo\\bar\\..\\..")                          "C"
     *   "..\\..\\.."        (as in "C:\\foo\\bar\\..\\..\\..", thus too far back)   "C"
     *   "/" (*nix)          ""
     */
    std::string leafOrParentName() const;

    void append(const LocalPath& additionalPath);
    void appendWithSeparator(const LocalPath& additionalPath, const bool separatorAlways);
    void prependWithSeparator(const LocalPath& additionalPath);
    LocalPath prependNewWithSeparator(const LocalPath& additionalPath) const;
    void trimNonDriveTrailingSeparator();
    bool findPrevSeparator(size_t& separatorBytePos, const FileSystemAccess& fsaccess) const;
    bool beginsWithSeparator() const;
    bool endsInSeparator() const;

    // get the index of the leaf name.  A trailing separator is considered part of the leaf.
    size_t getLeafnameByteIndex() const;
    LocalPath subpathFrom(const size_t bytePos) const;

    void changeLeaf(const LocalPath& newLeaf);

    // Return a path denoting this path's parent.
    //
    // Result is undefined if this path is a "root."
    LocalPath parentPath() const;

    LocalPath insertFilenameSuffix(const std::string& suffix) const;

    bool isContainingPathOf(const LocalPath& path, size_t* subpathIndex = nullptr) const;
    bool nextPathComponent(size_t& subpathIndex, LocalPath& component) const;
    bool hasNextPathComponent(const size_t index) const;

    // Return a utf8 representation of the LocalPath
    // No escaping or unescaping is done.
    // If this function is called with normalize false, utf8 representation of the LocalPath won't
    // be modified. Otherwise utf8 representation will be normalized
    std::string toPath(const bool normalize) const;

    // Return a utf8 representation of the LocalPath, taking into account that the LocalPath
    // may contain escaped characters that are disallowed for the filesystem.
    // Those characters are converted back (unescaped).  fsaccess is used to do the conversion.
    std::string toName(const FileSystemAccess& fsaccess) const;

    // Does this path represent a filesystem root?
    //
    // Relative paths are never considered to be a root path.
    //
    // On UNIX systems, this predicate returns true if and only if the
    // path denotes /.
    //
    // On Windows systems, this predicate returns true if and only if the
    // path specifies a drive such as C:\.
    bool isRootPath() const;

    bool extension(std::string& extension) const;

    std::string extension() const;

    // Check if this path is "related" to another.
    //
    // Two paths are related if:
    // - They are effectively identical.
    // - One path contains another.
    bool related(const LocalPath& other) const;
    bool invariant() const;

private:
#ifdef _WIN32
    static string_type toStringType(const std::wstring& path);
#endif
    static string_type toStringType(const std::string& path);
    std::unique_ptr<AbstractLocalPath> mImplementation;
};
} // mega namespace

#endif // LOCALPATH_H
