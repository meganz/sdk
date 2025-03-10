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

bool AndroidFileWrapper::exists()
{
    return mAndroidFileObject != nullptr;
}

std::shared_ptr<AndroidFileWrapper>
    AndroidFileWrapper::getAndroidFileWrapper(const std::string& path)
{
    std::lock_guard<std::mutex> g(mMutex);
    auto androidFileWrapper = mRepository.get(path);
    if (androidFileWrapper.has_value())
    {
        return androidFileWrapper.value();
    }

    std::shared_ptr<AndroidFileWrapper> androidFileWrapperNew{new AndroidFileWrapper(path)};
    mRepository.put(path, androidFileWrapperNew);
    return androidFileWrapperNew;
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

    jstring pathString = (jstring)env->CallObjectMethod(mAndroidFileObject, methodID);
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

AndroidPlatformURIHelper::AndroidPlatformURIHelper()
{
    URIHandler::setPlatformHelper(this);
}

bool AndroidPlatformURIHelper::isURI(const std::string& path)
{
    std::shared_ptr<AndroidFileWrapper> fileWrapper =
        AndroidFileWrapper::getAndroidFileWrapper(path);
    if (fileWrapper->exists())
    {
        return fileWrapper->isURI();
    }

    return false;
}

std::optional<std::string> AndroidPlatformURIHelper::getName(const std::string& path)
{
    std::shared_ptr<AndroidFileWrapper> fileWrapper =
        AndroidFileWrapper::getAndroidFileWrapper(path);
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
    std::string fstr = f.asPlatformEncoded(false);
    assert(!mFileWrapper);

    mFileWrapper = AndroidFileWrapper::getAndroidFileWrapper(fstr);

    if (!mFileWrapper->exists())
    {
        return false;
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

    struct stat statbuf;
    if (::fstat(fd, &statbuf) == -1)
    {
        errorcode = errno;
        LOG_err << "Failled to call fstat: " << errorcode << "  " << strerror(errorcode);
        close(fd);
        fd = -1;
        return false;
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
        mFileWrapper.reset();
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
        mFileWrapper = AndroidFileWrapper::getAndroidFileWrapper(
            nonblocking_localname.asPlatformEncoded(false));
    }
    else
    {
        assert(nonblocking_localname.asPlatformEncoded(false) == mFileWrapper->getName());
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

    struct stat statbuf;
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

    mFileWrapper =
        AndroidFileWrapper::getAndroidFileWrapper(nonblocking_localname.platformEncoded());

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

} // namespace
