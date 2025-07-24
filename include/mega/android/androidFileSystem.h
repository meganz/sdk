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

extern jclass fileWrapper;
extern jclass integerClass;
extern jclass arrayListClass;
extern JavaVM* MEGAjvm;

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

    bool exists() const;
    int getFileDescriptor(bool write);
    void close();
    std::string getName();
    std::vector<std::shared_ptr<AndroidFileWrapper>> getChildren();
    // Check if tree exists
    std::shared_ptr<AndroidFileWrapper> pathExists(const std::vector<std::string>& subPaths);

    std::shared_ptr<AndroidFileWrapper>
        createOrReturnNestedPath(const std::vector<std::string>& subPaths,
                                 bool create,
                                 bool isFolder);

    // Create child (only first level)
    std::shared_ptr<AndroidFileWrapper> createChild(const std::string& childName, bool isFolder);
    // Returns child by name (only first level)
    std::shared_ptr<AndroidFileWrapper> getChildByName(const std::string& name);
    // Remove the file associated
    // this FileWrapper shouldn't be used after call this method
    bool deleteFile();
    // Remove the associated folder if it's empty
    // If it isn't a folder or it isn't empty, it will return false
    // this FileWrapper shouldn't be used after call this method
    bool deleteEmptyFolder();
    // Rename an element. It is kept at same folder
    bool rename(const std::string& newName);

    // Returns true if it's a folder
    bool isFolder();
    // Returns the URI
    std::string getURI() const;
    // Returns FileWrapper parent
    std::shared_ptr<AndroidFileWrapper> getParent() const;
    // Returns the path if it's possible (/local/..)
    std::optional<std::string> getPath();
    bool isURI();

    static std::shared_ptr<AndroidFileWrapper> getAndroidFileWrapper(const std::string& path);
    static std::shared_ptr<AndroidFileWrapper> getAndroidFileWrapper(const LocalPath& localPath,
                                                                     bool create,
                                                                     bool lastIsFolder);

private:
    class JavaObject
    {
    public:
        // Note: it should be global reference
        JavaObject(jobject obj):
            mObj(obj)
        {}

        ~JavaObject()
        {
            JNIEnv* env{nullptr};
            MEGAjvm->AttachCurrentThread(&env, NULL);
            env->DeleteGlobalRef(mObj);
        }

        jobject mObj;
    };

    class URIData
    {
    public:
        std::optional<bool> mIsURI;
        std::optional<bool> mIsFolder;
        std::optional<std::string> mName;
        std::optional<std::string> mPath;
        std::shared_ptr<JavaObject> mJavaObject;
    };

    AndroidFileWrapper(const std::string& path);
    AndroidFileWrapper(std::shared_ptr<JavaObject>);
    std::shared_ptr<JavaObject> mJavaObject;

    jobject vectorToJavaList(JNIEnv* env, const std::vector<std::string>& vec);

    std::string mURI;
    static constexpr char GET_ANDROID_FILE[] = "getFromUri";
    static constexpr char GET_FILE_DESCRIPTOR[] = "getFileDescriptor";
    static constexpr char IS_PATH[] = "isPath";
    static constexpr char IS_FOLDER[] = "isFolder";
    static constexpr char GET_NAME[] = "getName";
    static constexpr char GET_CHILDREN_URIS[] = "getChildrenUris";
    static constexpr char CHILD_EXISTS[] = "childFileExists";
    static constexpr char CREATE_CHILD[] = "createChildFile";
    static constexpr char GET_CHILD_BY_NAME[] = "getChildByName";
    static constexpr char GET_PARENT[] = "getParentFile";
    static constexpr char GET_PATH[] = "getPath";
    static constexpr char DELETE_FILE[] = "deleteFile";
    static constexpr char DELETE_EMPTY_FOLDER[] = "deleteFolderIfEmpty";
    static constexpr char RENAME[] = "rename";
    static constexpr char CREATE_NESTED_PATH[] = "createNestedPath";

    void setUriData(const URIData& uriData);
    std::optional<URIData> getURIData(const std::string& uri) const;
    static LRUCache<std::string, URIData> URIDataCache;
    static std::mutex URIDataCacheLock;
};

