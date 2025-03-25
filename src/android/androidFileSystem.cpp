#include <mega/android/androidFileSystem.h>
#include <mega/filesystem.h>
#include <mega/logging.h>

jclass fileWrapper = nullptr;
jclass integerClass = nullptr;
JavaVM* MEGAjvm = nullptr;

namespace mega
{

AndroidPlatformURIHelper AndroidPlatformURIHelper::mPlatformHelper;
LRUCache<std::string, std::shared_ptr<AndroidFileWrapper>> AndroidFileWrapper::mRepository(100);

std::mutex AndroidFileWrapper::mMutex;

AndroidFileWrapper::AndroidFileWrapper(const std::string& path):
    mURI(path)
{
    if (fileWrapper == nullptr)
    {
        LOG_err << "Error: AndroidFileWrapper::AndroidFileWrapper class not found";
        return;
    }

    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID getAndroidFileMethod = env->GetStaticMethodID(
        fileWrapper,
        GET_ANDROID_FILE,
        "(Ljava/lang/String;)Lmega/privacy/android/data/filewrapper/FileWrapper;");

    if (getAndroidFileMethod == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::AndroidFileWrapper";
        return;
    }

    jstring jPath = env->NewStringUTF(mURI.c_str());
    jobject temporalObject = env->CallStaticObjectMethod(fileWrapper, getAndroidFileMethod, jPath);
    env->DeleteLocalRef(jPath);

    if (temporalObject != nullptr)
    {
        mAndroidFileObject = env->NewGlobalRef(temporalObject);
        env->DeleteLocalRef(temporalObject);
    }
}

AndroidFileWrapper::AndroidFileWrapper(jobject fileWrapper):
    mAndroidFileObject(fileWrapper)
{}

AndroidFileWrapper::~AndroidFileWrapper()
{
    if (mAndroidFileObject)
    {
        JNIEnv* env{nullptr};
        MEGAjvm->AttachCurrentThread(&env, NULL);
        env->DeleteGlobalRef(mAndroidFileObject);
    }
}

int AndroidFileWrapper::getFileDescriptor(bool write)
{
    if (!exists())
    {
        return -1;
    }

    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);

    jmethodID methodID =
        env->GetMethodID(fileWrapper, "getFileDescriptor", "(Z)Ljava/lang/Integer;");
    if (methodID == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::getFileDescriptor";
        return -1;
    }

    jobject fileDescriptorObj = env->CallObjectMethod(mAndroidFileObject, methodID, write);
    if (fileDescriptorObj && integerClass)
    {
        jmethodID intValueMethod = env->GetMethodID(integerClass, "intValue", "()I");
        if (!intValueMethod)
        {
            return -1;
        }

        return env->CallIntMethod(fileDescriptorObj, intValueMethod);
    }

    return -1;
}

bool AndroidFileWrapper::isFolder()
{
    if (!exists())
    {
        return false;
    }

    if (mIsFolder.has_value())
    {
        return mIsFolder.value();
    }

    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID = env->GetMethodID(fileWrapper, IS_FOLDER, "()Z");
    if (methodID == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::isFolder";
        return false;
    }

    mIsFolder = env->CallBooleanMethod(mAndroidFileObject, methodID);
    return mIsFolder.value();
}

std::string AndroidFileWrapper::getURI() const
{
    return mURI;
}

bool AndroidFileWrapper::isURI()
{
    if (mIsURI.has_value())
    {
        return mIsURI.value();
    }

    constexpr char IS_PATH[] = "isPath";
    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID = env->GetStaticMethodID(fileWrapper, IS_PATH, "(Ljava/lang/String;)Z");
    if (methodID == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();

        LOG_err << "Critical error AndroidPlatformHelper::isURI";
        return false;
    }

    mIsURI = !env->CallStaticBooleanMethod(fileWrapper, methodID, env->NewStringUTF(mURI.c_str()));
    return mIsURI.value();
}

std::string AndroidFileWrapper::getName()
{
    if (!exists())
    {
        return std::string();
    }

    if (mName.has_value())
    {
        return mName.value();
    }

    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID = env->GetMethodID(fileWrapper, GET_NAME, "()Ljava/lang/String;");
    if (methodID == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::getName";
        return "";
    }

    jstring name = static_cast<jstring>(env->CallObjectMethod(mAndroidFileObject, methodID));

    const char* nameStr = env->GetStringUTFChars(name, nullptr);
    mName = nameStr;
    env->ReleaseStringUTFChars(name, nameStr);
    return mName.value();
}

