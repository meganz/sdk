#include <mega/android/androidFileSystem.h>
#include <mega/filesystem.h>
#include <mega/logging.h>

#include <numeric>
JavaVM* MEGAjvm = nullptr;
jclass applicationClass = nullptr;
jmethodID deviceListMID = nullptr;
jclass fileWrapper = nullptr;
jclass integerClass = nullptr;
jobject surfaceTextureHelper = nullptr;

namespace mega
{

AndroidPlatformURIHelper AndroidPlatformURIHelper::mPlatformHelper;

LRUCache<std::string, AndroidFileWrapper::URIData> AndroidFileWrapper::URIDataCache(300);
std::mutex AndroidFileWrapper::URIDataCacheLock;

AndroidFileWrapper::AndroidFileWrapper(const std::string& path):
    mURI(path)
{
    if (fileWrapper == nullptr)
    {
        LOG_err << "Error: AndroidFileWrapper::AndroidFileWrapper class not found";
        return;
    }

    auto data = getURIData(mURI);
    if (!data.has_value())
    {
        data = URIData();
    }
    else if (data->mJavaObject.get())
    {
        mJavaObject = data->mJavaObject;
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
        mJavaObject = std::make_shared<JavaObject>(env->NewGlobalRef(temporalObject));
        data->mJavaObject = mJavaObject;
        env->DeleteLocalRef(temporalObject);
        setUriData(data.value());
    }
}

AndroidFileWrapper::AndroidFileWrapper(std::shared_ptr<JavaObject> javaObject):
    mJavaObject(javaObject)
{}

AndroidFileWrapper::~AndroidFileWrapper() {}

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

    jobject fileDescriptorObj = env->CallObjectMethod(mJavaObject->mObj, methodID, write);
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

