/**
 * @file mega/android/androidFileSystem.h
 * @brief Android filesystem/directory access
 */

#ifndef ANDROIDFILESYSTEM_H
#define ANDROIDFILESYSTEM_H

#include <mega/filesystem.h>
#include <mega/posix/megafs.h>
#include <mega/types.h>

#include <jni.h>
#include <mutex>

namespace mega
{
/*
 * @brief Encapsulates a Java object to provide file and directory functionalities on Android
 *
 * This class minimizes JNI calls by maintaining its instances in an LRU cache.
 * Rather than creating a new JNI object for every operation, previously created
 * instances are reused, reducing the number of JNI calls required.
 *
 * To get an instance of this class, getAndroidFileWrapper should be called
 * Pay attention, constructor is private
 */
class AndroidFileWrapper
{
public:
    ~AndroidFileWrapper();
    AndroidFileWrapper() = delete;
    AndroidFileWrapper(const AndroidFileWrapper&) = delete;
    AndroidFileWrapper& operator=(const AndroidFileWrapper&) = delete;
    AndroidFileWrapper(AndroidFileWrapper&& other) = delete;
    AndroidFileWrapper& operator=(AndroidFileWrapper&& other) = delete;
    bool exists();
    int getFileDescriptor(bool write);
    void close();
    std::string getName();
    std::vector<std::shared_ptr<AndroidFileWrapper>> getChildren();
    bool isFolder();
    std::string getPath();
    bool isURI();

    static std::shared_ptr<AndroidFileWrapper> getAndroidFileWrapper(const std::string& path);

private:
    AndroidFileWrapper(const std::string& path);
    jobject mAndroidFileObject{nullptr};
    std::string mPath;
    std::optional<std::string> mName;
    std::optional<bool> mIsFolder;
    std::optional<bool> mIsURI;
    static constexpr char GET_ANDROID_FILE[] = "getFromUri";
    static constexpr char GET_FILE_DESCRIPTOR[] = "getFileDescriptor";
    static constexpr char IS_FOLDER[] = "isFolder";
    static constexpr char GET_NAME[] = "getName";
    static constexpr char GET_CHILDREN_URIS[] = "getChildrenUris";

    static LRUCache<std::string, std::shared_ptr<AndroidFileWrapper>> mRepository;
    static std::mutex mMutex;
};

/**
 * @brief Android implementation to handle URIs
 */
class MEGA_API AndroidPlatformURIHelper: public PlatformURIHelper
{
public:
    bool isURI(const std::string& path) override;
    std::string getName(const std::string& path) override;

private:
    /**
     * @brief Private default constructor to enforce controlled instantiation.
     *
     * The static instance mPlatformHelper is used instead of creating multiple objects.
     */
    AndroidPlatformURIHelper();

    ~AndroidPlatformURIHelper() override {}

    static AndroidPlatformURIHelper mPlatformHelper;
};

/**
 * @brief Implement FileAcces functionality for Android
 *
 * For access to file from Android, required data (file descriptor,
 * name, is folder) are obtained with a JNI call to Android layer
 * Other required data, as size or creation time, are obtained
 * with the file descriptor
 */
class MEGA_API AndroidFileAccess: public FileAccess
{
public:
    bool fopen(const LocalPath&,
               bool read,
               bool write,
               FSLogging,
               DirAccess* iteratingDir = nullptr,
               bool ignoreAttributes = false,
               bool skipcasecheck = false,
               LocalPath* actualLeafNameIfDifferent = nullptr) override;

    void fclose() override;

    bool fwrite(const byte*, unsigned, m_off_t) override;

    bool fstat(m_time_t& modified, m_off_t& size) override;

    bool ftruncate(m_off_t size = 0) override;

    void updatelocalname(const LocalPath& name, bool force) override;

    AndroidFileAccess(Waiter* w, int defaultfilepermissions = 0600, bool followSymLinks = true);
    virtual ~AndroidFileAccess();

    std::shared_ptr<AndroidFileWrapper> stealFileWrapper();

protected:
    bool sysread(byte*, unsigned, m_off_t) override;
    bool sysstat(m_time_t*, m_off_t*, FSLogging) override;
    bool sysopen(bool async, FSLogging) override;
    void sysclose() override;

    std::shared_ptr<AndroidFileWrapper> mFileWrapper;
    int fd{-1};
    int mDefaultFilePermissions{0600};
};

/**
 * @brief Implement DirAccess functionality for Android
 *
 * For access to directory from Android, required data
 * (list of children) are obtained with a JNI call to Android layer
 */
class MEGA_API AndroidDirAccess: public DirAccess
{
public:
    bool dopen(LocalPath* path, FileAccess* f, bool doglob) override;
    bool dnext(LocalPath& path, LocalPath& name, bool followsymlinks, nodetype_t* type) override;

private:
    std::shared_ptr<AndroidFileWrapper> mFileWrapper;
    std::vector<std::shared_ptr<AndroidFileWrapper>> mChildren;
    size_t mIndex{0};

    std::unique_ptr<PosixDirAccess> mGlobbing;
};
}

#endif // ANDROIDFILESYSTEM_H