std::vector<std::shared_ptr<AndroidFileWrapper>> AndroidFileWrapper::getChildren()
{
    if (!exists())
    {
        return {};
    }

    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID = env->GetMethodID(fileWrapper, GET_CHILDREN_URIS, "()Ljava/util/List;");
    if (methodID == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::getchildren";
        return {};
    }

    jobject childrenUris = env->CallObjectMethod(mAndroidFileObject, methodID);
    jclass listClass = env->FindClass("java/util/List");
    jmethodID sizeMethod = env->GetMethodID(listClass, "size", "()I");
    jmethodID getMethod = env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");
    jint size = env->CallIntMethod(childrenUris, sizeMethod);

    std::vector<std::shared_ptr<AndroidFileWrapper>> children;
    children.reserve(size);
    for (jint i = 0; i < size; ++i)
    {
        jstring element = (jstring)env->CallObjectMethod(childrenUris, getMethod, i);
        const char* elementStr = env->GetStringUTFChars(element, nullptr);
        children.push_back(AndroidFileWrapper::getAndroidFileWrapper(elementStr));
        env->ReleaseStringUTFChars(element, elementStr);
        env->DeleteLocalRef(element);
    }

    return children;
}

bool AndroidFileWrapper::pathExists(const std::vector<std::string>& subPaths)
{
    std::shared_ptr<AndroidFileWrapper> child;
    for (const auto& childName: subPaths)
    {
        // First iteration child undef (check own children), rest iteration use matched child
        child = !child ? getChildByName(childName) : child->getChildByName(childName);
        if (!child)
        {
            return false;
        }
    }

    return true;
}

std::shared_ptr<AndroidFileWrapper>
    AndroidFileWrapper::returnOrCreateByPath(const std::vector<std::string>& subPaths,
                                             bool lastIsFolder)
{
    std::shared_ptr<AndroidFileWrapper> previousParent;
    for (const auto& childName: subPaths)
    {
        // First iteration child undef (check own children), rest iteration use child
        auto child =
            !previousParent ? getChildByName(childName) : previousParent->getChildByName(childName);
        if (!child)
        {
            // Intermediate leaves are always folders (childName != subPaths.back())
            bool isFolder = childName != subPaths.back() || lastIsFolder;
            child = !previousParent ? createChild(childName, isFolder) :
                                      previousParent->createChild(childName, isFolder);
        }
        previousParent = child;
    }

    return previousParent;
}

std::shared_ptr<AndroidFileWrapper> AndroidFileWrapper::createChild(const std::string& childName,
                                                                    bool isFolder)
{
    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID = env->GetMethodID(
        fileWrapper,
        CREATE_CHILD,
        "(Ljava/lang/String;Z)Lmega/privacy/android/data/filewrapper/FileWrapper;");

    if (methodID == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::createChild";
        return nullptr;
    }

    jstring jname = env->NewStringUTF(childName.c_str());
    jobject temporalObject = env->CallObjectMethod(mAndroidFileObject, methodID, jname, isFolder);
    env->DeleteLocalRef(jname);
    jobject globalObject{nullptr};
    if (temporalObject != nullptr)
    {
        globalObject = env->NewGlobalRef(temporalObject);
        env->DeleteLocalRef(temporalObject);
    }

    if (!globalObject)
    {
        return nullptr;
    }

    return std::make_shared<AndroidFileWrapper>(globalObject);
}

std::shared_ptr<AndroidFileWrapper> AndroidFileWrapper::getChildByName(const std::string& name)
{
    auto children = getChildren();
    for (auto& child: children)
    {
        if (child->getName() == name)
        {
            return child;
        }
    }

    return {};
}

std::shared_ptr<AndroidFileWrapper> AndroidFileWrapper::getParent() const
{
    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID = env->GetMethodID(fileWrapper,
                                          GET_PARENT,
                                          "()Lmega/privacy/android/data/filewrapper/FileWrapper;");

    if (methodID == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::getParent";
        return nullptr;
    }

    jobject temporalObject = env->CallObjectMethod(mAndroidFileObject, methodID);
    jobject globalObject{nullptr};
    if (temporalObject != nullptr)
    {
        globalObject = env->NewGlobalRef(temporalObject);
        env->DeleteLocalRef(temporalObject);
    }

    if (!globalObject)
    {
        return nullptr;
    }

    return std::make_shared<AndroidFileWrapper>(globalObject);
}

std::optional<std::string> AndroidFileWrapper::getPath() const
{
    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID = env->GetMethodID(fileWrapper, GET_PATH, "()Ljava/lang/String;");
    if (!methodID)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::getPath";
        return std::nullopt;
    }

    jstring pathString = static_cast<jstring>(env->CallObjectMethod(mAndroidFileObject, methodID));
    if (!pathString)
    {
        return std::nullopt;
    }

    const char* chars = env->GetStringUTFChars(pathString, nullptr);
    std::string outputString(chars);
    env->ReleaseStringUTFChars(pathString, chars);
    env->DeleteLocalRef(pathString);
    return outputString;
}

bool AndroidFileWrapper::deleteFile()
{
    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID = env->GetMethodID(fileWrapper, DELETE_FILE, "()Z");
    if (!methodID)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::deleteFile";
        return false;
    }

    return env->CallBooleanMethod(mAndroidFileObject, methodID);
}

bool AndroidFileWrapper::deleteEmptyFolder()
{
    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID = env->GetMethodID(fileWrapper, DELETE_EMPTY_FOLDER, "()Z");
    if (!methodID)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::deleteEmptyFolder";
        return false;
    }

    return env->CallBooleanMethod(mAndroidFileObject, methodID);
}