    auto data = getURIData(mURI);
    if (!data.has_value())
    {
        data = URIData();
    }
    else if (data->mIsFolder.has_value())
    {
        return data->mIsFolder.value();
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

    data->mIsFolder = env->CallBooleanMethod(mJavaObject->mObj, methodID);
    setUriData(data.value());
    return data->mIsFolder.value();
}

std::string AndroidFileWrapper::getURI() const
{
    return mURI;
}

bool AndroidFileWrapper::isURI()
{
    auto data = getURIData(mURI);

    if (!data.has_value())
    {
        data = URIData();
    }
    else if (data->mIsURI.has_value())
    {
        return data->mIsURI.value();
    }

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

    data->mIsURI =
        !env->CallStaticBooleanMethod(fileWrapper, methodID, env->NewStringUTF(mURI.c_str()));
    setUriData(data.value());
    return data->mIsURI.value();
}

std::string AndroidFileWrapper::getName()
{
    if (!exists())
    {
        return std::string();
    }

    auto data = getURIData(mURI);
    if (!data.has_value())
    {
        data = URIData();
    }
    else if (data->mName.has_value())
    {
        return data->mName.value();
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

    jstring name = static_cast<jstring>(env->CallObjectMethod(mJavaObject->mObj, methodID));

    const char* nameStr = env->GetStringUTFChars(name, nullptr);
    data->mName = nameStr;
    setUriData(data.value());
    env->ReleaseStringUTFChars(name, nameStr);
    return data->mName.value();
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

    jobject childrenUris = env->CallObjectMethod(mJavaObject->mObj, methodID);
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

std::shared_ptr<AndroidFileWrapper>
    AndroidFileWrapper::pathExists(const std::vector<std::string>& subPaths)

{
    std::shared_ptr<AndroidFileWrapper> child;
    for (const auto& childName: subPaths)
    {
        // First iteration child undef (check own children), rest iteration use matched child
        child = !child ? getChildByName(childName) : child->getChildByName(childName);
        if (!child)
        {
            return nullptr;
        }
    }

    return child;
}

std::shared_ptr<AndroidFileWrapper>
    AndroidFileWrapper::returnOrCreateByPath(const std::vector<std::string>& subPaths,
                                             bool lastIsFolder)
{
    const auto moveParentForward =
        [this, lastIsFolder, nElements = subPaths.size(), nVisited = 0u](
            std::shared_ptr<AndroidFileWrapper> parent,
            const std::string& childName) mutable -> std::shared_ptr<AndroidFileWrapper>
    {
        ++nVisited;
        std::shared_ptr<AndroidFileWrapper> child =
            parent ? parent->getChildByName(childName) : getChildByName(childName);
        if (!child)
        {
            const bool isFolder = nVisited != nElements || lastIsFolder;
            child = parent ? parent->createChild(childName, isFolder) :
                             createChild(childName, isFolder);
        }
        return child;
    };

    return std::accumulate(begin(subPaths),
                           end(subPaths),
                           std::shared_ptr<AndroidFileWrapper>{},
                           moveParentForward);
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

    jstring jname{env->NewStringUTF(childName.c_str())};
    jobject temporalObject{env->CallObjectMethod(mJavaObject->mObj, methodID, jname, isFolder)};
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

    return std::shared_ptr<AndroidFileWrapper>(
        new AndroidFileWrapper(std::make_shared<JavaObject>(globalObject)));
}

std::shared_ptr<AndroidFileWrapper> AndroidFileWrapper::getChildByName(const std::string& name)
{
    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID =
        env->GetMethodID(fileWrapper, GET_CHILD_BY_NAME, "(Ljava/lang/String;)Ljava/lang/String;");
    if (!methodID)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::getChildByName";
        return nullptr;
    }

    jstring jname{env->NewStringUTF(name.c_str())};
    jstring uriString =
        static_cast<jstring>(env->CallObjectMethod(mJavaObject->mObj, methodID, jname));
    env->DeleteLocalRef(jname);
    if (!uriString)
    {
        return nullptr;
    }

    const char* elementStr = env->GetStringUTFChars(uriString, nullptr);
    auto aux = AndroidFileWrapper::getAndroidFileWrapper(elementStr);
    env->ReleaseStringUTFChars(uriString, elementStr);
    env->DeleteLocalRef(uriString);
    return aux;
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

    jobject temporalObject = env->CallObjectMethod(mJavaObject->mObj, methodID);
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

    return std::shared_ptr<AndroidFileWrapper>(
        new AndroidFileWrapper(std::make_shared<JavaObject>(globalObject)));
}

std::optional<std::string> AndroidFileWrapper::getPath()
{
    if (!isURI())
    {
        return mURI;
    }

    auto data = getURIData(mURI);
    if (!data.has_value())
    {
        data = URIData();
    }
    else if (data->mPath.has_value())
    {
        return data->mPath.value();
    }

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

    jstring pathString = static_cast<jstring>(env->CallObjectMethod(mJavaObject->mObj, methodID));
    if (!pathString)
    {
        return std::nullopt;
    }

    const char* chars = env->GetStringUTFChars(pathString, nullptr);
    if (!chars)
    {
        return std::nullopt;
    }

    data->mPath = chars;
    setUriData(data.value());
    env->ReleaseStringUTFChars(pathString, chars);
    env->DeleteLocalRef(pathString);
    return data->mPath.value();
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

    return env->CallBooleanMethod(mJavaObject->mObj, methodID);
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

    return env->CallBooleanMethod(mJavaObject->mObj, methodID);
}

bool AndroidFileWrapper::rename(const std::string& newName)
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
        return false;
    }

    jstring jnewName = env->NewStringUTF(newName.c_str());
    jobject temporalObject = env->CallObjectMethod(mJavaObject->mObj, methodID, jnewName);
    env->DeleteLocalRef(jnewName);
    if (temporalObject != nullptr)
    {
        env->DeleteGlobalRef(mJavaObject->mObj);
        mJavaObject->mObj = env->NewGlobalRef(temporalObject);
        env->DeleteLocalRef(temporalObject);
        return true;
    }

    return false;
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
        while (!auxPath.isRootPath()) // for URIs, this method returns true just if PathURI doesn't
                                      // contains any leaf
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
            if (auto auxFileWrapper = uriFileWrapper->pathExists(children);
                auxFileWrapper || !create)
            {
                return auxFileWrapper;
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
        if (create)
        {
            LocalPath parentPath = localPath.parentPath();
            auto parentFileWrapper =
                AndroidFileWrapper::getAndroidFileWrapper(parentPath.toPath(false));
            if (parentFileWrapper->exists())
            {
                return parentFileWrapper->createChild(localPath.leafName().toPath(false),
                                                      lastIsFolder);
            }
        }
        else
        {
            return AndroidFileWrapper::getAndroidFileWrapper(localPath.toPath(false));
        }
    }

    return nullptr;
}

bool AndroidFileWrapper::exists()
{
    return mJavaObject->mObj != nullptr;
}

std::shared_ptr<AndroidFileWrapper>
    AndroidFileWrapper::getAndroidFileWrapper(const std::string& uri)
{
    std::shared_ptr<AndroidFileWrapper> androidFileWrapperNew{new AndroidFileWrapper(uri)};
    assert(androidFileWrapperNew); // this method must return a valid AndroidFileWrapper ptr,
                                   // otherwise all usages of this method must be reviewed
    return androidFileWrapperNew;
}

