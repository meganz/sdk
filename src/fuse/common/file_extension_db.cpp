#include <mega/fuse/common/file_extension_db.h>

#include <mega/utils.h>

namespace mega
{
namespace fuse
{

FileExtension::FileExtension(FileExtensionDB& db,
                             FromStringMap<std::size_t>::iterator iterator)
  : mDB(&db)
  , mIterator(iterator)
{
}

FileExtension::FileExtension()
  : mDB(nullptr)
  , mIterator()
{
}

FileExtension::FileExtension(const FileExtension& other)
  : mDB(other.mDB)
  , mIterator(other.mIterator)
{
    if (mDB)
        mDB->ref(mIterator);
}

FileExtension::FileExtension(FileExtension&& other)
  : mDB(other.mDB)
  , mIterator(other.mIterator)
{
    other.mDB = nullptr;
}

FileExtension::~FileExtension()
{
    if (mDB)
        mDB->unref(mIterator);
}

FileExtension::operator const std::string&() const
{
    return get();
}

FileExtension& FileExtension::operator=(const FileExtension& rhs)
{
    FileExtension temp(rhs);

    swap(temp);

    return *this;
}

FileExtension& FileExtension::operator=(FileExtension&& rhs)
{
    FileExtension temp(std::move(rhs));

    swap(temp);

    return *this;
}

const std::string& FileExtension::get() const
{
    static const std::string empty;

    if (mDB)
        return mIterator->first;

    return empty;
}

void FileExtension::swap(FileExtension& other)
{
    using std::swap;

    swap(mDB, other.mDB);
    swap(mIterator, other.mIterator);
}

void swap(FileExtension& lhs, FileExtension& rhs)
{
    lhs.swap(rhs);
}

void FileExtensionDB::ref(FromStringMap<std::size_t>::iterator iterator)
{
    std::lock_guard<std::mutex> guard(mLock);

    ++iterator->second;
}

void FileExtensionDB::unref(FromStringMap<std::size_t>::iterator iterator)
{
    std::lock_guard<std::mutex> guard(mLock);

    if (!--iterator->second)
        mExtensions.erase(iterator);
}

FileExtension FileExtensionDB::get(const std::string& extension)
{
    // Empty extension.
    if (extension.empty())
        return FileExtension();

    std::lock_guard<std::mutex> guard(mLock);

    // Does our index already contain this extension?
    auto i = mExtensions.lower_bound(extension);

    // Add extension as necessary.
    if (i == mExtensions.end() || i->first != extension)
        i = mExtensions.emplace_hint(i, extension, 0);

    // Increment extension's reference count.
    ++i->second;

    // Return extension to caller.
    return FileExtension(*this, i);
}

FileExtension FileExtensionDB::getFromPath(const std::string& path)
{
    // Empty path.
    if (path.empty())
        return FileExtension();

    // Add extension to index.
    return get(extensionOf(path));
}

} // fuse
} // mega