std::shared_ptr<AndroidFileWrapper> AndroidFileWrapper::rename(const std::string& newName)
{
    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID =
        env->GetMethodID(fileWrapper,
                         RENAME,
                         "(Ljava/lang/String;)Lmega/privacy/android/data/filewrapper/FileWrapper;");
    if (!methodID)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::rename";
        return nullptr;
    }

    jstring jnewName = env->NewStringUTF(newName.c_str());
    jobject temporalObject = env->CallObjectMethod(mAndroidFileObject, methodID, jnewName);
    env->DeleteLocalRef(jnewName);
    jobject globalObject{nullptr};
    if (temporalObject != nullptr)
    {
        globalObject = env->NewGlobalRef(temporalObject);
        env->DeleteLocalRef(temporalObject);
    }

    if (!globalObject)
    {
        return nullptr;
    }

    return std::make_shared<AndroidFileWrapper>(globalObject);
}

std::shared_ptr<AndroidFileWrapper>
    AndroidFileWrapper::getAndroidFileWrapper(const LocalPath& localPath,
                                              bool create,
                                              bool lastIsFolder)
{
    if (localPath.isURI())
    {
        std::vector<std::string> children;
        LocalPath auxPath{localPath};
        while (!auxPath.isRootPath())
        {
            children.insert(children.begin(), auxPath.leafOrParentName());
            auxPath = auxPath.parentPath();
        }

        std::shared_ptr<AndroidFileWrapper> uriFileWrapper =
            AndroidFileWrapper::getAndroidFileWrapper(auxPath.toPath(false));

        if (!uriFileWrapper->exists())
        {
            return nullptr;
        }

        if (children.size())
        {
            if (!uriFileWrapper->pathExists(children) && !create)
            {
                return nullptr;
            }

            uriFileWrapper = uriFileWrapper->returnOrCreateByPath(children, lastIsFolder);
            return uriFileWrapper;
        }
        else
        {
            return uriFileWrapper;
        }
    }
    else
    {
        return AndroidFileWrapper::getAndroidFileWrapper(localPath.toPath(false));
    }

    return nullptr;
}

bool AndroidFileWrapper::exists()
{
    return mAndroidFileObject != nullptr;
}

std::shared_ptr<AndroidFileWrapper>
    AndroidFileWrapper::getAndroidFileWrapper(const std::string& uri)
{
    std::lock_guard<std::mutex> g(mMutex);
    auto androidFileWrapper = mRepository.get(uri);
    if (androidFileWrapper.has_value())
    {
        return androidFileWrapper.value();
    }

    std::shared_ptr<AndroidFileWrapper> androidFileWrapperNew{new AndroidFileWrapper(uri)};
    mRepository.put(uri, androidFileWrapperNew);
    return androidFileWrapperNew;
}

AndroidPlatformURIHelper::AndroidPlatformURIHelper()
{
    URIHandler::setPlatformHelper(this);
}

bool AndroidPlatformURIHelper::isURI(const std::string& uri)
{
    std::shared_ptr<AndroidFileWrapper> fileWrapper =
        AndroidFileWrapper::getAndroidFileWrapper(uri);
    if (fileWrapper->exists())
    {
        return fileWrapper->isURI();
    }

    return false;
}

std::optional<std::string> AndroidPlatformURIHelper::getName(const std::string& uri)
{
    std::shared_ptr<AndroidFileWrapper> fileWrapper =
        AndroidFileWrapper::getAndroidFileWrapper(uri);
    if (fileWrapper->exists())
    {
        return fileWrapper->getName();
    }

    return std::nullopt;
}

std::optional<std::string> AndroidPlatformURIHelper::getParentURI(const std::string& uri)
{
    std::shared_ptr<AndroidFileWrapper> fileWrapper =
        AndroidFileWrapper::getAndroidFileWrapper(uri);

    if (fileWrapper->exists())
    {
        std::shared_ptr<AndroidFileWrapper> parentWrapper = fileWrapper->getParent();
        return parentWrapper ? std::optional<std::string>{parentWrapper->getURI()} : std::nullopt;
    }

    return std::nullopt;
}

std::optional<std::string> AndroidPlatformURIHelper::getPath(const std::string& uri)
{
    std::shared_ptr<AndroidFileWrapper> fileWrapper =
        AndroidFileWrapper::getAndroidFileWrapper(uri);

    if (fileWrapper->exists())
    {
        return fileWrapper->getPath();
    }

    return std::nullopt;
}