void AndroidFileWrapper::setUriData(const URIData& uriData)
{
    if (mURI.size())
    {
        std::unique_lock<std::mutex> lock(URIDataCacheLock);
        URIDataCache.put(mURI, uriData);
    }
}

std::optional<AndroidFileWrapper::URIData>
    AndroidFileWrapper::getURIData(const std::string& uri) const
{
    std::unique_lock<std::mutex> lock(URIDataCacheLock);
    return URIDataCache.get(uri);
}

AndroidPlatformURIHelper::AndroidPlatformURIHelper()
{
    URIHandler::setPlatformHelper(this);
}

bool AndroidPlatformURIHelper::isURI(const std::string& uri)
{
    auto androidFileWrapper{AndroidFileWrapper::getAndroidFileWrapper(uri)};
    if (androidFileWrapper->exists())
    {
        return androidFileWrapper->isURI();
    }

    return false;
}

std::optional<std::string> AndroidPlatformURIHelper::getName(const std::string& uri)
{
    auto androidFileWrapper{AndroidFileWrapper::getAndroidFileWrapper(uri)};
    if (androidFileWrapper->exists())
    {
        return androidFileWrapper->getName();
    }

    return std::nullopt;
}

std::optional<std::string> AndroidPlatformURIHelper::getParentURI(const std::string& uri)
{
    auto androidFileWrapper{AndroidFileWrapper::getAndroidFileWrapper(uri)};

    if (androidFileWrapper->exists())
    {
        std::shared_ptr<AndroidFileWrapper> parentWrapper{androidFileWrapper->getParent()};
        return parentWrapper ? std::optional<std::string>{parentWrapper->getURI()} : std::nullopt;
    }

    return std::nullopt;
}

std::optional<std::string> AndroidPlatformURIHelper::getPath(const std::string& uri)
{
    auto androidFileWrapper{AndroidFileWrapper::getAndroidFileWrapper(uri)};

    if (androidFileWrapper->exists())
    {
        return androidFileWrapper->getPath();
    }

    return std::nullopt;
}

