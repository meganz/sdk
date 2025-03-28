
#include <mega/filesystem.h>
#include <mega/localpath.h>
#include <mega/logging.h>
#include <mega/utils.h>

#include <memory>

#ifdef WIN32
#include <mega/win32/megafs.h>

#include <cwctype>
#elif TARGET_OS_MAC
#include <mega/osx/megafs.h>
#include <mega/osx/osxutils.h>
#elif __ANDROID__
#include "mega/android/androidFileSystem.h"
#else
#include <mega/posix/megafs.h>
#endif

namespace mega
{

PlatformURIHelper* URIHandler::mPlatformHelper = nullptr;

bool URIHandler::isURI(const string_type& uri)
{
    if (mPlatformHelper)
    {
        return mPlatformHelper->isURI(uri);
    }

    return false;
}

std::optional<string_type> URIHandler::getName(const string_type& uri)
{
    if (mPlatformHelper)
    {
        return mPlatformHelper->getName(uri);
    }

    return std::nullopt;
}

std::optional<string_type> URIHandler::getParentURI(const string_type& uri)
{
    if (mPlatformHelper)
    {
        return mPlatformHelper->getParentURI(uri);
    }

    return std::nullopt;
}

std::optional<string_type> URIHandler::getPath(const string_type& uri)
{
    if (mPlatformHelper)
    {
        return mPlatformHelper->getPath(uri);
    }

    return std::nullopt;
}

void URIHandler::setPlatformHelper(PlatformURIHelper* platformHelper)
{
    mPlatformHelper = platformHelper;
}

// anonymous namespace
namespace
{
class Path: public mega::AbstractLocalPath
{
public:
    ~Path() override {}

    std::string platformEncoded() const override;
    auto asPlatformEncoded(const bool stripPrefix) const -> string_type override;

    bool empty() const override;
    void clear() override;
    LocalPath leafName() const override;
    std::string leafOrParentName() const override;
    void append(const LocalPath& additionalPath) override;
    void appendWithSeparator(const LocalPath& additionalPath, const bool separatorAlways) override;

    void prependWithSeparator(const LocalPath& additionalPath) override;
    LocalPath prependNewWithSeparator(const LocalPath& additionalPath) const override;
    void trimNonDriveTrailingSeparator() override;
    bool findPrevSeparator(size_t& separatorBytePos,
                           const FileSystemAccess& fsaccess) const override;

    bool beginsWithSeparator() const override;
    bool endsInSeparator() const override;

    size_t getLeafnameByteIndex() const override;
    LocalPath subpathFrom(const size_t bytePos) const override;

    void changeLeaf(const LocalPath& newLeaf) override;

    LocalPath parentPath() const override;

    LocalPath insertFilenameSuffix(const std::string& suffix) const override;

    std::string toPath(const bool normalize) const override;

    std::string toName(const FileSystemAccess& fsaccess) const override;

    bool isRootPath() const override;

    bool extension(std::string& extension) const override;
    std::string extension() const override;

    bool related(const LocalPath& other) const override;

    PathType getPathType() const override
    {
        return mPathType;
    }

    bool invariant() const override;

    // helper functions to ensure proper format especially on windows
    void normalizeAbsolute();

    Path(const string_type& path):
        mLocalpath(path)
    {}

    Path() = default;

    std::unique_ptr<AbstractLocalPath> clone() const override
    {
        return std::make_unique<Path>(*this);
    }

    void setPathType(const PathType type)
    {
        mPathType = type;
    }

    std::string serialize() const override;
    bool unserialize(const std::string& data) override;

    string_type getRealPath() const override;

private:
    string_type mLocalpath;
    // Track whether this LocalPath is from the root of a filesystem (ie, an absolute path)
    // It makes a big difference for windows, where we must prepend \\?\ prefix
    // to be able to access long paths, paths ending with space or `.`, etc
    PathType mPathType{PathType::RELATIVE_PATH};
    void removeTrailingSeparators();
    void truncate(size_t bytePos);
    LocalPath subpathTo(size_t bytePos) const;
    bool findNextSeparator(size_t& separatorBytePos) const override;
};

class PathURI: public mega::AbstractLocalPath
{
public:
    ~PathURI() override {}

    auto asPlatformEncoded(const bool stripPrefix) const -> string_type override;
    std::string platformEncoded() const override;

    bool empty() const override;
    void clear() override;
    LocalPath leafName() const override;
    std::string leafOrParentName() const override;
    void append(const LocalPath& additionalPath) override;
    void appendWithSeparator(const LocalPath& additionalPath, const bool separatorAlways) override;

    void prependWithSeparator(const LocalPath& additionalPath) override;
    LocalPath prependNewWithSeparator(const LocalPath& additionalPath) const override;
    void trimNonDriveTrailingSeparator() override;
    bool findPrevSeparator(size_t& separatorBytePos,
                           const FileSystemAccess& fsaccess) const override;

    bool beginsWithSeparator() const override;
    bool endsInSeparator() const override;

    size_t getLeafnameByteIndex() const override;
    LocalPath subpathFrom(const size_t bytePos) const override;

    void changeLeaf(const LocalPath& newLeaf) override;

    LocalPath parentPath() const override;

    LocalPath insertFilenameSuffix(const std::string& suffix) const override;

    std::string toPath(const bool normalize) const override;

    std::string toName(const FileSystemAccess& fsaccess) const override;

    bool isRootPath() const override;

    PathType getPathType() const override
    {
        return PathType::URI_PATH;
    }

    bool extension(std::string& extension) const override;
    std::string extension() const override;

    bool related(const LocalPath& other) const override;

    bool invariant() const override;

    PathURI(const string_type& path):
        mUri(path)
    {}

    PathURI() = default;

    std::unique_ptr<AbstractLocalPath> clone() const override
    {
        return std::make_unique<PathURI>(*this);
    }

    std::string serialize() const override;
    bool unserialize(const std::string& data) override;
    string_type getRealPath() const override;

private:
    // String allows to identify a file or folder
    string_type mUri;
    // Chain of elements that identify leaves from the tree
    // It isn't possible concat element as in a standard path
    // They are stored as elements in a vector
    std::vector<string_type> mAuxPath;
    void removeLastElement();
    bool findNextSeparator(size_t& separatorBytePos) const override;
};
} // end anonymous namespace

class LocalPathImplementationHelper
{
public:
    static const PathURI* getPathURI(const LocalPath& p)
    {
        return dynamic_cast<const PathURI*>(p.getImpl());
    }