bool AndroidFileAccess::fopen(const LocalPath& f,
                              bool,
                              bool write,
                              FSLogging,
                              DirAccess*,
                              bool,
                              bool,
                              LocalPath*)
{
    fopenSucceeded = false;
    retry = false;
    assert(!mFileWrapper);

    mFileWrapper = AndroidFileWrapper::getAndroidFileWrapper(f, write, false);
    if (!mFileWrapper)
    {
        return false;
    }

    if (!mFileWrapper->exists())
    {
        return false;
    }

    bool statCalculated = false;
    std::optional<std::string> path = mFileWrapper->getPath();
    struct stat statbuf;
    if (path.has_value())
    {
        statCalculated = true;
        if (stat(path->c_str(), &statbuf) != -1)
        {
            if (S_ISDIR(statbuf.st_mode))
            {
                type = FOLDERNODE;
                size = 0;
                mtime = statbuf.st_mtime;
                fsid = static_cast<handle>(statbuf.st_ino);
                fsidvalid = true;
                fopenSucceeded = true;
                return true;
            }
        }
    }

    assert(fd < 0 && "There should be no opened file descriptor at this point");
    sysclose();

    fd = mFileWrapper->getFileDescriptor(write);
    if (fd < 0)
    {
        LOG_err << "Error getting file descriptor";
        errorcode = fd == -2 ? EACCES : ENOENT;
        return false;
    }

    if (!statCalculated)
    {
        if (::fstat(fd, &statbuf) == -1)
        {
            errorcode = errno;
            LOG_err << "Failled to call fstat: " << errorcode << "  " << strerror(errorcode);
            close(fd);
            fd = -1;
            return false;
        }
    }

    if (S_ISLNK(statbuf.st_mode))
    {
        LOG_err << "Sym links aren't supported in Android";
        return -1;
    }

    type = S_ISDIR(statbuf.st_mode) ? FOLDERNODE : FILENODE;
    size = (type == FILENODE || mIsSymLink) ? statbuf.st_size : 0;
    mtime = statbuf.st_mtime;
    fsid = static_cast<handle>(statbuf.st_ino);
    fsidvalid = true;

    FileSystemAccess::captimestamp(&mtime);

    fopenSucceeded = true;
    return true;
}

void AndroidFileAccess::fclose()
{
    if (fd >= 0)
    {
        close(fd);
    }

    fd = -1;
}

bool AndroidFileAccess::fwrite(const byte* data, unsigned len, m_off_t pos)
{
    retry = false;
    lseek64(fd, pos, SEEK_SET);
    return write(fd, data, len) == len;
}

bool AndroidFileAccess::fstat(m_time_t& modified, m_off_t& size)
{
    struct stat attributes;

    retry = false;
    if (::fstat(fd, &attributes))
    {
        errorcode = errno;

        LOG_err << "Unable to stat descriptor: " << fd << ". Error was: " << errorcode;

        return false;
    }

    modified = attributes.st_mtime;
    size = static_cast<m_off_t>(attributes.st_size);

    return true;
}

bool AndroidFileAccess::ftruncate(m_off_t size)
{
    retry = false;

    // Truncate the file.
    if (::ftruncate(fd, size) == 0)
    {
        // Set the file pointer to the end.
        return lseek(fd, size, SEEK_SET) == size;
    }

    // Couldn't truncate the file.
    return false;
}

void AndroidFileAccess::updatelocalname(const LocalPath& name, bool force)
{
    if (force || !nonblocking_localname.empty())
    {
        nonblocking_localname = name;
        mFileWrapper = nullptr;
    }
}

AndroidFileAccess::AndroidFileAccess(Waiter* w, int defaultfilepermissions, bool):
    FileAccess(w),
    mDefaultFilePermissions(defaultfilepermissions)
{}

AndroidFileAccess::~AndroidFileAccess() {}

std::shared_ptr<AndroidFileWrapper> AndroidFileAccess::stealFileWrapper()
{
    sysclose();
    return std::exchange(mFileWrapper, nullptr);
}

bool AndroidFileAccess::sysread(byte* dst, unsigned len, m_off_t pos)
{
    retry = false;
    lseek64(fd, pos, SEEK_SET);
    return read(fd, (char*)dst, len) == len;
}

bool AndroidFileAccess::sysstat(m_time_t* mtime, m_off_t* size, FSLogging)
{
    if (!mFileWrapper)
    {
        mFileWrapper =
            AndroidFileWrapper::getAndroidFileWrapper(nonblocking_localname, false, false);
    }
    else
    {
        assert(nonblocking_localname.asPlatformEncoded(false) == mFileWrapper->getName());
    }

    if (!mFileWrapper)
    {
        return false;
    }

    if (!mFileWrapper->exists())
    {
        return false;
    }

    // Try to calculate first with path, in case of failure,
    // get statbuf with the file descriptor

    std::optional<std::string> path = mFileWrapper->getPath();
    struct stat statbuf;
    if (path.has_value())
    {
        if (stat(path->c_str(), &statbuf) != -1)
        {
            if (S_ISLNK(statbuf.st_mode))
            {
                LOG_err << "Sym links aren't supported in Android";
                return false;
            }

            *size = 0;
            type = S_ISDIR(statbuf.st_mode) ? FOLDERNODE : FILENODE;
            if (type == FILENODE)
            {
                *size = statbuf.st_size;
                *mtime = statbuf.st_mtime;
                FileSystemAccess::captimestamp(mtime);
            }

            return true;
        }
    }

    bool opened = false;
    if (fd < 0)
    {
        fd = mFileWrapper->getFileDescriptor(false);
        if (fd < 0)
        {
            errorcode = fd == -2 ? EACCES : ENOENT;
            LOG_err << "Error getting file descriptor";
            return false;
        }

        opened = true;
    }

    if (::fstat(fd, &statbuf) == -1)
    {
        errorcode = errno;
        LOG_err << "Failled to call fstat: " << errorcode << "  " << strerror(errorcode);
        if (opened)
        {
            close(fd);
        }
        return false;
    }

    if (S_ISLNK(statbuf.st_mode))
    {
        LOG_err << "Sym links aren't supported in Android";
        return false;
    }

    retry = false;

    type = TYPE_UNKNOWN;

    errorcode = 0;
    if (S_ISDIR(statbuf.st_mode))
    {
        type = FOLDERNODE;
        if (opened)
        {
            close(fd);
            fd = -1;
        }
        return false;
    }

    type = FILENODE;
    *size = statbuf.st_size;
    *mtime = statbuf.st_mtime;

    FileSystemAccess::captimestamp(mtime);

    if (opened)
    {
        close(fd);
        fd = -1;
    }

    return true;
}