std::optional<string_type> AndroidPlatformURIHelper::getURI(const string_type& uri,
                                                            const std::vector<string_type> leaves)
{
    auto child{AndroidFileWrapper::getAndroidFileWrapper(uri)};
    if (!child)
    {
        return std::nullopt;
    }

    for (const auto& childName: leaves)
    {
        child = child->getChildByName(childName);
        if (!child)
        {
            return std::nullopt;
        }
    }

    string_type aux;
    std::string newUri = child->getURI();
    LocalPath::path2local(&newUri, &aux);
    return aux;
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

    std::optional<std::string> path{mFileWrapper->getPath()};
    struct stat statbuf;
    const bool statCalculated = path.has_value() && stat(path->c_str(), &statbuf) != -1;
    if (statCalculated && S_ISDIR(statbuf.st_mode))
    {
        type = FOLDERNODE;
        size = 0;
        mtime = statbuf.st_mtime;
        fsid = static_cast<handle>(statbuf.st_ino);
        fsidvalid = true;
        fopenSucceeded = true;
        return true;
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

    if (!statCalculated && ::fstat(fd, &statbuf) == -1)
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

    if (!mFileWrapper || !mFileWrapper->exists())
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
    return std::unique_ptr<FileAccess>{
        new AndroidFileAccess{waiter,
                              LinuxFileSystemAccess::getdefaultfilepermissions(),
                              followSymLinks}};
}

std::unique_ptr<DirAccess> AndroidFileSystemAccess::newdiraccess()
{
    return std::unique_ptr<DirAccess>(new AndroidDirAccess());
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
    return LinuxFileSystemAccess::getlocalfstype(getStandartPath(path), type);
}

bool AndroidFileSystemAccess::getsname(const LocalPath& p1, LocalPath& p2) const
{
    p2 = getStandartPath(p2);
    return LinuxFileSystemAccess::getsname(getStandartPath(p1), p2);
}

bool AndroidFileSystemAccess::renamelocal(const LocalPath& oldname,
                                          const LocalPath& newname,
                                          bool overwrite)
{
    if (oldname.isURI() || newname.isURI())
    {
        auto oldNameWrapper = AndroidFileWrapper::getAndroidFileWrapper(oldname, false, false);
        if (!oldNameWrapper)
        {
            return false;
        }

        if (oldname.parentPath() == newname.parentPath())
        {
            return oldNameWrapper->rename(newname.leafName().toPath(false));
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

    return LinuxFileSystemAccess::renamelocal(oldname, newname, overwrite);
}

bool AndroidFileSystemAccess::copylocal(const LocalPath& oldname,
                                        const LocalPath& newname,
                                        m_time_t time)
{
    if (oldname.isURI() || newname.isURI())
    {
        if (!copy(oldname, newname))
        {
            return false;
        }

        // Note: at Android is not possible set mtime
        return true;
    }

    return LinuxFileSystemAccess::copylocal(oldname, newname, time);
}

bool AndroidFileSystemAccess::unlinklocal(const LocalPath& p1)
{
    if (auto wrapper{AndroidFileWrapper::getAndroidFileWrapper(p1, false, false)};
        wrapper && !wrapper->isFolder())
    {
        return wrapper->deleteFile();
    }

    return false;
}

bool AndroidFileSystemAccess::rmdirlocal(const LocalPath& p1)
{
    emptydirlocal(p1);

    auto androidFileWrapper{AndroidFileWrapper::getAndroidFileWrapper(p1, false, false)};
    if (!androidFileWrapper || androidFileWrapper->getChildren().size())
    {
        return false;
    }

    return androidFileWrapper->deleteEmptyFolder();
}

bool AndroidFileSystemAccess::mkdirlocal(const LocalPath& name, bool, bool)
{
    return AndroidFileWrapper::getAndroidFileWrapper(name, true, true) != nullptr;
}

bool AndroidFileSystemAccess::setmtimelocal(const LocalPath&, m_time_t)
{
    return true;
}

bool AndroidFileSystemAccess::chdirlocal(LocalPath& path) const
{
    path = getStandartPath(path);
    return LinuxFileSystemAccess::chdirlocal(path);
}

bool AndroidFileSystemAccess::issyncsupported(const LocalPath& path,
                                              bool& isnetwork,
                                              SyncError& syncError,
                                              SyncWarning& syncWarning)
{
    return LinuxFileSystemAccess::issyncsupported(getStandartPath(path),
                                                  isnetwork,
                                                  syncError,
                                                  syncWarning);
}

bool AndroidFileSystemAccess::expanselocalpath(const LocalPath& path, LocalPath& absolutepath)
{
    if (path.isURI())
    {
        absolutepath = path;
        return true;
    }

    return LinuxFileSystemAccess::expanselocalpath(path, absolutepath);
}

int AndroidFileSystemAccess::getdefaultfilepermissions()
{
    return LinuxFileSystemAccess::getdefaultfilepermissions();
}

void AndroidFileSystemAccess::setdefaultfilepermissions(int permissions)
{
    LinuxFileSystemAccess::setdefaultfilepermissions(permissions);
}

int AndroidFileSystemAccess::getdefaultfolderpermissions()
{
    return LinuxFileSystemAccess::getdefaultfolderpermissions();
}

void AndroidFileSystemAccess::setdefaultfolderpermissions(int permissions)
{
    LinuxFileSystemAccess::setdefaultfolderpermissions(permissions);
}

void AndroidFileSystemAccess::osversion(string* u, bool includeArchExtraInfo) const
{
    LinuxFileSystemAccess::osversion(u, includeArchExtraInfo);
}

void AndroidFileSystemAccess::statsid(string* id) const
{
    LinuxFileSystemAccess::statsid(id);
}

bool AndroidFileSystemAccess::cwd(LocalPath& path) const
{
    path = getStandartPath(path);
    return LinuxFileSystemAccess::cwd(path);
}

#ifdef ENABLE_SYNC
// True if the filesystem indicated by the specified path has stable FSIDs.
bool AndroidFileSystemAccess::fsStableIDs(const LocalPath& path) const
{
    return LinuxFileSystemAccess::fsStableIDs(getStandartPath(path));
}

bool AndroidFileSystemAccess::initFilesystemNotificationSystem()
{
    return LinuxFileSystemAccess::initFilesystemNotificationSystem();
}
#endif

ScanResult AndroidFileSystemAccess::directoryScan(const LocalPath& targetPath,
                                                  handle expectedFsid,
                                                  std::map<LocalPath, FSNode>& known,
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

bool AndroidFileSystemAccess::hardLink(const LocalPath&, const LocalPath&)
{
    return false;
}

m_off_t AndroidFileSystemAccess::availableDiskSpace(const LocalPath& drivePath)
{
    return LinuxFileSystemAccess::availableDiskSpace(getStandartPath(drivePath));
}

void AndroidFileSystemAccess::addevents(Waiter* w, int flag)
{
    LinuxFileSystemAccess::addevents(w, flag);
}

fsfp_t AndroidFileSystemAccess::fsFingerprint(const LocalPath& path) const
{
    LocalPath auxPath{path};
    if (auxPath.isURI())
    {
        auto wrapper = AndroidFileWrapper::getAndroidFileWrapper(path, false, false);
        auto p = wrapper ? wrapper->getPath() : std::nullopt;
        if (p.has_value())
        {
            auxPath = LocalPath::fromAbsolutePath(p.value());
        }
    }

    return LinuxFileSystemAccess::fsFingerprint(auxPath);
}

void AndroidFileSystemAccess::emptydirlocal(const LocalPath& path, dev_t)
{
    auto wrapper = AndroidFileWrapper::getAndroidFileWrapper(path, false, false);
    if (!wrapper || !wrapper->isFolder())
    {
        return;
    }

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

LocalPath AndroidFileSystemAccess::getStandartPath(const LocalPath& localPath) const
{
    if (!localPath.isURI())
    {
        return localPath;
    }

    auto androidFileWrapper{AndroidFileWrapper::getAndroidFileWrapper(localPath, false, false)};
    if (androidFileWrapper)
    {
        if (auto path = androidFileWrapper->getPath(); path.has_value())
        {
            LocalPath auxPath = LocalPath::fromAbsolutePath(path->c_str());
            return auxPath;
        }
    }

    return LocalPath{};
}

bool AndroidFileSystemAccess::copy(const LocalPath& oldname, const LocalPath& newname)
{
    auto androidfileWrapper{AndroidFileWrapper::getAndroidFileWrapper(oldname, false, false)};

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
                LocalPath childNewPath{newname};
                childNewPath.appendWithSeparator(LocalPath::fromRelativePath(child->getName()),
                                                 false);
                LocalPath childOldPath{oldname};
                childOldPath.appendWithSeparator(LocalPath::fromRelativePath(child->getName()),
                                                 false);
                copy(childOldPath, childNewPath);
            }
        }

        return true;
    }

    unique_ptr<FileAccess> oldFile{newfileaccess()};
    unique_ptr<FileAccess> newFile{newfileaccess()};
    if (oldFile->fopen(oldname, true, false, FSLogging::logOnError) &&
        newFile->fopen(newname, true, true, FSLogging::logOnError))
    {
        constexpr uint32_t BUFFER_SIZE{16384};
        unsigned char buffer[BUFFER_SIZE];
        size_t pos{0};
        bool followRead = true;
        // Set true when last pacake isn't complete,
        bool moreData = true;
        do
        {
            unsigned bytesToRead{BUFFER_SIZE};
            if (static_cast<m_off_t>(pos + bytesToRead) > oldFile->size)
            {
                bytesToRead = static_cast<unsigned>(oldFile->size) - static_cast<unsigned>(pos);
                moreData = false;
            }
            followRead = oldFile->frawread(static_cast<byte*>(buffer),
                                           bytesToRead,
                                           static_cast<m_off_t>(pos),
                                           true,
                                           FSLogging::logOnError);
            newFile->fwrite(static_cast<const byte*>(buffer),
                            bytesToRead,
                            static_cast<m_off_t>(pos));

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

bool AndroidFileSystemAccess::isFileWrapperActive(const FileSystemAccess* fsa)
{
    if (auto afsa = dynamic_cast<const AndroidFileSystemAccess*>(fsa); afsa)
    {
        return afsa->isFileWrapperActive();
    }

    return false;
}

AndroidDirNotify::AndroidDirNotify(AndroidFileSystemAccess& owner,
                                   LocalNode& root,
                                   const LocalPath& rootPath):
    LinuxDirNotify(owner, root, rootPath)
{}

AddWatchResult AndroidDirNotify::addWatch(LocalNode& node, const LocalPath& path, handle fsid)
{
    LocalPath auxPath{path};
    if (auxPath.isURI())
    {
        auto androidFileWrapper{AndroidFileWrapper::getAndroidFileWrapper(auxPath, false, false)};
        if (!androidFileWrapper)
        {
            return make_pair(WatchMapIterator{}, WR_FAILURE);
        }
        auto pathStr{androidFileWrapper->getPath()};
        if (pathStr.has_value())
        {
            auxPath = LocalPath::fromAbsolutePath(pathStr.value());
        }
        else
        {
            return make_pair(WatchMapIterator{}, WR_FAILURE);
        }
    }

    return LinuxDirNotify::addWatch(node, auxPath, fsid);
}
} // namespace