    static PathURI* getPathURI(LocalPath& p)
    {
        return dynamic_cast<PathURI*>(p.getImpl());
    }

    static const Path* getPathLocal(const LocalPath& p)
    {
        return dynamic_cast<const Path*>(p.getImpl());
    }

    static Path* getPathLocal(LocalPath& p)
    {
        return dynamic_cast<Path*>(p.getImpl());
    }

    template<typename T>
    static LocalPath buildLocalPath(const T& source)
    {
        static_assert(std::is_base_of<AbstractLocalPath, T>::value,
                      "T must be derived from AbstractLocalPath");

        LocalPath localPath;
        auto aux = std::make_unique<T>(source);
        localPath.setImpl(std::move(aux));
        return localPath;
    }

    template<typename StringT>
    static LocalPath buildFromPlatformEncoded(StringT&& encodedPath,
                                              const PathType pathType,
                                              const bool normalize)
    {
        LocalPath p;

        auto aux = std::make_unique<Path>(std::forward<StringT>(encodedPath));
        if (normalize)
            aux->normalizeAbsolute();
        aux->setPathType(pathType);

        p.setImpl(std::move(aux));
        return p;
    }

    static string_type convertPlatformEncoded(const std::string& path)
    {
#ifdef _WIN32
        assert((path.size() % sizeof(wchar_t) == 0) && "size is not a multiple of wchar_t !!!");
        return std::wstring(reinterpret_cast<const wchar_t*>(path.data()),
                            path.size() / sizeof(wchar_t));
#else
        return path;
#endif
    }
};

LocalPath::LocalPath():
    mImplementation(std::make_unique<Path>())
{
    assert(invariant());
}

LocalPath::LocalPath(LocalPath&& p) noexcept:
    mImplementation(std::move(p.mImplementation))
{
    assert(invariant());
}

LocalPath& LocalPath::operator=(LocalPath&& p) noexcept
{
    if (this != &p)
    {
        if (p.mImplementation)
        {
            mImplementation = std::move(p.mImplementation);
        }
        else
        {
            mImplementation.reset(new Path());
        }
    }
    assert(invariant());
    return *this;
}

LocalPath::LocalPath(const LocalPath& p)
{
    if (p.mImplementation)
    {
        mImplementation = p.mImplementation->clone();
    }
    else
    {
        mImplementation.reset(new Path());
    }
    assert(invariant());
}

LocalPath LocalPath::operator=(const LocalPath& p)
{
    if (this != &p)
    {
        if (p.mImplementation)
        {
            mImplementation = p.mImplementation->clone();
        }
        else
        {
            mImplementation.reset(new Path());
        }
    }
    assert(invariant());
    return *this;
}

#if defined(_WIN32)
// convert UTF-8 to Windows Unicode
void LocalPath::path2local(const std::string* path, std::string* local)
{
    // make space for the worst case
    local->resize((path->size() + 1) * sizeof(wchar_t));

    const auto len = MultiByteToWideChar(CP_UTF8,
                                         0,
                                         path->c_str(),
                                         -1,
                                         (wchar_t*)local->data(),
                                         int(local->size() / sizeof(wchar_t) + 1));
    if (len)
    {
        // resize to actual result
        local->resize(sizeof(wchar_t) * (len - 1));
    }
    else
    {
        local->clear();
    }
}

// convert UTF-8 to Windows Unicode
void LocalPath::path2local(const std::string* path, std::wstring* local)
{
    // make space for the worst case
    local->resize(path->size() + 2);

    const auto len = MultiByteToWideChar(CP_UTF8,
                                         0,
                                         path->c_str(),
                                         -1,
                                         const_cast<wchar_t*>(local->data()),
                                         int(local->size()));

    if (len)
    {
        // resize to actual result
        local->resize(len - 1);
    }
    else
    {
        local->clear();
    }
}

// convert Windows Unicode to UTF-8
void LocalPath::local2path(const std::string* local, std::string* path, const bool normalize)
{
    path->resize((local->size() + 1) * 4 / sizeof(wchar_t) + 1);

    path->resize(WideCharToMultiByte(CP_UTF8,
                                     0,
                                     (wchar_t*)local->data(),
                                     int(local->size() / sizeof(wchar_t)),
                                     (char*)path->data(),
                                     int(path->size()),
                                     NULL,
                                     NULL));

    if (normalize)
        utf8_normalize(path);
}

void LocalPath::local2path(const std::wstring* local, std::string* path, const bool normalize)
{
    path->resize((local->size() * sizeof(wchar_t) + 1) * 4 / sizeof(wchar_t) + 1);

    path->resize(WideCharToMultiByte(CP_UTF8,
                                     0,
                                     local->data(),
                                     int(local->size()),
                                     (char*)path->data(),
                                     int(path->size()),
                                     NULL,
                                     NULL));

    if (normalize)
        utf8_normalize(path);
}

#else

void LocalPath::path2local(const std::string* path, std::string* local)
{
#ifdef __MACH__
    path2localMac(path, local);
#else
    *local = *path;
#endif
}

void LocalPath::local2path(const std::string* local, std::string* path, const bool normalize)
{
    *path = *local;
    if (normalize)
        LocalPath::utf8_normalize(path);
}

#endif

LocalPath LocalPath::fromAbsolutePath(const std::string& path)
{
    LocalPath p;
    string_type newPath;
    path2local(&path, &newPath);
    if (LocalPath::isURIPath(path))
    {
        p.mImplementation = std::make_unique<PathURI>(newPath);
    }
    else
    {
        auto aux = std::make_unique<Path>(newPath);
        aux->normalizeAbsolute();
        aux->setPathType(PathType::ABSOLUTE_PATH);
        p.setImpl(std::move(aux));
    }

    return p;
}

LocalPath LocalPath::fromRelativePath(const std::string& path)
{
    LocalPath p;
    string_type newPath;
    path2local(&path, &newPath);
    auto aux = std::make_unique<Path>(newPath);
    aux->setPathType(PathType::RELATIVE_PATH);
    p.setImpl(std::move(aux));
    assert(p.invariant());
    return p;
}

LocalPath LocalPath::fromURIPath(const string_type& path)
{
    LocalPath p;
    std::string auxStr;
    LocalPath::local2path(&path, &auxStr, false);
    std::unique_ptr<PathURI> aux;
    p.mImplementation = std::make_unique<PathURI>(path);
    return p;
}

#ifdef _WIN32
string_type LocalPath::toStringType(const std::wstring& path)
{
    return path;
}
#endif

string_type LocalPath::toStringType(const std::string& path)
{
    string_type converted;
    LocalPath::path2local(&path, &converted);
    return converted;
}

bool LocalPath::isURIPath(const std::string& path)
{
    return URIHandler::isURI(toStringType(path));
}

LocalPath LocalPath::fromRelativeName(std::string path,
                                      const FileSystemAccess& fsaccess,
                                      const FileSystemType fsType)
{
    fsaccess.escapefsincompatible(&path, fsType);
    return fromRelativePath(path);
}

LocalPath LocalPath::fromPlatformEncodedAbsolute(const std::string& path)
{
    using helper = LocalPathImplementationHelper;
    if (const auto stringTypePath = toStringType(path); URIHandler::isURI(stringTypePath))
    {
        return fromURIPath(stringTypePath);
    }

    return helper::buildFromPlatformEncoded(helper::convertPlatformEncoded(path),
                                            PathType::ABSOLUTE_PATH,
                                            true);
}

#if defined(_WIN32)
LocalPath LocalPath::fromPlatformEncodedRelative(wstring&& wpath)
{
    using helper = LocalPathImplementationHelper;
    return helper::buildFromPlatformEncoded(std::move(wpath), PathType::RELATIVE_PATH, false);
}

LocalPath LocalPath::fromPlatformEncodedAbsolute(wstring&& wpath)
{
    using helper = LocalPathImplementationHelper;
    if (const auto stringTypePath = toStringType(wpath); URIHandler::isURI(stringTypePath))
    {
        return fromURIPath(stringTypePath);
    }

    return helper::buildFromPlatformEncoded(std::move(wpath), PathType::ABSOLUTE_PATH, true);
}
#endif

LocalPath LocalPath::fromPlatformEncodedRelative(const std::string& path)
{
    using helper = LocalPathImplementationHelper;
    return helper::buildFromPlatformEncoded(helper::convertPlatformEncoded(path),
                                            PathType::RELATIVE_PATH,
                                            false);
}

void LocalPath::utf8_normalize(std::string* filename)
{
    if (!filename)
        return;

    const char* cfilename = filename->c_str();
    const auto fnsize = filename->size();
    std::string result;

    for (size_t i = 0; i < fnsize;)
    {
        // allow NUL bytes between valid UTF-8 sequences
        if (!cfilename[i])
        {
            result.append("", 1);
            i++;
            continue;
        }

        const char* substring = cfilename + i;
        char* normalized = (char*)utf8proc_NFC((uint8_t*)substring);

        if (!normalized)
        {
            filename->clear();
            return;
        }

        result.append(normalized);
        free(normalized);

        i += strlen(substring);
    }

    *filename = std::move(result);
}

std::atomic<unsigned> LocalPath_tmpNameLocal_counter{};

LocalPath LocalPath::tmpNameLocal()
{
    char buf[128];
    snprintf(buf,
             sizeof(buf),
             ".getxfer.%lu.%u.mega",
             mega::getCurrentPid(),
             ++LocalPath_tmpNameLocal_counter);
    return LocalPath::fromRelativePath(buf);
}

std::string LocalPath::serialize() const
{
    return mImplementation ? mImplementation->serialize() : std::string{};
}

std::optional<LocalPath> LocalPath::unserialize(const std::string& d)
{
    CacheableReader r(d);
    uint8_t type;
    r.unserializeu8(type);
    LocalPath p{};
    if (static_cast<PathType>(type) == PathType::URI_PATH)
    {
        p.mImplementation = std::make_unique<PathURI>();
    }
    else
    {
        p.mImplementation = std::make_unique<Path>();
    }

    if (p.mImplementation->unserialize(d))
    {
        return p;
    }

    return std::nullopt;
}

bool LocalPath::operator==(const LocalPath& p) const
{
    return toPath(false) == p.toPath(false);
}

bool LocalPath::operator!=(const LocalPath& p) const
{
    return toPath(false) != p.toPath(false);
}

bool LocalPath::operator<(const LocalPath& p) const
{
    return toPath(false) < p.toPath(false);
}

auto LocalPath::asPlatformEncoded(const bool stripPrefix) const -> string_type
{
    if (mImplementation)
    {
        return mImplementation->asPlatformEncoded(stripPrefix);
    }

    return string_type{};
}

std::string LocalPath::platformEncoded() const
{
    if (mImplementation)
    {
        return mImplementation->platformEncoded();
    }

    return std::string{};
}

bool LocalPath::empty() const
{
    if (mImplementation)
    {
        return mImplementation->empty();
    }

    return true;
}

void LocalPath::clear()
{
    mImplementation.reset(new Path());
    assert(invariant());
}

LocalPath LocalPath::leafName() const
{
    if (mImplementation)
    {
        return mImplementation->leafName();
    }

    return LocalPath{};
}

std::string LocalPath::leafOrParentName() const
{
    if (mImplementation)
    {
        return mImplementation->leafOrParentName();
    }

    return std::string{};
}

void LocalPath::append(const LocalPath& additionalPath)
{
    if (mImplementation)
    {
        mImplementation->append(additionalPath);
    }
}

void LocalPath::appendWithSeparator(const LocalPath& additionalPath, const bool separatorAlways)
{
    if (mImplementation)
    {
        mImplementation->appendWithSeparator(additionalPath, separatorAlways);
    }
}

void LocalPath::prependWithSeparator(const LocalPath& additionalPath)
{
    if (isAbsolute() || isURI())
    {
        LOG_err << "Invalid parameter type (prependWithSeparator)";
        assert(false);
        return;
    }

    if (additionalPath.isURI())
    {
        const auto previousPath = this->toPath(false);
        mImplementation =
            std::make_unique<PathURI>(*LocalPathImplementationHelper::getPathURI(additionalPath));
        auto leaves = splitString<std::vector<string>>(previousPath, localPathSeparator);
        for (auto& leaf: leaves)
        {
            mImplementation->appendWithSeparator(LocalPath::fromRelativePath(leaf), true);
        }

        return;
    }
    else if (!mImplementation)
    {
        mImplementation = std::make_unique<Path>();
    }

    mImplementation->prependWithSeparator(additionalPath);
}

LocalPath LocalPath::prependNewWithSeparator(const LocalPath& additionalPath) const
{
    if (additionalPath.isURI())
    {
        LocalPath newPath = additionalPath;
        newPath.append(*this);
        return newPath;
    }

    if (mImplementation)
    {
        return mImplementation->prependNewWithSeparator(additionalPath);
    }

    return LocalPath{};
}

void LocalPath::trimNonDriveTrailingSeparator()
{
    if (mImplementation)
    {
        mImplementation->trimNonDriveTrailingSeparator();
    }
}

bool LocalPath::findPrevSeparator(size_t& separatorBytePos, const FileSystemAccess& fsaccess) const
{
    if (mImplementation)
    {
        return mImplementation->findPrevSeparator(separatorBytePos, fsaccess);
    }

    return false;
}

bool LocalPath::beginsWithSeparator() const
{
    if (mImplementation)
    {
        return mImplementation->beginsWithSeparator();
    }

    return false;
}

bool LocalPath::endsInSeparator() const
{
    if (mImplementation)
    {
        return mImplementation->endsInSeparator();
    }

    return false;
}

size_t LocalPath::getLeafnameByteIndex() const
{
    if (mImplementation)
    {
        return mImplementation->getLeafnameByteIndex();
    }

    return 0u;
}

LocalPath LocalPath::subpathFrom(size_t bytePos) const
{
    if (mImplementation)
    {
        return mImplementation->subpathFrom(bytePos);
    }
    return LocalPath{};
}

void LocalPath::changeLeaf(const LocalPath& newLeaf)
{
    if (mImplementation)
    {
        mImplementation->changeLeaf(newLeaf);
    }
}

LocalPath LocalPath::parentPath() const
{
    if (mImplementation)
    {
        return mImplementation->parentPath();
    }

    return LocalPath{};
}

LocalPath LocalPath::insertFilenameSuffix(const std::string& suffix) const
{
    if (mImplementation)
    {
        return mImplementation->insertFilenameSuffix(suffix);
    }
    return LocalPath{};
}

bool LocalPath::isContainingPathOf(const LocalPath& path, size_t* subpathIndex) const
{
    if (mImplementation)
    {
        return mImplementation->isContainingPathOf(path, subpathIndex);
    }

    return false;
}

bool LocalPath::nextPathComponent(size_t& subpathIndex, LocalPath& component) const
{
    if (mImplementation)
    {
        return mImplementation->nextPathComponent(subpathIndex, component);
    }

    return false;
}

bool LocalPath::hasNextPathComponent(const size_t index) const
{
    if (mImplementation)
    {
        return mImplementation->hasNextPathComponent(index);
    }

    return false;
}

std::string LocalPath::toPath(const bool normalize) const
{
    if (mImplementation)
    {
        return mImplementation->toPath(normalize);
    }

    return {};
}

std::string LocalPath::toName(const FileSystemAccess& fsaccess) const
{
    if (mImplementation)
    {
        return mImplementation->toName(fsaccess);
    }

    return {};
}

bool LocalPath::isRootPath() const
{
    if (mImplementation)
    {
        return mImplementation->isRootPath();
    }

    return false;
}

bool LocalPath::extension(std::string& extension) const
{
    if (mImplementation)
    {
        return mImplementation->extension(extension);
    }

    return false;
}

std::string LocalPath::extension() const
{
    if (mImplementation)
    {
        return mImplementation->extension();
    }

    return {};
}

bool LocalPath::related(const LocalPath& other) const
{
    if (mImplementation)
    {
        return mImplementation->related(other);
    }

    return false;
}

bool LocalPath::invariant() const
{
    if (mImplementation)
    {
        return mImplementation->invariant();
    }

    return false;
}

string_type LocalPath::getRealPath() const
{
    if (mImplementation)
    {
        return mImplementation->getRealPath();
    }

    return {};
}

auto Path::asPlatformEncoded([[maybe_unused]] const bool skipPrefix) const -> string_type
{
#ifdef WIN32
    // Caller wants the prefix intact.
    if (!skipPrefix)
        return mLocalpath;

    // Path doesn't begin with the prefix.
    if (mLocalpath.size() < 4 || mLocalpath.compare(0, 4, L"\\\\?\\"))
        return mLocalpath;

    // Path doesn't begin wih the UNC prefix.
    if (mLocalpath.size() < 8 || mLocalpath.compare(4, 4, L"UNC\\"))
        return mLocalpath.substr(4);

    return mLocalpath.substr(8);
#else
    return mLocalpath;
#endif
}

std::string Path::platformEncoded() const
{
#ifdef WIN32
    // this function is typically used where we need to pass a file path to the client app, which
    // expects utf16 in a std::string buffer some other backwards compatible cases need this format
    // also, eg. serialization
    std::string outstr;

    if (mLocalpath.size() >= 4 && 0 == mLocalpath.compare(0, 4, L"\\\\?\\", 4))
    {
        if (0 == mLocalpath.compare(4, 4, L"UNC\\", 4))
        {
            // when a path leaves LocalPath, we can remove prefix which is only needed internally
            outstr.resize((mLocalpath.size() - 6) * sizeof(wchar_t));
            memcpy(const_cast<char*>(outstr.data()),
                   mLocalpath.data() + 0,
                   2 * sizeof(wchar_t)); // "\\\\"
            memcpy(const_cast<char*>(outstr.data()) + 4,
                   mLocalpath.data() + 8,
                   (mLocalpath.size() - 8) * sizeof(wchar_t));
        }
        else
        {
            // when a path leaves LocalPath, we can remove prefix which is only needed internally
            outstr.resize((mLocalpath.size() - 4) * sizeof(wchar_t));
            memcpy(const_cast<char*>(outstr.data()),
                   mLocalpath.data() + 4,
                   (mLocalpath.size() - 4) * sizeof(wchar_t));
        }
    }
    else
    {
        outstr.resize(mLocalpath.size() * sizeof(wchar_t));
        memcpy(const_cast<char*>(outstr.data()),
               mLocalpath.data(),
               mLocalpath.size() * sizeof(wchar_t));
    }
    return outstr;
#else
    // for non-windows, it's just the same utf8 string we use anyway
    return mLocalpath;
#endif
}

bool Path::empty() const
{
    assert(invariant());
    return mLocalpath.empty();
}

void Path::clear()
{
    assert(invariant());
    mLocalpath.clear();
    mPathType = PathType::RELATIVE_PATH;
    assert(invariant());
}

LocalPath Path::leafName() const
{
    Path result;
    assert(invariant());
    auto p = mLocalpath.find_last_of(LocalPath::localPathSeparator);
    p = p == std::string::npos ? 0 : p + 1;
    result.mLocalpath = mLocalpath.substr(p, mLocalpath.size() - p);
    assert(result.invariant());
    return LocalPathImplementationHelper::buildLocalPath(result);
}

std::string Path::leafOrParentName() const
{
    assert(invariant());
    LocalPath name;
    // win32: normalizeAbsolute() does not work with paths like "D:\\foo\\..\\bar.txt". TODO ?
    FSACCESS_CLASS().expanselocalpath(LocalPathImplementationHelper::buildLocalPath(*this), name);
    Path* auxPath = LocalPathImplementationHelper::getPathLocal(name);
    assert(auxPath);
    auxPath->removeTrailingSeparators();

    if (name.empty())
    {
        return std::string{};
    }

#ifdef WIN32
    auto nameStr = name.asPlatformEncoded(false);
    if (nameStr.back() == L':')
    {
        // drop trailing ':'
        string n = name.toPath(true);
        n.pop_back();
        return n;
    }
#endif

    return name.leafName().toPath(true);
}

void Path::append(const LocalPath& additionalPath)
{
    assert(!additionalPath.isAbsolute() && !additionalPath.isURI());
    assert(invariant());
    mLocalpath.append(additionalPath.asPlatformEncoded(false));
    assert(invariant());
}

void Path::appendWithSeparator(const LocalPath& additionalPath, const bool separatorAlways)
{
    if (additionalPath.isAbsolute() || additionalPath.isURI())
    {
        LOG_err << "Invalid parameter type (appendWithSeparator)";
        assert(false);
        return;
    }

#ifdef USE_IOS
    bool originallyUsesAppBasePath =
        getPathType() == PathType::ABSOLUTE_PATH && (empty() || !beginsWithSeparator());
#endif

    if (separatorAlways || mLocalpath.size())
    {
        // still have to be careful about appending a \ to F:\ for example, on windows, which
        // produces an invalid path
        const Path* p = LocalPathImplementationHelper::getPathLocal(additionalPath);
        assert(p);
        if (!(endsInSeparator() || p->beginsWithSeparator()))
        {
            mLocalpath.append(1, LocalPath::localPathSeparator);
        }
    }

    mLocalpath.append(additionalPath.asPlatformEncoded(false));

#ifdef USE_IOS
    if (originallyUsesAppBasePath)
    {
        while (!empty() && beginsWithSeparator())
        {
            mLocalpath.erase(0, 1);
        }
    }
#endif

    assert(invariant());
}

void Path::prependWithSeparator(const LocalPath& additionalPath)
{
    if (mPathType == PathType::ABSOLUTE_PATH)
    {
        LOG_err << "Invalid parameter type (prependWithSeparator)";
        return;
    }

    assert(invariant());
    // no additional separator if there is already one after
    if (!mLocalpath.empty() && mLocalpath[0] != LocalPath::localPathSeparator)
    {
        // no additional separator if there is already one before
        if (!(beginsWithSeparator() || additionalPath.endsInSeparator()))
        {
            mLocalpath.insert(0, 1, LocalPath::localPathSeparator);
        }
    }

    mLocalpath.insert(0, additionalPath.asPlatformEncoded(false));
    mPathType = additionalPath.isAbsolute() ? PathType::ABSOLUTE_PATH : PathType::RELATIVE_PATH;
    assert(invariant());
}

LocalPath Path::prependNewWithSeparator(const LocalPath& additionalPath) const
{
    Path p = *this;
    p.prependWithSeparator(additionalPath);
    return LocalPathImplementationHelper::buildLocalPath(p);
}

void Path::trimNonDriveTrailingSeparator()
{
    assert(invariant());
    if (endsInSeparator())
    {
// ok so the last character is a directory separator.  But don't remove it for eg. F:\ on windows
#ifdef WIN32
        if (mLocalpath.size() > 1 && mLocalpath[mLocalpath.size() - 2] == L':')
        {
            return;
        }
#endif

        mLocalpath.resize(mLocalpath.size() - 1);
    }
    assert(invariant());
}

bool Path::findPrevSeparator(size_t& separatorBytePos, const FileSystemAccess&) const
{
    assert(invariant());
    separatorBytePos = mLocalpath.rfind(LocalPath::localPathSeparator, separatorBytePos);
    return separatorBytePos != std::string::npos;
}

bool Path::endsInSeparator() const
{
    assert(invariant());
    return !mLocalpath.empty() && mLocalpath.back() == LocalPath::localPathSeparator;
}

size_t Path::getLeafnameByteIndex() const
{
    assert(invariant());
    size_t p = mLocalpath.size();

    while (p && (p -= 1))
    {
        if (mLocalpath[p] == LocalPath::localPathSeparator)
        {
            p += 1;
            break;
        }
    }
    return p;
}

LocalPath Path::subpathFrom(const size_t bytePos) const
{
    assert(invariant());
    Path result;
    result.mLocalpath = mLocalpath.substr(bytePos);
    assert(result.invariant());
    return LocalPathImplementationHelper::buildLocalPath(result);
}

void Path::changeLeaf(const LocalPath& newLeaf)
{
    const auto leafIndex = getLeafnameByteIndex();
    truncate(leafIndex);
    appendWithSeparator(newLeaf, false);
}

LocalPath Path::parentPath() const
{
    assert(invariant());
    return subpathTo(getLeafnameByteIndex());
}

LocalPath Path::insertFilenameSuffix(const std::string& suffix) const
{
    assert(invariant());

    const auto dotindex = mLocalpath.find_last_of('.');
    const auto sepindex = mLocalpath.find_last_of(LocalPath::localPathSeparator);

    Path result, extension;

    if (dotindex == std::string::npos || (sepindex != std::string::npos && sepindex > dotindex))
    {
        result.mLocalpath = mLocalpath;
        result.mPathType = mPathType;
    }
    else
    {
        result.mLocalpath = mLocalpath.substr(0, dotindex);
        result.mPathType = mPathType;
        extension.mLocalpath = mLocalpath.substr(dotindex);
    }

    result.mLocalpath +=
        LocalPath::fromRelativePath(suffix).asPlatformEncoded(false) + extension.mLocalpath;
    assert(result.invariant());
    return LocalPathImplementationHelper::buildLocalPath(result);
}

bool AbstractLocalPath::isContainingPathOf(const LocalPath& path, size_t* subpathIndex) const
{
    string_type parameterLocalPath{path.getRealPath()};
    string_type thisLocalPath{getRealPath()};

    if (parameterLocalPath.size() >= thisLocalPath.size() &&
        !Utils::pcasecmp(parameterLocalPath, thisLocalPath, thisLocalPath.size()))
    {
        if (parameterLocalPath.size() == thisLocalPath.size())
        {
            if (subpathIndex)
                *subpathIndex = thisLocalPath.size();
            return true;
        }
        else if (parameterLocalPath[thisLocalPath.size()] == LocalPath::localPathSeparator)
        {
            if (subpathIndex)
                *subpathIndex = thisLocalPath.size() + 1;
            return true;
        }
        else if (!thisLocalPath.empty() &&
                 parameterLocalPath[thisLocalPath.size() - 1] == LocalPath::localPathSeparator)
        {
            if (subpathIndex)
                *subpathIndex = thisLocalPath.size();
            return true;
        }
    }

    return false;
}

bool AbstractLocalPath::nextPathComponent(size_t& subpathIndex, LocalPath& component) const
{
    string_type parameterLocalPath{component.getRealPath()};
    string_type thisLocalPath{getRealPath()};

    while (subpathIndex < thisLocalPath.size() &&
           thisLocalPath[subpathIndex] == LocalPath::localPathSeparator)
    {
        ++subpathIndex;
    }

    const auto start = subpathIndex;
    if (start >= thisLocalPath.size())
    {
        return false;
    }
    else if (findNextSeparator(subpathIndex))
    {
        parameterLocalPath = thisLocalPath.substr(start, subpathIndex - start);
        component = LocalPath::fromPlatformEncodedRelative(std::move(parameterLocalPath));
        assert(component.invariant());
        return true;
    }
    else
    {
        parameterLocalPath = thisLocalPath.substr(start, thisLocalPath.size() - start);
        component.clear();
        component = LocalPath::fromPlatformEncodedRelative(std::move(parameterLocalPath));
        subpathIndex = thisLocalPath.size();
        assert(component.invariant());
        return true;
    }
}

bool AbstractLocalPath::hasNextPathComponent(size_t index) const
{
    assert(invariant());
    string_type thisLocalPath{getRealPath()};
    return index < thisLocalPath.size();
}

std::string Path::toPath(const bool normalize) const
{
    assert(invariant());
    std::string path;
    LocalPath::local2path(&mLocalpath, &path, normalize);

#ifdef WIN32
    if (path.size() >= 4 && 0 == path.compare(0, 4, "\\\\?\\", 4))
    {
        if (0 == mLocalpath.compare(4, 4, L"UNC\\", 4))
        {
            // when a path leaves LocalPath, we can remove prefix which is only needed internally
            path.erase(2, 6);
        }
        else
        {
            // when a path leaves LocalPath, we can remove prefix which is only needed internally
            path.erase(0, 4);
        }
    }
#endif

    return path;
}

std::string Path::toName(const FileSystemAccess& fsaccess) const
{
    std::string name = toPath(true);
    fsaccess.unescapefsincompatible(&name);
    return name;
}

bool Path::isRootPath() const
{
#ifdef WIN32
    if (mPathType != PathType::ABSOLUTE_PATH)
        return false;

    static const std::wstring prefix = L"\\\\?\\";

    std::size_t length = mLocalpath.size();
    std::size_t offset = 0;

    // Skip namespace prefix if present.
    if (mLocalpath.size() > prefix.size() && !mLocalpath.compare(0, prefix.size(), prefix))
        offset = prefix.size();

    // Path is too short to contain a drive letter.
    if (offset + 2 > mLocalpath.size())
        return false;

    // Convenience.
    std::wint_t drive = mLocalpath[offset++];

    // Drive letter's outside domain of wchar_t.
    if (drive < WCHAR_MIN || drive > WCHAR_MAX)
        return false;

    // Drive letter isn't actually a drive letter.
    if (!std::iswalpha(drive))
        return false;

    // Path doesn't contain drive letter separator.
    if (mLocalpath[offset++] != L':')
        return false;

    // Path must end with a directory separator.
    if (length > offset)
        return mLocalpath[offset++] == L'\\' && length == offset;

    return true;
#else
    if (mPathType == PathType::ABSOLUTE_PATH)
        return mLocalpath.size() == 1 && mLocalpath.back() == '/';

    return false;
#endif
}

bool Path::extension(std::string& extension) const
{
    return extensionOf(leafName().toPath(false), extension);
}

std::string Path::extension() const
{
    return extensionOf(leafName().toPath(false));
}

bool Path::related(const LocalPath& other) const
{
    assert(other.isAbsolute());

    // This path is shorter: It may contain other.
    if (mLocalpath.size() <= other.toPath(true).size())
        return isContainingPathOf(other);

    // Other is shorter: It may contain this path.
    return other.isContainingPathOf(LocalPathImplementationHelper::buildLocalPath(*this));
}

void Path::removeTrailingSeparators()
{
    assert(invariant());

    // Remove trailing separator if present.
    while (mLocalpath.size() > 1 && mLocalpath.back() == LocalPath::localPathSeparator)
    {
        mLocalpath.pop_back();
    }

    assert(invariant());
}

bool Path::invariant() const
{
#ifdef USE_IOS
    // iOS is a tricky case.
    // We need to be able to use and persist absolute paths.  however on iOS app restart,
    // our app base path may be different.  So, we only record the path beyond that app
    // base path.   That is what the app supplies for absolute paths, that's what is persisted.
    // Actual filesystem functions passed such an "absolute" path will prepend the app base path
    // unless it already started with /
    // and that's how it worked before we added the "absolute" feature to LocalPath.
    // As a result of that though, there's nothing to adjust or check here for iOS.
#elif WIN32
    if (mPathType == PathType::ABSOLUTE_PATH)
    {
        // if it starts with \\ then it's absolute, either by us or provided
        if (mLocalpath.size() >= 2 && mLocalpath[0] == '\\' && mLocalpath[1] == '\\')
            return true;
        // otherwise it must contain a drive letter
        if (mLocalpath.find(L":") == string_type::npos)
            return false;
        // ok so probably relative then, but double check:
        if (PathIsRelativeW(mLocalpath.c_str()))
            return false;
    }
    else
    {
        // must not contain a drive letter
        if (mLocalpath.find(L":") != string_type::npos)
            return false;

        // must not start "\\"
        if (mLocalpath.size() >= 2 && mLocalpath.substr(0, 2) == L"\\\\")
            return false;
    }
#else
    if (mPathType == PathType::ABSOLUTE_PATH)
    {
        // must start /
        if (mLocalpath.size() < 1)
            return false;
        if (mLocalpath.front() != LocalPath::localPathSeparator)
            return false;
    }
    else
    {
        // this could contain /relative for appending etc.
    }
#endif
    return true;
}

void Path::normalizeAbsolute()
{
    assert(!mLocalpath.empty());

#ifdef USE_IOS
    // iOS is a tricky case.
    // We need to be able to use and persist absolute paths.  however on iOS app restart,
    // our app base path may be different.  So, we only record the path beyond that app
    // base path.   That is what the app supplies for absolute paths, that's what is persisted.
    // Actual filesystem functions passed such an "absolute" path will prepend the app base path
    // unless it already started with /
    // and that's how it worked before we added the "absolute" feature to LocalPath.
    // As a result of that though, there's nothing to adjust or check here for iOS.
    mPathType = PathType::ABSOLUTE_PATH;

    // In addition, for iOS, should the app try to use ".", or "./", we interpret that to mean
    // that really it's relative to the app base path.  So we convert:
    if (!mLocalpath.empty() && mLocalpath.front() == '.')
    {
        if (mLocalpath.size() == 1 || mLocalpath[1] == LocalPath::localPathSeparator)
        {
            mLocalpath.erase(0, 1);
            while (!mLocalpath.empty() && mLocalpath.front() == LocalPath::localPathSeparator)
            {
                mLocalpath.erase(0, 1);
            }
        }
    }

#elif WIN32

    // Add a drive separator if necessary.
    // append \ to bare Windows drive letter paths
    // GetFullPathNameW does all of this for windows.
    // The documentation says to prepend \\?\ to deal with long names, but it makes the function
    // fail it seems to work with long names anyway.

    // We also convert to absolute if it isn't already, which GetFullPathNameW does also.
    // So that when working with LocalPath, we always have the full path.
    // Historically, relative paths can come into the system, this will convert them.

    if (PathIsRelativeW(mLocalpath.c_str()))
    {
        // ms: In the ANSI version of this function, the name is limited to MAX_PATH characters.
        // ms: To extend this limit to 32,767 wide characters, call the Unicode version of the
        // function (GetFullPathNameW), and prepend "\\?\" to the path
        WCHAR buffer[32768];
        DWORD stringLen = GetFullPathNameW(mLocalpath.c_str(), 32768, buffer, NULL);
        assert(stringLen < 32768);

        mLocalpath = wstring(buffer, stringLen);
    }

    mPathType = PathType::ABSOLUTE_PATH;

    // See https://docs.microsoft.com/en-us/dotnet/standard/io/file-path-formats
    // Also https://docs.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation
    // Basically, \\?\ is the magic prefix that means "don't mess with the path I gave you",
    // and lets us access otherwise inaccessible files (trailing ' ', '.', very long names, etc).
    // "Unless the path starts exactly with \\?\ (note the use of the canonical backslash), it is
    // normalized."

    // TODO:  add long-path-aware manifest? (see 2nd link)

    if (mLocalpath.substr(0, 2) == L"\\\\")
    {
        // The caller already passed in a path that should be precise either with \\?\ or \\.\ or
        // \\<server> etc. Let's trust they know what they are doing and leave the path alone

        if (mLocalpath.substr(0, 4) != L"\\\\?\\" && mLocalpath.substr(0, 4) != L"\\\\.\\")
        {
            // However, it turns out, \\?\UNC\<server>\etc  can allow us to operate on paths with
            // trailing spaces and other things Explorer doesn't like, which just \\server\ does not
            {
                mLocalpath.insert(2, L"?\\UNC\\");
            }
        }
    }
    else
    {
        mLocalpath.insert(0, L"\\\\?\\");
    }

#else
    // convert to absolute if it isn't already
    if (!mLocalpath.empty() && mLocalpath[0] != LocalPath::localPathSeparator)
    {
        LocalPath lp;
        PosixFileSystemAccess::cwd_static(lp);
        lp.appendWithSeparator(LocalPathImplementationHelper::buildLocalPath(*this), false);
        mLocalpath = lp.toPath(false);
    }

    mPathType = PathType::ABSOLUTE_PATH;
#endif

    assert(invariant());
}

void Path::truncate(size_t bytePos)
{
    assert(invariant());
    mLocalpath.resize(bytePos);
    assert(invariant());
}

bool Path::beginsWithSeparator() const
{
    return !mLocalpath.empty() && mLocalpath.front() == LocalPath::localPathSeparator;
}

LocalPath Path::subpathTo(size_t bytePos) const
{
    assert(invariant());
    Path p;
    p.mLocalpath = mLocalpath.substr(0, bytePos);
    p.mPathType = mPathType;
    assert(p.invariant());
    return LocalPathImplementationHelper::buildLocalPath(p);
}

bool Path::findNextSeparator(size_t& separatorBytePos) const
{
    assert(invariant());
    separatorBytePos = mLocalpath.find(LocalPath::localPathSeparator, separatorBytePos);
    return separatorBytePos != std::string::npos;
}

std::string Path::serialize() const
{
    std::string d;
    CacheableWriter w(d);
    w.serializeu8(static_cast<uint8_t>(mPathType));
    std::string aux;
    LocalPath::local2path(&mLocalpath, &aux, false);
    w.serializestring(aux);
    return d;
}

bool Path::unserialize(const std::string& data)
{
    CacheableReader r(data);
    uint8_t type;
    r.unserializeu8(type);
    mPathType = static_cast<PathType>(type);
    std::string aux;
    bool unserilizeValue = r.unserializestring(aux);
    LocalPath::path2local(&aux, &mLocalpath);
    return unserilizeValue;
}

string_type Path::getRealPath() const
{
    return asPlatformEncoded(false);
}

auto PathURI::asPlatformEncoded(const bool) const -> string_type
{
    string_type path{mUri};

    for (const auto& leaf: mAuxPath)
    {
        path.push_back(LocalPath::localPathSeparator);
        path.append(leaf);
    }

    return path;
}

string PathURI::platformEncoded() const
{
    return toPath(false);
}

bool PathURI::empty() const
{
    return mUri.empty();
}

void PathURI::clear()
{
    mUri.clear();
    mAuxPath.clear();
}

LocalPath PathURI::leafName() const
{
    if (mAuxPath.size())
    {
        std::string aux;
        LocalPath::local2path(&mAuxPath.back(), &aux, false);
        return LocalPath::fromRelativePath(aux);
    }
    else if (std::optional<string_type> optionalName = URIHandler::getName(mUri);
             optionalName.has_value())
    {
        std::string aux;
        LocalPath::local2path(&optionalName.value(), &aux, false);
        return LocalPath::fromRelativePath(aux);
    }

    return {};
}

std::string PathURI::leafOrParentName() const
{
    if (mAuxPath.size())
    {
        std::string aux;
        LocalPath::local2path(&mAuxPath.back(), &aux, false);
        return aux;
    }
    else
    {
        std::optional<string_type> optionalName = URIHandler::getName(mUri);
        if (optionalName.has_value())
        {
            std::string aux;
            LocalPath::local2path(&optionalName.value(), &aux, false);
            return aux;
        }
    }

    return {};
}

void PathURI::append(const LocalPath& additionalPath)
{
    assert(!additionalPath.isAbsolute() && !additionalPath.isURI());

    mAuxPath.back().append(additionalPath.asPlatformEncoded(false));
}

void PathURI::appendWithSeparator(const LocalPath& additionalPath, const bool)
{
    const auto auxPath = additionalPath.toPath(false);
    auto leaves =
        splitString<std::vector<std::string>>(auxPath, LocalPath::localPathSeparator_utf8);
    for (const auto& leaf: leaves)
    {
        string_type auxLeaf;
        LocalPath::path2local(&leaf, &auxLeaf);
        mAuxPath.emplace_back(auxLeaf);
    }
}

void PathURI::prependWithSeparator(const LocalPath&)
{
    LOG_err << "Invalid operation for URI Path (prependWithSeparator)";
    assert(false);
}

LocalPath PathURI::prependNewWithSeparator(const LocalPath&) const
{
    LOG_err << "Invalid operation for URI Path (prependNewWithSeparator)";
    assert(false);
    return LocalPathImplementationHelper::buildLocalPath(*this);
}

void PathURI::trimNonDriveTrailingSeparator()
{
    LOG_err << "Invalid operation for URI Path (trimNonDriveTrailingSeparator)";
    assert(false);
    return;
}

bool PathURI::findPrevSeparator(size_t& separatorBytePos, const FileSystemAccess&) const
{
    LOG_err << "Invalid operation for URI Path (findPrevSeparator)";
    separatorBytePos = std::string::npos;
    assert(false);
    return false;
}

bool PathURI::beginsWithSeparator() const
{
    LOG_err << "Invalid operation for URI Path (beginsWithSeparator)";
    assert(false);
    return false;
}

bool PathURI::endsInSeparator() const
{
    return mAuxPath.empty();
}

size_t PathURI::getLeafnameByteIndex() const
{
    LOG_err << "Invalid operation for URI Path (getLeafnameByteIndex)";
    assert(false);
    return 0;
}

LocalPath PathURI::subpathFrom(const size_t) const
{
    LOG_err << "Invalid operation for URI Path (subpathFrom)";
    assert(false);
    return LocalPath{};
}

void PathURI::changeLeaf(const LocalPath& newLeaf)
{
    if (newLeaf.isAbsolute() || newLeaf.isURI())
    {
        LOG_err << "Invalid parameter type (appendWithSeparator)";
        assert(false);
        return;
    }

    if (mAuxPath.size())
    {
        mAuxPath.pop_back();
    }
    else if (const auto uri = URIHandler::getParentURI(mUri); !uri.has_value())
    {
        mUri = uri.value();
    }
    else
    {
        LOG_err << "Error change leaf with uri";
        assert(false && "Error change leaf with uri");
    }

    mAuxPath.emplace_back(newLeaf.asPlatformEncoded(false));
}

LocalPath PathURI::parentPath() const
{
    PathURI newPathUri{*this};
    newPathUri.removeLastElement();
    return LocalPathImplementationHelper::buildLocalPath(newPathUri);
}

LocalPath PathURI::insertFilenameSuffix(const std::string& suffix) const
{
    PathURI newPathUri{*this};
    std::string name = newPathUri.leafName().toPath(false);
    name.append(suffix);
    newPathUri.changeLeaf(LocalPath::fromRelativePath(name));
    return LocalPathImplementationHelper::buildLocalPath(newPathUri);
}

std::string PathURI::toPath(const bool) const
{
    std::string aux;
    string_type name = asPlatformEncoded(false);
    LocalPath::local2path(&name, &aux, false);
    return aux;
}

std::string PathURI::toName(const FileSystemAccess&) const
{
    return toPath(false);
}

bool PathURI::isRootPath() const
{
    return mAuxPath.empty();
}

bool PathURI::extension(std::string& extension) const
{
    return extensionOf(leafName().toPath(false), extension);
}

std::string PathURI::extension() const
{
    return extensionOf(toPath(false));
}

bool PathURI::related(const LocalPath&) const
{
    LOG_err << "Invalid operation for URI Path (related)";
    assert(false);
    return false;
}

bool PathURI::invariant() const
{
    return true;
}

void PathURI::removeLastElement()
{
    if (mAuxPath.size())
    {
        mAuxPath.pop_back();
    }
    else if (std::optional<string_type> parentPath = URIHandler::getParentURI(mUri);
             parentPath.has_value())
    {
        mUri = parentPath.value();
    }
}

std::string PathURI::serialize() const
{
    std::string d;
    CacheableWriter w(d);
    uint8_t type = static_cast<uint8_t>(PathType::URI_PATH);
    w.serializeu8(type);
    std::string aux;
    LocalPath::local2path(&mUri, &aux, false);
    w.serializestring(aux);
    uint32_t numElements = static_cast<uint32_t>(mAuxPath.size()); // URI + leaves
    w.serializeu32(numElements);
    for (const auto& leaf: mAuxPath)
    {
        LocalPath::local2path(&leaf, &aux, false);
        w.serializestring(aux);
    }

    return d;
}

bool PathURI::unserialize(const std::string& data)
{
    bool success = true;
    CacheableReader r(data);
    uint8_t type;
    r.unserializeu8(type);
    assert(type == static_cast<uint8_t>(PathType::URI_PATH));
    std::string aux;
    success = r.unserializestring(aux);
    LocalPath::path2local(&aux, &mUri);
    uint32_t numElements;
    success = r.unserializeu32(numElements);
    for (uint32_t i = 0; i < numElements; ++i)
    {
        success = r.unserializestring(aux);
        string_type leaf;
        LocalPath::path2local(&aux, &leaf);
        mAuxPath.emplace_back(leaf);
    }

    return success;
}

bool PathURI::findNextSeparator(size_t& separatorBytePos) const
{
    separatorBytePos = getRealPath().find(LocalPath::localPathSeparator, separatorBytePos);
    return separatorBytePos != std::string::npos;
}

string_type PathURI::getRealPath() const
{
    string_type path{mUri};
    auto pathOptional = URIHandler::getPath(mUri);
    if (pathOptional.has_value())
    {
        LocalPath::path2local(&pathOptional.value(), &path);
    }

    for (const auto& leaf: mAuxPath)
    {
        path.push_back(LocalPath::localPathSeparator);
        path.append(leaf);
    }

    return path;
}
}
