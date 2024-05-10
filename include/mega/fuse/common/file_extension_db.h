#pragma once

#include <mutex>

#include <mega/fuse/common/file_extension_db_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

class FileExtensionDB;

class FileExtension
{
    friend class FileExtensionDB;

    FileExtension(FileExtensionDB& db,
                  FromStringMap<std::size_t>::iterator iterator);

    // The DB that contains this extension.
    FileExtensionDB* mDB;
    
    // Our position in the DB's extensions map.
    FromStringMap<std::size_t>::iterator mIterator;

public:
    FileExtension();

    FileExtension(const FileExtension& other);

    FileExtension(FileExtension&& other);

    ~FileExtension();

    operator const std::string&() const;

    FileExtension& operator=(const FileExtension& rhs);

    FileExtension& operator=(FileExtension&& rhs);

    const std::string& get() const;

    void swap(FileExtension& other);
}; // FileExtension

class FileExtensionDB
{
    friend class FileExtension;

    // Add a reference to an existing extension.
    void ref(FromStringMap<std::size_t>::iterator iterator);

    // Remove a reference from an existing extension.
    void unref(FromStringMap<std::size_t>::iterator iterator);

    // Serializes access to mExtensions.
    std::mutex mLock;

    // Tracks the extensions we know about along with their reference count.
    FromStringMap<std::size_t> mExtensions;

public:
    FileExtensionDB() = default;
    
    FileExtensionDB(const FileExtensionDB& other) = delete;

    ~FileExtensionDB() = default;

    FileExtensionDB& operator=(const FileExtensionDB& rhs) = delete;

    // Add a new (or reference an existing) extension to the DB.
    FileExtension get(const std::string& extension);

    // The same as the above but determines the extension from the path.
    FileExtension getFromPath(const std::string& path);
}; // FileExtensionDB

void swap(FileExtension& lhs, FileExtension& rhs);

} // fuse
} // mega