bool AndroidFileAccess::sysopen(bool, FSLogging)
{
    assert(fd < 0 && "There should be no opened file descriptor at this point");
    errorcode = 0;
    if (fd >= 0)
    {
        sysclose();
    }

    mFileWrapper = AndroidFileWrapper::getAndroidFileWrapper(nonblocking_localname, false, false);

    if (!mFileWrapper->exists())
    {
        errorcode = ENOENT;
        return false;
    }

    fd = mFileWrapper->getFileDescriptor(false);
    if (fd < 0)
    {
        LOG_err << "Error getting file descriptor";
        errorcode = EACCES;
    }

    return fd >= 0;
}

void AndroidFileAccess::sysclose()
{
    assert(nonblocking_localname.empty() || fd >= 0);
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

bool AndroidDirAccess::dopen(LocalPath* path, FileAccess* f, bool doglob)
{
    if (doglob)
    {
        if (path->isURI())
        {
            return false;
        }

        mGlobbing = std::make_unique<PosixDirAccess>();
        return mGlobbing->dopen(path, f, doglob);
    }

    mGlobbing.reset();
    mIndex = 0;
    if (f)
    {
        mFileWrapper = static_cast<AndroidFileAccess*>(f)->stealFileWrapper();
    }
    else
    {
        assert(path);
        std::string fstr = path->asPlatformEncoded(false);
        assert(!mFileWrapper);

        mFileWrapper = AndroidFileWrapper::getAndroidFileWrapper(fstr);
    }

    if (!mFileWrapper->exists())
    {
        return false;
    }

    mChildren = mFileWrapper->getChildren();
    return true;
}

bool AndroidDirAccess::dnext(LocalPath& path,
                             LocalPath& name,
                             bool followsymlinks,
                             nodetype_t* type)
{
    if (mGlobbing)
    {
        return mGlobbing->dnext(path, name, followsymlinks, type);
    }

    if (mChildren.size() <= mIndex)
    {
        return false;
    }

    auto& next = mChildren[mIndex];
    assert(next.get());
    path = LocalPath::fromPlatformEncodedAbsolute(next->getURI());
    name = LocalPath::fromPlatformEncodedRelative(next->getName());
    if (type)
    {
        *type = next->isFolder() ? FOLDERNODE : FILENODE;
    }

    mIndex++;
    return true;
}

std::unique_ptr<FileAccess> AndroidFileSystemAccess::newfileaccess(bool followSymLinks)
{
    if (fileWrapper != nullptr)
    {
        return std::unique_ptr<FileAccess>{
            new AndroidFileAccess{waiter,
                                  mLinuxFileSystemAccess.getdefaultfilepermissions(),
                                  followSymLinks}};
    }
    else
    {
        return std::unique_ptr<FileAccess>{
            new PosixFileAccess{waiter,
                                mLinuxFileSystemAccess.getdefaultfilepermissions(),
                                followSymLinks}};
    }
}

std::unique_ptr<DirAccess> AndroidFileSystemAccess::newdiraccess()
{
    if (fileWrapper != nullptr)
    {
        return unique_ptr<DirAccess>(new AndroidDirAccess());
    }
    else
    {
        return unique_ptr<DirAccess>(new PosixDirAccess());
    }
}

#ifdef ENABLE_SYNC
DirNotify* AndroidFileSystemAccess::newdirnotify(LocalNode& root,
                                                 const LocalPath& rootPath,
                                                 Waiter* waiter)
{
    return new AndroidDirNotify(*this, root, rootPath);
}
#endif

bool AndroidFileSystemAccess::getlocalfstype(const LocalPath& path, FileSystemType& type) const
{
    LocalPath auxPath{path};
    if (path.isURI())
    {
        auxPath = getStandartPathFromURIPath(path);
    }

    return mLinuxFileSystemAccess.getlocalfstype(auxPath, type);
}

bool AndroidFileSystemAccess::getsname(const LocalPath& p1, LocalPath& p2) const
{
    LocalPath auxP1{p1};
    if (p1.isURI())
    {
        auxP1 = getStandartPathFromURIPath(p1);
    }

    LocalPath auxP2{p2};
    if (p2.isURI())
    {
        auxP2 = getStandartPathFromURIPath(p2);
    }

    return mLinuxFileSystemAccess.getsname(auxP1, auxP2);
}

bool AndroidFileSystemAccess::renamelocal(const LocalPath& oldname,
                                          const LocalPath& newname,
                                          bool overwrite)
{
    if (oldname.isURI() && newname.isURI())
    {
        auto oldNameWrapper = AndroidFileWrapper::getAndroidFileWrapper(oldname, false, false);
        if (oldname.parentPath() == newname.parentPath())
        {
            if (!oldNameWrapper)
            {
                return false;
            }

            return oldNameWrapper->rename(newname.leafName().toPath(false)) != nullptr;
        }
        else
        {
            if (copy(oldname, newname))
            {
                if (oldNameWrapper->isFolder())
                {
                    rmdirlocal(oldname);
                }
                else
                {
                    unlinklocal(oldname);
                }
                return true;
            }

            return false;
        }
    }

    return mLinuxFileSystemAccess.renamelocal(oldname, newname, overwrite);
}

bool AndroidFileSystemAccess::copylocal(const LocalPath& oldname,
                                        const LocalPath& newname,
                                        m_time_t time)
{
    if (oldname.isURI() && newname.isURI())
    {
        if (!copy(oldname, newname))
        {
            return false;
        }

        // Note: at Android is not possible set mtime
        return true;
    }

    return mLinuxFileSystemAccess.copylocal(oldname, newname, time);
}

bool AndroidFileSystemAccess::unlinklocal(const LocalPath& p1)
{
    auto wrapper = AndroidFileWrapper::getAndroidFileWrapper(p1, false, false);
    if (!wrapper)
    {
        return false;
    }

    if (wrapper->isFolder())
    {
        return false;
    }

    return wrapper->deleteFile();
}

bool AndroidFileSystemAccess::rmdirlocal(const LocalPath& p1)
{
    emptydirlocal(p1);

    auto fileWrapper = AndroidFileWrapper::getAndroidFileWrapper(p1, false, false);
    if (!fileWrapper || fileWrapper->getChildren().size())
    {
        return false;
    }

    return fileWrapper->deleteEmptyFolder();
}

bool AndroidFileSystemAccess::mkdirlocal(const LocalPath& name,
                                         bool hidden,
                                         bool logAlreadyExistsError)
{
    return AndroidFileWrapper::getAndroidFileWrapper(name, true, true) != nullptr;
}

bool AndroidFileSystemAccess::setmtimelocal(const LocalPath& path, m_time_t time)
{
    return true;
}

bool AndroidFileSystemAccess::chdirlocal(LocalPath& path) const
{
    LocalPath auxPath{path};
    if (path.isURI())
    {
        auxPath = getStandartPathFromURIPath(path);
    }

    return mLinuxFileSystemAccess.chdirlocal(auxPath);
}

bool AndroidFileSystemAccess::issyncsupported(const LocalPath& path,
                                              bool& isnetwork,
                                              SyncError& syncError,
                                              SyncWarning& syncWarning)
{
    LocalPath auxPath{path};
    if (path.isURI())
    {
        auxPath = getStandartPathFromURIPath(path);
    }

    return mLinuxFileSystemAccess.issyncsupported(auxPath, isnetwork, syncError, syncWarning);
}

bool AndroidFileSystemAccess::expanselocalpath(const LocalPath& path, LocalPath& absolutepath)
{
    // TODO ARV androdid sync implement
    if (path.isURI())
    {
        absolutepath = path;
        return true;
    }

    return mLinuxFileSystemAccess.expanselocalpath(path, absolutepath);
}

int AndroidFileSystemAccess::getdefaultfilepermissions()
{
    return mLinuxFileSystemAccess.getdefaultfilepermissions();
}

void AndroidFileSystemAccess::setdefaultfilepermissions(int permissions)
{
    mLinuxFileSystemAccess.setdefaultfilepermissions(permissions);
}

int AndroidFileSystemAccess::getdefaultfolderpermissions()
{
    return mLinuxFileSystemAccess.getdefaultfolderpermissions();
}

void AndroidFileSystemAccess::setdefaultfolderpermissions(int permissions)
{
    mLinuxFileSystemAccess.setdefaultfolderpermissions(permissions);
}

void AndroidFileSystemAccess::osversion(string* u, bool includeArchExtraInfo) const
{
    mLinuxFileSystemAccess.osversion(u, includeArchExtraInfo);
}

void AndroidFileSystemAccess::statsid(string* id) const
{
    mLinuxFileSystemAccess.statsid(id);
}

bool AndroidFileSystemAccess::cwd(LocalPath& path) const
{
    LocalPath auxPath{path};
    if (path.isURI())
    {
        auxPath = getStandartPathFromURIPath(path);
    }

    return mLinuxFileSystemAccess.cwd(auxPath);
}

#ifdef ENABLE_SYNC
// True if the filesystem indicated by the specified path has stable FSIDs.
bool AndroidFileSystemAccess::fsStableIDs(const LocalPath& path) const
{
    LocalPath auxPath{path};
    if (path.isURI())
    {
        auxPath = getStandartPathFromURIPath(path);
    }

    return mLinuxFileSystemAccess.fsStableIDs(auxPath);
}

bool AndroidFileSystemAccess::initFilesystemNotificationSystem()
{
    return mLinuxFileSystemAccess.initFilesystemNotificationSystem();
}
#endif

ScanResult AndroidFileSystemAccess::directoryScan(const LocalPath& targetPath,
                                                  handle expectedFsid,
                                                  map<LocalPath, FSNode>& known,
                                                  std::vector<FSNode>& results,
                                                  bool followSymLinks,
                                                  unsigned& nFingerprinted)
{
    // Whether we can reuse an existing fingerprint.
    // I.e. Can we avoid computing the CRC?
    auto reuse = [](const FSNode& lhs, const FSNode& rhs)
    {
        return lhs.type == rhs.type && lhs.fsid == rhs.fsid &&
               lhs.fingerprint.mtime == rhs.fingerprint.mtime &&
               lhs.fingerprint.size == rhs.fingerprint.size;
    };

    // Where we store file information.
    struct stat metadata;

    // Try and get information about the scan target.
    bool scanTarget_followSymLink = true; // Follow symlink for the parent directory, so we retrieve
                                          // the stats of the path that the symlinks points to
    std::shared_ptr<AndroidFileWrapper> targetWrapper =
        AndroidFileWrapper::getAndroidFileWrapper(targetPath, false, true);
    std::optional<std::string> uriPath = targetWrapper ? targetWrapper->getPath() : std::nullopt;
    if (!uriPath.has_value() || stat(uriPath->c_str(), &metadata) == -1)
    {
        LOG_warn << "Failed to directoryScan: "
                 << "Unable to stat(...) scan target: " << targetPath
                 << ". Error code was: " << errno;

        return SCAN_INACCESSIBLE;
    }

    // Is the scan target a directory?
    if (!S_ISDIR(metadata.st_mode))
    {
        LOG_warn << "Failed to directoryScan: "
                 << "Scan target is not a directory: " << targetPath;

        return SCAN_INACCESSIBLE;
    }

    // Are we scanning the directory we think we are?
    if (expectedFsid != (handle)metadata.st_ino)
    {
        LOG_warn << "Failed to directoryScan: "
                 << "Scan target mismatch on expected FSID: " << targetPath << " was "
                 << expectedFsid << " now " << (handle)metadata.st_ino;

        return SCAN_FSID_MISMATCH;
    }

    // What device is this directory on?
    auto device = metadata.st_dev;

    auto children = targetWrapper->getChildren();
    for (auto child: children)
    {
        auto& result = (results.emplace_back(), results.back());
        result.localname = LocalPath::fromPlatformEncodedRelative(child->getName());

        LocalPath newpath = LocalPath::fromURIPath((child->getURI()));

        std::optional<std::string> childPath = child->getPath();

        if (!childPath.has_value() || stat(childPath->c_str(), &metadata) == -1)
        {
            LOG_warn << "directoryScan: "
                     << "Unable to stat(...) file: " << newpath << ". Error code was: " << errno;

            // Entry's unknown if we can't determine otherwise.
            result.type = TYPE_UNKNOWN;
            continue;
        }

        // result.fsid = (handle)entry->d_ino; (posix implementation)
        result.fsid = static_cast<handle>(metadata.st_ino);
        result.fingerprint.mtime = metadata.st_mtime;
        captimestamp(&result.fingerprint.mtime);

        // Are we dealing with a directory?
        if (S_ISDIR(metadata.st_mode))
        {
            // Then no fingerprint is necessary.
            result.fingerprint.size = 0;

            // Assume this directory isn't a mount point.
            result.type = FOLDERNODE;

            // Directory's a mount point.
            if (device != metadata.st_dev)
            {
                // Mark directory as a mount so we can emit a stall.
                result.type = TYPE_NESTED_MOUNT;

                // Leave a trail for debuggers.
                LOG_warn << "directoryScan: "
                         << "Encountered a nested mount: " << newpath;
            }

            continue;
        }

        if (!S_ISREG(metadata.st_mode))
        {
            LOG_warn << "directoryScan: "
                     << "Encountered a special file: " << newpath
                     << ". Mode flags were: " << (metadata.st_mode & S_IFMT);

            result.isSymlink = S_ISLNK(metadata.st_mode);
            result.type = result.isSymlink ? TYPE_SYMLINK : TYPE_SPECIAL;
            continue;
        }

        // We're dealing with a regular file.
        result.type = FILENODE;

        auto it = known.find(result.localname);

        // Can we avoid recomputing this file's fingerprint?
        if (it != known.end() && reuse(result, it->second))
        {
            result.fingerprint = std::move(it->second.fingerprint);
            continue;
        }

        AndroidFileAccess fAccess(nullptr);
        fAccess.updatelocalname(newpath, true);
        bool validOpen = fAccess.fopen(newpath, false, false, FSLogging::logOnError);

        // Only fingerprint the file if we could actually open it.
        if (!validOpen)
        {
            LOG_warn << "directoryScan: "
                     << "Unable to open file for fingerprinting: " << newpath
                     << ". Error was: " << errno;
            continue;
        }

        // Fingerprint the file.
        result.fingerprint.genfingerprint(&fAccess);

        ++nFingerprinted;
    }

    return SCAN_SUCCESS;
}

bool AndroidFileSystemAccess::hardLink(const LocalPath& source, const LocalPath& target)
{
    return false;
}

m_off_t AndroidFileSystemAccess::availableDiskSpace(const LocalPath& drivePath)
{
    LocalPath auxPath{drivePath};
    if (drivePath.isURI())
    {
        auxPath = getStandartPathFromURIPath(drivePath);
    }

    return mLinuxFileSystemAccess.availableDiskSpace(auxPath);
}

void AndroidFileSystemAccess::addevents(Waiter* w, int flag)
{
    mLinuxFileSystemAccess.addevents(w, flag);
}

void AndroidFileSystemAccess::emptydirlocal(const LocalPath& path, dev_t)
{
    auto wrapper = AndroidFileWrapper::getAndroidFileWrapper(path, false, false);
    if (!wrapper)
    {
        return;
    }

    if (wrapper->isFolder())
    {
        for (const auto& child: wrapper->getChildren())
        {
            if (child->isFolder())
            {
                LocalPath childPath = path;
                childPath.appendWithSeparator(LocalPath::fromRelativePath(child->getName()), false);
                emptydirlocal(childPath);
                child->deleteEmptyFolder();
            }
            else
            {
                child->deleteFile();
            }
        }
    }
}

LocalPath AndroidFileSystemAccess::getStandartPathFromURIPath(const LocalPath& localPath) const
{
    auto fileWrapper = AndroidFileWrapper::getAndroidFileWrapper(localPath, false, false);
    if (fileWrapper)
    {
        if (auto path = fileWrapper->getPath(); path.has_value())
        {
            LocalPath auxPath = LocalPath::fromAbsolutePath(path->c_str());
            return auxPath;
        }
    }

    return LocalPath{};
}

bool AndroidFileSystemAccess::copy(const LocalPath& oldname, const LocalPath& newname)
{
    std::shared_ptr<AndroidFileWrapper> androidfileWrapper =
        AndroidFileWrapper::getAndroidFileWrapper(oldname, false, false);

    if (!androidfileWrapper)
    {
        return false;
    }

    if (androidfileWrapper->isFolder())
    {
        if (mkdirlocal(newname, false, true))
        {
            for (const auto& child: androidfileWrapper->getChildren())
            {
                LocalPath childNewPath = newname;
                childNewPath.appendWithSeparator(LocalPath::fromRelativePath(child->getName()),
                                                 false);
                LocalPath childOldPath = oldname;
                childOldPath.appendWithSeparator(LocalPath::fromRelativePath(child->getName()),
                                                 false);
                copy(childOldPath, childNewPath);
            }
        }

        return true;
    }

    unique_ptr<FileAccess> oldFile = newfileaccess();
    unique_ptr<FileAccess> newFile = newfileaccess();
    if (oldFile->fopen(oldname, true, false, FSLogging::logOnError) &&
        newFile->fopen(newname, true, true, FSLogging::logOnError))
    {
        unsigned char buffer[16384];
        size_t pos = 0;
        bool followRead = true;
        // Set true when last pacake isn't complete,
        bool moreData = true;
        do
        {
            unsigned bytesToRead = sizeof buffer;
            if (pos + static_cast<m_off_t>(bytesToRead) > oldFile->size)
            {
                bytesToRead = oldFile->size - pos;
                moreData = false;
            }
            followRead = oldFile->frawread(static_cast<byte*>(buffer),
                                           bytesToRead,
                                           pos,
                                           true,
                                           FSLogging::logOnError);
            newFile->fwrite(static_cast<const byte*>(buffer), bytesToRead, pos);
            pos += bytesToRead;
        }
        while (followRead && moreData);
        oldFile->closef();
        newFile->closef();
        LOG_verbose << "Copying via read/write";
        return true;
    }

    LOG_warn << "Unable to copy file";
    return false;
}

AndroidDirNotify::AndroidDirNotify(AndroidFileSystemAccess& owner,
                                   LocalNode& root,
                                   const LocalPath& rootPath):
    DirNotify(rootPath),
    mLinuxDirNotify(owner.getLinuxFileSystemAccess(), root, rootPath)
{}

AddWatchResult AndroidDirNotify::addWatch(LocalNode& node, const LocalPath& path, handle fsid)
{
    LocalPath auxPath{path};
    if (auxPath.isURI())
    {
        auto androidFileWrapper{AndroidFileWrapper::getAndroidFileWrapper(auxPath, false, false)};
        auto pathStr = androidFileWrapper->getPath();
        if (pathStr.has_value())
        {
            auxPath = LocalPath::fromAbsolutePath(pathStr.value());
        }
        else
        {
            return make_pair(WatchMapIterator{}, WR_FAILURE);
        }
    }

    return mLinuxDirNotify.addWatch(node, auxPath, fsid);
}
} // namespace