/**
 * @brief Android implementation to handle URIs
 */
class MEGA_API AndroidPlatformURIHelper: public PlatformURIHelper
{
public:
    bool isURI(const std::string& path) override;
    std::optional<std::string> getName(const std::string& path) override;
    // Returns parent URI if it's available
    std::optional<string_type> getParentURI(const string_type& uri) override;
    std::optional<string_type> getPath(const string_type& uri) override;
    std::optional<string_type> getURI(const string_type& uri,
                                      const std::vector<string_type> leaves) override;

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
    void fCloseInternal();
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

class MEGA_API AndroidFileSystemAccess: public LinuxFileSystemAccess
{
public:
    using FileSystemAccess::getlocalfstype;
    std::unique_ptr<FileAccess> newfileaccess(bool followSymLinks = true) override;
    std::unique_ptr<DirAccess> newdiraccess() override;

#ifdef ENABLE_SYNC
    DirNotify* newdirnotify(LocalNode& root, const LocalPath& rootPath, Waiter* waiter) override;
#endif

    bool getlocalfstype(const LocalPath& path, FileSystemType& type) const override;
    bool getsname(const LocalPath&, LocalPath&) const override;
    bool renamelocal(const LocalPath&, const LocalPath&, bool = true) override;
    bool copylocal(const LocalPath&, const LocalPath&, m_time_t) override;
    bool unlinklocal(const LocalPath&) override;
    bool rmdirlocal(const LocalPath&) override;
    bool mkdirlocal(const LocalPath&, bool hidden, bool logAlreadyExistsError) override;
    /* On Android we cannot set mtime on files, due to insufficient permissions */
    bool setmtimelocal(const LocalPath&, m_time_t) override;
    bool chdirlocal(LocalPath&) const override;
    bool issyncsupported(const LocalPath&, bool&, SyncError&, SyncWarning&) override;
    bool expanselocalpath(const LocalPath& path, LocalPath& absolutepath) override;
    int getdefaultfilepermissions() override;
    void setdefaultfilepermissions(int) override;

    int getdefaultfolderpermissions() override;
    void setdefaultfolderpermissions(int) override;

    // append local operating system version information to string.
    // Set includeArchExtraInfo to know if the app is 32 bit running on 64 bit (on windows, that is
    // via the WOW subsystem)
    void osversion(string*, bool /*includeArchExtraInfo*/) const override;

    void statsid(string*) const override;

    AndroidFileSystemAccess() {}

    MEGA_DISABLE_COPY_MOVE(AndroidFileSystemAccess);

    ~AndroidFileSystemAccess() override {}

    bool cwd(LocalPath& path) const override;

#ifdef ENABLE_SYNC
    // True if the filesystem indicated by the specified path has stable FSIDs.
    bool fsStableIDs(const LocalPath& path) const override;

    bool initFilesystemNotificationSystem() override;
#endif // ENABLE_SYNC

    ScanResult directoryScan(const LocalPath& path,
                             handle expectedFsid,
                             map<LocalPath, FSNode>& known,
                             std::vector<FSNode>& results,
                             bool followSymLinks,
                             unsigned& nFingerprinted) override;

    /* Not implemented yet */
    bool hardLink(const LocalPath& source, const LocalPath& target) override;

    m_off_t availableDiskSpace(const LocalPath& drivePath) override;

    void addevents(Waiter*, int) override;

    fsfp_t fsFingerprint(const LocalPath& path) const override;

    static void emptydirlocal(const LocalPath&, dev_t = 0);

    static bool isFileWrapperActive(const FileSystemAccess* fsa);

    bool isFileWrapperActive() const
    {
        return fileWrapper != nullptr;
    }

private:
    LocalPath getStandartPath(const LocalPath& localPath) const;
    bool copy(const LocalPath& oldname, const LocalPath& newName);
};

class AndroidDirNotify: public LinuxDirNotify
{
public:
    AndroidDirNotify(AndroidFileSystemAccess& owner, LocalNode& root, const LocalPath& rootPath);

    ~AndroidDirNotify() override {}

    AddWatchResult addWatch(LocalNode& node, const LocalPath& path, handle fsid) override;
};
}

#endif // ANDROIDFILESYSTEM_H
