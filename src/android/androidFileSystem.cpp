#include <mega/android/androidFileSystem.h>
#include <mega/filesystem.h>
#include <mega/logging.h>

#include <numeric>
JavaVM* MEGAjvm = nullptr;
jclass fileWrapper = nullptr;
jclass integerClass = nullptr;
jclass arrayListClass = nullptr;
/// Global reference to ChildMetadata class, cached at JNI_OnLoad time.
/// Must NOT use FindClass() from background threads — those use the bootstrap classloader
/// and cannot find application classes like ChildMetadata.
jclass childMetadataClass = nullptr;
/// Cached java/util/List class and method IDs — set at JNI_OnLoad, safe for background threads.
jclass listClass = nullptr;
jmethodID listSizeMethod = nullptr;
jmethodID listGetMethod = nullptr;

namespace mega
{

AndroidPlatformURIHelper AndroidPlatformURIHelper::mPlatformHelper;

LRUCache<std::string, AndroidFileWrapper::URIData> AndroidFileWrapper::URIDataCache(LRUCacheSize);
LRUCache<std::string, std::string> AndroidFileWrapper::localPathURICache(LRUCacheSize);
std::mutex AndroidFileWrapper::URIDataCacheLock;
std::mutex AndroidFileWrapper::localPathURICacheLock;

namespace
{
// Check JNIEnv for a pending exception. If one is found: describe it, clear it, log an
// error mentioning the calling context, and return true. Returns false when no
// exception was pending.
//
// ALWAYS clear a pending exception before the next JNI call as required by the official docs:
//
// https://docs.oracle.com/en/java/javase/25/docs/specs/jni/design.html
// "After an exception has been raised, the native code must first clear the exception
// before making other JNI calls."
//
// Usage after CallBooleanMethod / CallIntMethod / CallLongMethod (no null return):
//
//     jboolean result = env->CallBooleanMethod(...);
//     if (checkAndClearJniException(...))
//         return ...;
//
// Usage after GetMethodID / GetStaticMethodID / GetFieldID:
//
//     jmethodID methodID = env->GetMethodID(...);
//     if (methodID == nullptr)
//     {
//         checkAndClearJniException(...);
//         return ...;
//     }
//
bool checkAndClearJniException(JNIEnv* env, const char* callerFn, const char* javaMethod)
{
    if (!env->ExceptionCheck())
    {
        return false;
    }
    env->ExceptionDescribe();
    env->ExceptionClear();
    LOG_err << callerFn << ": " << javaMethod << " threw a JNI exception";
    return true;
}

// Returns the JNIEnv of the calling thread, attaching it to the JVM the first time it is
// needed. A thread attached here is detached when it exits, as Android requires; a thread
// that was already attached (eg. an app thread calling into the SDK) is left as it was.
JNIEnv* jniEnv()
{
    struct Attachment
    {
        ~Attachment()
        {
            if (!mOwned || !MEGAjvm)
                return;

            // Detach only if still attached. Nothing else detaches a thread this helper attached,
            // but that invariant is spread across call sites, and this runs at thread exit.
            JNIEnv* env{nullptr};
            if (MEGAjvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_EDETACHED)
                MEGAjvm->DetachCurrentThread();
        }

        bool mOwned{false};
    };

    static thread_local Attachment attachment;

    if (!MEGAjvm)
    {
        LOG_err << "jniEnv: no JVM available";
        return nullptr;
    }

    JNIEnv* env{nullptr};
    // GetEnv may return JNI_EDETACHED if the thread is not attached to the JVM,
    // so we need to attach it if necessary.
    if (MEGAjvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED)
    {
        if (MEGAjvm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env)
        {
            LOG_err << "jniEnv: failed to attach the current thread to the JVM";
            return nullptr;
        }

        attachment.mOwned = true;
    }

    return env;
}

} // anonymous namespace

AndroidFileWrapper::JavaObject::~JavaObject()
{
    if (JNIEnv* env = jniEnv())
    {
        env->DeleteGlobalRef(mObj);
    }
}

AndroidFileWrapper::AndroidFileWrapper(const std::string& path):
    mURI(path)
{
    if (fileWrapper == nullptr)
    {
        LOG_err << "Error: AndroidFileWrapper::AndroidFileWrapper class not found";
        return;
    }

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return;
    }

    jmethodID getAndroidFileMethod = env->GetStaticMethodID(
        fileWrapper,
        GET_ANDROID_FILE,
        "(Ljava/lang/String;)Lmega/privacy/android/data/filewrapper/FileWrapper;");

    if (getAndroidFileMethod == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::AndroidFileWrapper",
                                  "GetStaticMethodID(FileWrapper.getFromUri)");
        return;
    }

    jstring jPath = env->NewStringUTF(mURI.c_str());
    jobject temporalObject = env->CallStaticObjectMethod(fileWrapper, getAndroidFileMethod, jPath);
    checkAndClearJniException(env,
                              "AndroidFileWrapper::AndroidFileWrapper",
                              "FileWrapper.getFromUri");
    env->DeleteLocalRef(jPath);

    if (temporalObject != nullptr)
    {
        mJavaObject = std::make_shared<JavaObject>(env->NewGlobalRef(temporalObject));
        env->DeleteLocalRef(temporalObject);

        constexpr const char contentScheme[] = "content://";
        // content:// URIs (e.g. from getChildrenUris()) are already canonical, skip
        // updateURIFromFileWrapper() call. Sync mURI otherwise.
        auto isContentUri = mURI.compare(0, sizeof(contentScheme) - 1, contentScheme) == 0;
        if (!isContentUri)
        {
            updateURIFromFileWrapper();
        }
    }
}

AndroidFileWrapper::AndroidFileWrapper(std::shared_ptr<JavaObject> javaObject):
    mJavaObject(javaObject)
{
    updateURIFromFileWrapper();
}

AndroidFileWrapper::~AndroidFileWrapper() {}

int AndroidFileWrapper::getFileDescriptor(bool write)
{
    if (!exists())
    {
        return -1;
    }

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return -1;
    }

    jmethodID methodID =
        env->GetMethodID(fileWrapper, "getFileDescriptor", "(Z)Ljava/lang/Integer;");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::getFileDescriptor",
                                  "GetMethodID(FileWrapper.getFileDescriptor)");
        return -1;
    }

    jobject fileDescriptorObj = env->CallObjectMethod(mJavaObject->mObj, methodID, write);
    checkAndClearJniException(env,
                              "AndroidFileWrapper::getFileDescriptor",
                              "FileWrapper.getFileDescriptor");
    if (fileDescriptorObj && integerClass)
    {
        jmethodID intValueMethod = env->GetMethodID(integerClass, "intValue", "()I");
        if (intValueMethod == nullptr)
        {
            checkAndClearJniException(env,
                                      "AndroidFileWrapper::getFileDescriptor",
                                      "GetMethodID(Integer.intValue)");
            env->DeleteLocalRef(fileDescriptorObj);
            return -1;
        }

        const jint result = env->CallIntMethod(fileDescriptorObj, intValueMethod);
        const bool threw = checkAndClearJniException(env,
                                                     "AndroidFileWrapper::getFileDescriptor",
                                                     "Integer.intValue");
        env->DeleteLocalRef(fileDescriptorObj);
        return threw ? -1 : result;
    }

    // integerClass was not initialized, so the Integer was never unwrapped
    if (fileDescriptorObj)
    {
        env->DeleteLocalRef(fileDescriptorObj);
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

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return false;
    }

    jmethodID methodID = env->GetMethodID(fileWrapper, IS_FOLDER, "()Z");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::isFolder",
                                  "GetMethodID(FileWrapper.isFolder)");
        return false;
    }

    const jboolean isFolder = env->CallBooleanMethod(mJavaObject->mObj, methodID);
    if (checkAndClearJniException(env, "AndroidFileWrapper::isFolder", "FileWrapper.isFolder"))
    {
        return false;
    }
    data->mIsFolder = isFolder;
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

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return false;
    }

    jmethodID methodID = env->GetStaticMethodID(fileWrapper, IS_PATH, "(Ljava/lang/String;)Z");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::isURI",
                                  "GetStaticMethodID(FileWrapper.isPath)");
        return false;
    }

    jstring jUri = env->NewStringUTF(mURI.c_str());
    const jboolean isPath = env->CallStaticBooleanMethod(fileWrapper, methodID, jUri);
    const bool threw =
        checkAndClearJniException(env, "AndroidFileWrapper::isURI", "FileWrapper.isPath");
    env->DeleteLocalRef(jUri);
    if (threw)
    {
        return false;
    }
    data->mIsURI = !isPath;
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

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return "";
    }

    jmethodID methodID = env->GetMethodID(fileWrapper, GET_NAME, "()Ljava/lang/String;");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::getName",
                                  "GetMethodID(FileWrapper.getName)");
        return "";
    }

    jstring name = static_cast<jstring>(env->CallObjectMethod(mJavaObject->mObj, methodID));
    if (checkAndClearJniException(env, "AndroidFileWrapper::getName", "FileWrapper.getName") ||
        !name)
    {
        return {};
    }

    const char* nameStr = env->GetStringUTFChars(name, nullptr);
    if (!nameStr)
    {
        checkAndClearJniException(env, "AndroidFileWrapper::getName", "GetStringUTFChars");
        env->DeleteLocalRef(name);
        return {};
    }
    data->mName = nameStr;
    setUriData(data.value());
    env->ReleaseStringUTFChars(name, nameStr);
    env->DeleteLocalRef(name);
    return data->mName.value();
}

std::optional<std::vector<std::shared_ptr<AndroidFileWrapper>>> AndroidFileWrapper::getChildren()
{
    if (!exists())
    {
        return std::nullopt;
    }

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return std::nullopt;
    }

    jmethodID methodID = env->GetMethodID(fileWrapper, GET_CHILDREN_URIS, "()Ljava/util/List;");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::getChildren",
                                  "GetMethodID(FileWrapper.getChildrenUris)");
        return std::nullopt;
    }

    jobject childrenUris = env->CallObjectMethod(mJavaObject->mObj, methodID);
    if (checkAndClearJniException(env,
                                  "AndroidFileWrapper::getChildren",
                                  "FileWrapper.getChildrenUris") ||
        !childrenUris)
    {
        return std::nullopt;
    }

    // Use cached List class and method IDs (set at JNI_OnLoad).
    if (!listClass || !listSizeMethod || !listGetMethod)
    {
        LOG_err << "Error: List class/methods not initialized";
        env->DeleteLocalRef(childrenUris);
        return std::nullopt;
    }

    jint size = env->CallIntMethod(childrenUris, listSizeMethod);
    if (checkAndClearJniException(env, "AndroidFileWrapper::getChildren", "List.size"))
    {
        env->DeleteLocalRef(childrenUris);
        return std::nullopt;
    }

    std::vector<std::shared_ptr<AndroidFileWrapper>> children;
    children.reserve(static_cast<size_t>(size));
    for (jint i = 0; i < size; ++i)
    {
        jstring element = (jstring)env->CallObjectMethod(childrenUris, listGetMethod, i);
        if (checkAndClearJniException(env, "AndroidFileWrapper::getChildren", "List.get"))
        {
            env->DeleteLocalRef(childrenUris);
            return std::nullopt;
        }
        if (!element)
        {
            env->DeleteLocalRef(childrenUris);
            return std::nullopt;
        }
        const char* elementStr = env->GetStringUTFChars(element, nullptr);
        if (!elementStr)
        {
            checkAndClearJniException(env, "AndroidFileWrapper::getChildren", "GetStringUTFChars");
            env->DeleteLocalRef(element);
            env->DeleteLocalRef(childrenUris);
            return std::nullopt;
        }
        children.push_back(AndroidFileWrapper::getAndroidFileWrapper(elementStr));
        env->ReleaseStringUTFChars(element, elementStr);
        env->DeleteLocalRef(element);
    }
    env->DeleteLocalRef(childrenUris);

    return children;
}

bool AndroidFileWrapper::updateURIFromFileWrapper()
{
    if (mJavaObject == nullptr || mJavaObject->mObj == nullptr)
    {
        LOG_err << "updateURIFromFileWrapper: mJavaObject object is not valid";
        return false;
    }
    if (fileWrapper == nullptr)
    {
        LOG_err << "updateURIFromFileWrapper: fileWrapper class not initialized";
        return false;
    }

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return false;
    }

    jmethodID getUriMethodID = env->GetMethodID(fileWrapper, GET_URI, "()Ljava/lang/String;");
    if (getUriMethodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::updateURIFromFileWrapper",
                                  "GetMethodID(FileWrapper.getUri)");
        return false;
    }

    jstring jUri = (jstring)env->CallObjectMethod(mJavaObject->mObj, getUriMethodID);
    if (checkAndClearJniException(env,
                                  "AndroidFileWrapper::updateURIFromFileWrapper",
                                  "FileWrapper.getUri"))
    {
        return false;
    }
    if (jUri == nullptr)
    {
        LOG_err << "updateURIFromFileWrapper: getUri() returned null";
        return false;
    }

    const char* uriStr = env->GetStringUTFChars(jUri, nullptr);
    if (uriStr == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::updateURIFromFileWrapper",
                                  "GetStringUTFChars");
        LOG_err << "updateURIFromFileWrapper: GetStringUTFChars() returned null";
        env->DeleteLocalRef(jUri);
        return false;
    }
    mURI = uriStr;
    env->ReleaseStringUTFChars(jUri, uriStr);
    env->DeleteLocalRef(jUri);
    return true;
}

std::optional<std::vector<AndroidFileWrapper::ChildMetadata>>
    AndroidFileWrapper::getChildrenWithMetadata()
{
    if (!exists())
        return std::nullopt;

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return std::nullopt;
    }

    // "()Ljava/util/List;" — returns a java.util.List<ChildMetadata>
    jmethodID methodID =
        env->GetMethodID(fileWrapper, GET_CHILDREN_META_DATA, "()Ljava/util/List;");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::getChildrenWithMetadata",
                                  "GetMethodID(FileWrapper.getChildrenWithMetadata)");
        return std::nullopt;
    }

    jobject resultList = env->CallObjectMethod(mJavaObject->mObj, methodID);
    if (checkAndClearJniException(env,
                                  "AndroidFileWrapper::getChildrenWithMetadata",
                                  "FileWrapper.getChildrenWithMetadata") ||
        !resultList)
    {
        return std::nullopt;
    }

    // Use the class reference cached at JNI_OnLoad time.
    // FindClass() from a background thread (AttachCurrentThread) only has access to the
    // bootstrap classloader and cannot find app classes like ChildMetadata at runtime.
    if (!childMetadataClass)
    {
        LOG_err << "Error: childMetadataClass not initialized (missing JNI_OnLoad registration?)";
        env->DeleteLocalRef(resultList);
        return std::nullopt;
    }

    jfieldID uriField = env->GetFieldID(childMetadataClass, "uri", "Ljava/lang/String;");
    if (uriField == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::getChildrenWithMetadata",
                                  "GetFieldID(ChildMetadata.uri)");
        env->DeleteLocalRef(resultList);
        return std::nullopt;
    }

    jfieldID nameField = env->GetFieldID(childMetadataClass, "name", "Ljava/lang/String;");
    if (nameField == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::getChildrenWithMetadata",
                                  "GetFieldID(ChildMetadata.name)");
        env->DeleteLocalRef(resultList);
        return std::nullopt;
    }

    jfieldID isFolderField = env->GetFieldID(childMetadataClass, "isFolder", "Z");
    if (isFolderField == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::getChildrenWithMetadata",
                                  "GetFieldID(ChildMetadata.isFolder)");
        env->DeleteLocalRef(resultList);
        return std::nullopt;
    }

    jfieldID sizeField = env->GetFieldID(childMetadataClass, "size", "J");
    if (sizeField == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::getChildrenWithMetadata",
                                  "GetFieldID(ChildMetadata.size)");
        env->DeleteLocalRef(resultList);
        return std::nullopt;
    }

    jfieldID lastModField = env->GetFieldID(childMetadataClass, "lastModified", "J");
    if (lastModField == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::getChildrenWithMetadata",
                                  "GetFieldID(ChildMetadata.lastModified)");
        env->DeleteLocalRef(resultList);
        return std::nullopt;
    }

    jfieldID pathField = env->GetFieldID(childMetadataClass, "path", "Ljava/lang/String;");
    if (pathField == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::getChildrenWithMetadata",
                                  "GetFieldID(ChildMetadata.path)");
        env->DeleteLocalRef(resultList);
        return std::nullopt;
    }

    // Use cached List class and method IDs (set at JNI_OnLoad).
    // Avoids FindClass() from background threads.
    if (!listClass || !listSizeMethod || !listGetMethod)
    {
        LOG_err << "Error: List class/methods not initialized";
        env->DeleteLocalRef(resultList);
        return std::nullopt;
    }

    jint count = env->CallIntMethod(resultList, listSizeMethod);
    if (checkAndClearJniException(env, "AndroidFileWrapper::getChildrenWithMetadata", "List.size"))
    {
        env->DeleteLocalRef(resultList);
        return std::nullopt;
    }

    std::vector<ChildMetadata> children;
    children.reserve(static_cast<size_t>(count));
    const LocalPath parentPath = LocalPath::fromURIPath(mURI);

    for (jint i = 0; i < count; ++i)
    {
        // PushLocalFrame/PopLocalFrame prevents JNI local reference table overflow.
        // Default table limit is 512 entries.  Each iteration creates up to 7 local
        // refs (list item + up to 6 string fields).  Without framing, directories
        // larger than ~70 entries would crash with a hard JNI abort.
        if (env->PushLocalFrame(16) < 0)
        {
            checkAndClearJniException(env,
                                      "AndroidFileWrapper::getChildrenWithMetadata",
                                      "PushLocalFrame");
            LOG_err
                << "AndroidFileWrapper::getChildrenWithMetadata: PushLocalFrame failed at index "
                << i;
            env->DeleteLocalRef(resultList);
            return std::nullopt;
        }

        jobject item = env->CallObjectMethod(resultList, listGetMethod, i);
        if (checkAndClearJniException(env,
                                      "AndroidFileWrapper::getChildrenWithMetadata",
                                      "List.get"))
        {
            env->PopLocalFrame(nullptr);
            env->DeleteLocalRef(resultList);
            return std::nullopt;
        }
        if (!item)
        {
            LOG_warn << "AndroidFileWrapper::getChildrenWithMetadata: Item is null at index " << i
                     << " of " << count << " for uri=" << mURI;
            env->PopLocalFrame(nullptr);
            continue;
        }

        auto getString = [&](jfieldID fid) -> std::string
        {
            auto jstr = (jstring)env->GetObjectField(item, fid);
            if (checkAndClearJniException(env,
                                          "AndroidFileWrapper::getChildrenWithMetadata",
                                          "GetObjectField(ChildMetadata)") ||
                !jstr)
            {
                return {};
            }
            const char* chars = env->GetStringUTFChars(jstr, nullptr);
            std::string result(chars ? chars : "");
            if (chars)
            {
                env->ReleaseStringUTFChars(jstr, chars);
            }
            else
            {
                // GetStringUTFChars failed (typically OOM) — clear any pending
                // JNI exception so subsequent fields don't short-circuit.
                checkAndClearJniException(env,
                                          "AndroidFileWrapper::getChildrenWithMetadata",
                                          "GetStringUTFChars");
            }
            // No DeleteLocalRef needed — PopLocalFrame frees all local refs
            return result;
        };

        const jboolean isFolder = env->GetBooleanField(item, isFolderField);
        if (checkAndClearJniException(env,
                                      "AndroidFileWrapper::getChildrenWithMetadata",
                                      "GetBooleanField(isFolder)"))
        {
            env->PopLocalFrame(nullptr);
            env->DeleteLocalRef(resultList);
            return std::nullopt;
        }

        const jlong size = env->GetLongField(item, sizeField);
        if (checkAndClearJniException(env,
                                      "AndroidFileWrapper::getChildrenWithMetadata",
                                      "GetLongField(size)"))
        {
            env->PopLocalFrame(nullptr);
            env->DeleteLocalRef(resultList);
            return std::nullopt;
        }

        const jlong lastModified = env->GetLongField(item, lastModField);
        if (checkAndClearJniException(env,
                                      "AndroidFileWrapper::getChildrenWithMetadata",
                                      "GetLongField(lastModified)"))
        {
            env->PopLocalFrame(nullptr);
            env->DeleteLocalRef(resultList);
            return std::nullopt;
        }

        auto child = ChildMetadata{getString(uriField),
                                   getString(nameField),
                                   isFolder != JNI_FALSE,
                                   size,
                                   lastModified,
                                   getString(pathField)};
        {
            // child.path is empty when path resolution is not available (see
            // ChildMetadata::path); it must not be cached as a resolved path,
            // otherwise getPath() would later return "" instead of std::nullopt.
            std::optional<std::string> cachedPath =
                child.path.empty() ? std::nullopt : std::make_optional(child.path);
            std::unique_lock<std::mutex> lock(URIDataCacheLock);
            URIDataCache.put(
                child.uri,
                AndroidFileWrapper::URIData{true, child.isFolder, child.name, cachedPath});
        }
        LocalPath childPath = parentPath;
        childPath.appendWithSeparator(LocalPath::fromRelativePath(child.name), true);
        setLocalPathURI(childPath.toPath(false), child.uri);
        children.push_back(std::move(child));

        env->PopLocalFrame(nullptr);
    }

    env->DeleteLocalRef(resultList);
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

jobject AndroidFileWrapper::vectorToJavaList(JNIEnv* env, const std::vector<std::string>& vec)
{
    jmethodID init = env->GetMethodID(arrayListClass, "<init>", "()V");
    jmethodID add = env->GetMethodID(arrayListClass, "add", "(Ljava/lang/Object;)Z");

    jobject list = env->NewObject(arrayListClass, init);

    for (const std::string& str: vec)
    {
        jstring jstr = env->NewStringUTF(str.c_str());
        env->CallBooleanMethod(list, add, jstr);
        checkAndClearJniException(env, "AndroidFileWrapper::vectorToJavaList", "ArrayList.add");
        env->DeleteLocalRef(jstr);
    }

    return list;
}

std::optional<std::string> AndroidFileWrapper::createOrReturnElement(const std::string& element,
                                                                     bool create,
                                                                     bool isFolder)
{
    if (!exists())
    {
        return std::nullopt;
    }

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return std::nullopt;
    }

    jmethodID methodID =
        env->GetMethodID(fileWrapper, CREATE_NESTED_PATH, "(Ljava/util/List;ZZ)Ljava/lang/String;");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::createOrReturnElement",
                                  "GetMethodID(FileWrapper.createNestedPath)");
        return std::nullopt;
    }

    std::vector<std::string> subPaths;
    subPaths.push_back(element);

    jobject list = vectorToJavaList(env, subPaths);

    jstring uriString = static_cast<jstring>(
        env->CallObjectMethod(mJavaObject->mObj, methodID, list, create, isFolder));
    const bool threw = checkAndClearJniException(env,
                                                 "AndroidFileWrapper::createOrReturnElement",
                                                 "FileWrapper.createNestedPath");
    env->DeleteLocalRef(list);
    if (threw || uriString == nullptr)
    {
        return std::nullopt;
    }

    const char* elementStr = env->GetStringUTFChars(uriString, nullptr);
    if (elementStr == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::createOrReturnElement",
                                  "GetStringUTFChars");
        env->DeleteLocalRef(uriString);
        return std::nullopt;
    }

    std::string uri{elementStr};
    env->ReleaseStringUTFChars(uriString, elementStr);
    env->DeleteLocalRef(uriString);
    return uri;
}

std::shared_ptr<AndroidFileWrapper> AndroidFileWrapper::createChild(const std::string& childName,
                                                                    bool isFolder)
{
    if (!exists())
    {
        return nullptr;
    }

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return nullptr;
    }

    jmethodID methodID = env->GetMethodID(
        fileWrapper,
        CREATE_CHILD,
        "(Ljava/lang/String;Z)Lmega/privacy/android/data/filewrapper/FileWrapper;");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::createChild",
                                  "GetMethodID(FileWrapper.createChildFile)");
        return nullptr;
    }

    jstring jname{env->NewStringUTF(childName.c_str())};
    jobject temporalObject{env->CallObjectMethod(mJavaObject->mObj, methodID, jname, isFolder)};
    checkAndClearJniException(env,
                              "AndroidFileWrapper::createChild",
                              "FileWrapper.createChildFile");
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
    if (!exists())
    {
        return nullptr;
    }

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return nullptr;
    }

    jmethodID methodID =
        env->GetMethodID(fileWrapper, GET_CHILD_BY_NAME, "(Ljava/lang/String;)Ljava/lang/String;");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::getChildByName",
                                  "GetMethodID(FileWrapper.getChildByName)");
        return nullptr;
    }

    jstring jname{env->NewStringUTF(name.c_str())};
    jstring uriString =
        static_cast<jstring>(env->CallObjectMethod(mJavaObject->mObj, methodID, jname));
    const bool threw = checkAndClearJniException(env,
                                                 "AndroidFileWrapper::getChildByName",
                                                 "FileWrapper.getChildByName");
    env->DeleteLocalRef(jname);
    if (threw || uriString == nullptr)
    {
        return nullptr;
    }

    const char* elementStr = env->GetStringUTFChars(uriString, nullptr);
    if (elementStr == nullptr)
    {
        checkAndClearJniException(env, "AndroidFileWrapper::getChildByName", "GetStringUTFChars");
        env->DeleteLocalRef(uriString);
        return nullptr;
    }

    auto aux = AndroidFileWrapper::getAndroidFileWrapper(elementStr);
    env->ReleaseStringUTFChars(uriString, elementStr);
    env->DeleteLocalRef(uriString);
    return aux;
}

std::shared_ptr<AndroidFileWrapper> AndroidFileWrapper::getParent() const
{
    if (!exists())
    {
        return nullptr;
    }

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return nullptr;
    }

    jmethodID methodID = env->GetMethodID(fileWrapper,
                                          GET_PARENT,
                                          "()Lmega/privacy/android/data/filewrapper/FileWrapper;");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::getParent",
                                  "GetMethodID(FileWrapper.getParentFile)");
        return nullptr;
    }

    jobject temporalObject = env->CallObjectMethod(mJavaObject->mObj, methodID);
    checkAndClearJniException(env, "AndroidFileWrapper::getParent", "FileWrapper.getParentFile");
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
    if (!exists())
    {
        return std::nullopt;
    }

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

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return std::nullopt;
    }

    jmethodID methodID = env->GetMethodID(fileWrapper, GET_PATH, "()Ljava/lang/String;");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::getPath",
                                  "GetMethodID(FileWrapper.getPath)");
        return std::nullopt;
    }

    jstring pathString = static_cast<jstring>(env->CallObjectMethod(mJavaObject->mObj, methodID));
    if (checkAndClearJniException(env, "AndroidFileWrapper::getPath", "FileWrapper.getPath") ||
        !pathString)
    {
        return std::nullopt;
    }

    const char* chars = env->GetStringUTFChars(pathString, nullptr);
    if (!chars)
    {
        checkAndClearJniException(env, "AndroidFileWrapper::getPath", "GetStringUTFChars");
        env->DeleteLocalRef(pathString);
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
    if (!exists())
    {
        return false;
    }

    const std::optional<std::string> localPath = getPath();

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return false;
    }

    jmethodID methodID = env->GetMethodID(fileWrapper, DELETE_FILE, "()Z");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::deleteFile",
                                  "GetMethodID(FileWrapper.deleteFile)");
        return false;
    }

    const jboolean success = env->CallBooleanMethod(mJavaObject->mObj, methodID);
    if (checkAndClearJniException(env, "AndroidFileWrapper::deleteFile", "FileWrapper.deleteFile"))
    {
        return false;
    }
    if (success)
    {
        removeUriDataFromCache(mURI);
        if (localPath.has_value())
        {
            removeLocalPathURI(localPath.value());
        }
    }

    return success;
}

bool AndroidFileWrapper::deleteEmptyFolder()
{
    if (!exists())
    {
        return false;
    }

    const std::optional<std::string> localPath = getPath();

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return false;
    }

    jmethodID methodID = env->GetMethodID(fileWrapper, DELETE_EMPTY_FOLDER, "()Z");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::deleteEmptyFolder",
                                  "GetMethodID(FileWrapper.deleteFolderIfEmpty)");
        return false;
    }

    const jboolean success = env->CallBooleanMethod(mJavaObject->mObj, methodID);
    if (checkAndClearJniException(env,
                                  "AndroidFileWrapper::deleteEmptyFolder",
                                  "FileWrapper.deleteFolderIfEmpty"))
    {
        return false;
    }
    if (success)
    {
        removeUriDataFromCache(mURI);
        if (localPath.has_value())
        {
            removeLocalPathURI(localPath.value());
        }
    }

    return success;
}

bool AndroidFileWrapper::move(const std::string& sourceParentUri,
                              const std::string& targetParentUri)
{
    if (!exists())
    {
        LOG_warn << "Warning: AndroidFileWrapper::move source wrapper does not exist";
        return false;
    }

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return false;
    }

    jmethodID methodID = env->GetMethodID(fileWrapper,
                                          MOVE,
                                          "(Ljava/lang/String;Ljava/lang/String;)Lmega/privacy/"
                                          "android/data/filewrapper/FileWrapper;");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::move",
                                  "GetMethodID(FileWrapper.moveDocument)");
        return false;
    }

    jstring jSourceParent = env->NewStringUTF(sourceParentUri.c_str());
    if (jSourceParent == nullptr)
    {
        checkAndClearJniException(env, "AndroidFileWrapper::move", "NewStringUTF(sourceParent)");
        return false;
    }

    jstring jTargetParent = env->NewStringUTF(targetParentUri.c_str());
    if (jTargetParent == nullptr)
    {
        checkAndClearJniException(env, "AndroidFileWrapper::move", "NewStringUTF(targetParent)");
        env->DeleteLocalRef(jSourceParent);
        return false;
    }

    jobject temporalObject =
        env->CallObjectMethod(mJavaObject->mObj, methodID, jSourceParent, jTargetParent);
    checkAndClearJniException(env, "AndroidFileWrapper::move", "FileWrapper.moveDocument");
    env->DeleteLocalRef(jSourceParent);
    env->DeleteLocalRef(jTargetParent);

    if (temporalObject != nullptr)
    {
        jobject newGlobalObject = env->NewGlobalRef(temporalObject);
        if (newGlobalObject == nullptr)
        {
            checkAndClearJniException(env, "AndroidFileWrapper::move", "NewGlobalRef");
            env->DeleteLocalRef(temporalObject);
            return false;
        }

        env->DeleteGlobalRef(mJavaObject->mObj);
        mJavaObject->mObj = newGlobalObject;
        env->DeleteLocalRef(temporalObject);

        auto uriData = getURIData(mURI);
        removeUriDataFromCache(mURI);
        if (!updateURIFromFileWrapper())
        {
            LOG_err << "AndroidFileWrapper::move: failed to sync URI from FileWrapper after move";
            return false;
        }

        if (uriData.has_value())
        {
            uriData->mName = std::nullopt;
            uriData->mPath = std::nullopt;
            setUriData(uriData.value());
        }

        return true;
    }

    LOG_warn << "Warning: AndroidFileWrapper::move failed";
    return false;
}

bool AndroidFileWrapper::rename(const std::string& parentPath,
                                const std::string& newName,
                                bool overwrite)
{
    if (!exists())
    {
        return false;
    }

    JNIEnv* env = jniEnv();
    if (!env)
    {
        return false;
    }

    jmethodID methodID = env->GetMethodID(fileWrapper,
                                          RENAME_OVERRIDE,
                                          "(Ljava/lang/String;Ljava/lang/String;Z)Lmega/privacy/"
                                          "android/data/filewrapper/FileWrapper;");
    if (methodID == nullptr)
    {
        checkAndClearJniException(env,
                                  "AndroidFileWrapper::rename",
                                  "GetMethodID(FileWrapper.renameOverwrite)");
        return false;
    }

    jstring jPathName = env->NewStringUTF(parentPath.c_str());
    jstring jnewName = env->NewStringUTF(newName.c_str());
    jobject temporalObject =
        env->CallObjectMethod(mJavaObject->mObj, methodID, jPathName, jnewName, overwrite);
    checkAndClearJniException(env, "AndroidFileWrapper::rename", "FileWrapper.renameOverwrite");
    env->DeleteLocalRef(jnewName);
    env->DeleteLocalRef(jPathName);
    if (temporalObject != nullptr)
    {
        env->DeleteGlobalRef(mJavaObject->mObj);
        mJavaObject->mObj = env->NewGlobalRef(temporalObject);
        env->DeleteLocalRef(temporalObject);

        auto uriData = getURIData(mURI);
        const std::optional<std::string> oldLocalPath = getPath();

        const std::string oldUri = mURI;
        removeUriDataFromCache(oldUri);
        if (oldLocalPath.has_value())
        {
            removeLocalPathURI(oldLocalPath.value());
        }

        if (!updateURIFromFileWrapper())
        {
            LOG_err
                << "AndroidFileWrapper::rename: failed to sync URI from FileWrapper after rename";
            return false;
        }

        if (uriData.has_value())
        {
            uriData->mName = newName;
            uriData->mPath = std::nullopt;
            setUriData(uriData.value());
        }

        if (auto newLocalPath = getPath(); newLocalPath.has_value())
        {
            setLocalPathURI(newLocalPath.value(), mURI);
        }

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
        return getAndroidFileWrapperFromURI(localPath, create, lastIsFolder);
    }

    return getAndroidFileWrapperFromPath(localPath, create, lastIsFolder);
}

void AndroidFileWrapper::setLocalPathURI(const std::string& path, const std::string& uri)
{
    if (path.empty() || uri.empty())
    {
        return;
    }
    std::unique_lock<std::mutex> lock(localPathURICacheLock);
    localPathURICache.put(path, uri);
}

std::optional<std::string> AndroidFileWrapper::getLocalPathURI(const std::string& path)
{
    std::unique_lock<std::mutex> lock(localPathURICacheLock);
    return localPathURICache.get(path);
}

void AndroidFileWrapper::removeLocalPathURI(const std::string& path)
{
    if (path.empty())
    {
        return;
    }
    std::unique_lock<std::mutex> lock(localPathURICacheLock);
    localPathURICache.erase(path);
}

bool AndroidFileWrapper::ensureDotNoMediaFile(const LocalPath& directory,
                                              FileSystemAccess& fsAccess)
{
    LocalPath noMediaPath = directory;
    noMediaPath.appendWithSeparator(LocalPath::fromRelativePath(".nomedia"), true);

    if (fsAccess.fileExistsAt(noMediaPath))
    {
        return true;
    }

    auto fa = fsAccess.newfileaccess();
    return fa && fa->fopen(noMediaPath, OPEN_WRONLY, FSLogging::logOnError);
}

std::shared_ptr<AndroidFileWrapper>
    AndroidFileWrapper::getAndroidFileWrapperFromURI(const LocalPath& localPath,
                                                     bool create,
                                                     bool lastIsFolder)
{
    // Attempt to resolve from URI cache
    if (auto cachedURI = getLocalPathURI(localPath.toPath(false)); cachedURI.has_value())
    {
        auto fileWrapper = AndroidFileWrapper::getAndroidFileWrapper(cachedURI.value());

        // Check if cached reference is still valid
        if (fileWrapper->exists())
        {
            return fileWrapper;
        }
    }

    // Decompose URI path into segments
    std::vector<std::string> pathSegments;
    LocalPath pathCursor = localPath;
    while (!pathCursor.isRootPath())
    {
        auto name{pathCursor.leafOrParentName()};
        if (name.empty())
        {
            return {};
        }

        pathSegments.insert(pathSegments.begin(), name);
        pathCursor = pathCursor.parentPath();
    }

    std::shared_ptr<AndroidFileWrapper> currentWrapper =
        AndroidFileWrapper::getAndroidFileWrapper(pathCursor.toPath(false));

    if (!currentWrapper->exists())
    {
        return nullptr;
    }

    if (pathSegments.empty())
    {
        return currentWrapper;
    }

    std::optional<std::string> currentURI;
    for (auto it = pathSegments.begin(); it != pathSegments.end(); ++it)
    {
        const auto& segment = *it;
        LocalPath compositePath = pathCursor;
        compositePath.appendWithSeparator(LocalPath::fromRelativePath(segment), true);

        std::shared_ptr<AndroidFileWrapper> nextWrapper;

        if (auto cachedChildURI = getLocalPathURI(compositePath.toPath(false));
            cachedChildURI.has_value())
        {
            currentURI = cachedChildURI.value();
            pathCursor = LocalPath::fromURIPath(currentURI.value());
            nextWrapper = AndroidFileWrapper::getAndroidFileWrapper(pathCursor.toPath(false));
        }

        // Create intermediate path if necessary
        if (!nextWrapper || !nextWrapper->exists())
        {
            bool isLast = (std::next(it) == pathSegments.end());
            currentURI =
                currentWrapper->createOrReturnElement(segment, create, !isLast || lastIsFolder);

            if (!currentURI.has_value())
            {
                return nullptr;
            }

            pathCursor = LocalPath::fromURIPath(currentURI.value());
            setLocalPathURI(compositePath.toPath(false), currentURI.value());
            nextWrapper = AndroidFileWrapper::getAndroidFileWrapper(pathCursor.toPath(false));
        }

        if (!nextWrapper->exists())
        {
            return nullptr;
        }

        currentWrapper = nextWrapper;
    }

    setLocalPathURI(localPath.toPath(false), currentURI.value());
    return currentWrapper;
}

std::shared_ptr<AndroidFileWrapper>
    AndroidFileWrapper::getAndroidFileWrapperFromPath(const LocalPath& localPath,
                                                      bool create,
                                                      bool lastIsFolder)
{
    if (create)
    {
        LocalPath parentPath = localPath.parentPath();
        auto parentFileWrapper =
            AndroidFileWrapper::getAndroidFileWrapper(parentPath.toPath(false));
        if (parentFileWrapper->exists())
        {
            std::string name = localPath.leafName().toPath(false);
            auto wrapper = parentFileWrapper->getChildByName(name);
            if (wrapper || !create)
            {
                return wrapper;
            }

            return parentFileWrapper->createChild(localPath.leafName().toPath(false), lastIsFolder);
        }
    }
    else
    {
        return AndroidFileWrapper::getAndroidFileWrapper(localPath.toPath(false));
    }

    return nullptr;
}

void AndroidFileWrapper::removeUriDataFromCache(const std::string& uri)
{
    if (uri.empty())
    {
        return;
    }

    std::unique_lock<std::mutex> lock(URIDataCacheLock);
    URIDataCache.erase(uri);
}

bool AndroidFileWrapper::exists() const
{
    return mJavaObject && mJavaObject->mObj != nullptr;
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
                              OpenFlag flag,
                              FSLogging,
                              DirAccess*,
                              bool,
                              bool,
                              LocalPath*)
{
    fopenSucceeded = false;
    retry = false;
    assert(!mFileWrapper);

    const bool write = openWrite(flag);
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

void AndroidFileAccess::fCloseInternal()
{
    if (fd >= 0)
    {
        close(fd);
    }

    fd = -1;
}

void AndroidFileAccess::fclose()
{
    fCloseInternal();
}

bool AndroidFileAccess::fwrite(const void* buffer,
                               unsigned long length,
                               m_off_t offset,
                               unsigned long* numWritten,
                               bool* cretry)
{
    // Sanity.
    assert(buffer || !length);
    assert(offset >= 0);

    // Keeps logic simple.
    if (!cretry)
        cretry = &retry;

    auto numWritten_ = 0ul;

    if (!numWritten)
        numWritten = &numWritten_;

    // Assume we can't write any data to file.
    *numWritten = 0;

    // Write failures are not retriable on POSIX systems.
    *cretry = false;

    // Try and perform the write.
    auto result = pwrite(fd, buffer, length, offset);

    // Couldn't perform the write.
    if (result < 0)
        return false;

    // Let the user know how many bytes were written.
    *numWritten = static_cast<unsigned long>(result);

    // Write is successful if all bytes were be written.
    return length == *numWritten;
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

AndroidFileAccess::~AndroidFileAccess()
{
    fCloseInternal();
}

std::shared_ptr<AndroidFileWrapper> AndroidFileAccess::stealFileWrapper()
{
    sysclose();
    return std::exchange(mFileWrapper, nullptr);
}

bool AndroidFileAccess::setSparse()
{
    return true;
}

auto AndroidFileAccess::getFileSize() const
    -> std::optional<std::pair<std::uint64_t, std::uint64_t>>
{
    // File isn't open.
    if (fd < 0)
        return std::nullopt;

    struct stat attributes;

    // Couldn't retrieve the file's attributes.
    if (::fstat(fd, &attributes) < 0)
        return std::nullopt;

    // Note that st_blocks is reported in units of 512B sectors.
    auto allocatedSize = static_cast<std::uint64_t>(attributes.st_blocks) * 512ul;

    // st_size is reported in units of bytes.
    auto reportedSize = static_cast<std::uint64_t>(attributes.st_size);

    return std::make_pair(allocatedSize, reportedSize);
}

AutoFileHandle AndroidFileAccess::dupFileDescriptor()
{
    return dup(fd);
}

bool AndroidFileAccess::sysread(void* buffer,
                                unsigned long length,
                                m_off_t offset,
                                bool* cretry,
                                int* cerrorcode)
{
    // Keeps logic simple.
    if (!cerrorcode)
        cerrorcode = &errorcode;

    if (!cretry)
        cretry = &retry;

    // Perform the read.
    auto result = ::mega::sysread(fd, buffer, length, offset, cretry, cerrorcode);

    // Read failed.
    if (result < 0)
        return false;

    // Read was successful if all bytes were read.
    return static_cast<unsigned long>(result) == length;
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

    auto childrenMetaData = mFileWrapper->getChildrenWithMetadata();
    if (!childrenMetaData.has_value())
    {
        LOG_warn << "AndroidDirAccess::dopen: getChildrenWithMetadata() failed for "
                 << mFileWrapper->getURI();
        mChildren.clear();
        return false;
    }

    mChildren = std::move(childrenMetaData.value());
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

    const auto& next = mChildren[mIndex];
    path = LocalPath::fromPlatformEncodedAbsolute(next.uri);
    name = LocalPath::fromPlatformEncodedRelative(next.name);
    if (type)
    {
        *type = next.isFolder ? FOLDERNODE : FILENODE;
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

// replace characters that are not allowed in local fs names with a %xx escape sequence
bool AndroidFileSystemAccess::needsTrailingDotEscape(FileSystemType) const
{
    // SAF/DocumentFile cannot store names ending in '.'. Escape trailing dots consistently so
    // sync/transfers can round-trip the original cloud name via unescapefsincompatible().
    return true;
}

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
            auto parent =
                AndroidFileWrapper::getAndroidFileWrapper(oldname.parentPath(), false, true);
            if (!parent)
            {
                return false;
            }

            bool success = oldNameWrapper->rename(parent->getURI(),
                                                  newname.leafName().toPath(false),
                                                  overwrite);
            if (success)
            {
                AndroidFileWrapper::removeLocalPathURI(oldname.toPath(false));
                AndroidFileWrapper::setLocalPathURI(newname.toPath(false),
                                                    oldNameWrapper->getURI());
            }
            target_exists = !overwrite && !success;
            return success;
        }
        else
        {
            // Cross-parent move within the same SAF tree. Preferred flow:
            //   1. Rename source to the final leaf inside the source parent. Typical sync
            //      finalize has source parent = .debris/tmp (hidden, not indexed by
            //      MediaProvider), so SAF renameDocument is cheap (~50 ms).
            //   2. moveDocument with the final leaf already in place — one SAF call, no
            //      follow-up rename at the indexed target parent.
            //
            // This avoids the ~687 ms follow-up rename observed when moveDocument keeps
            // the source's temp name (.getxfer.*.mega) and forces a rename into an indexed
            // folder (e.g. Pictures/), which triggers MediaProvider reindex synchronously.
            //
            // Fallbacks (in order): move-then-rename at the target parent when pre-rename
            // fails; copy+delete when moveDocument fails.
            LOG_verbose << "AndroidFileSystemAccess::renamelocal cross-parent URI rename: "
                        << oldname.toPath(false) << " -> " << newname.toPath(false);

            auto sourceParent =
                AndroidFileWrapper::getAndroidFileWrapper(oldname.parentPath(), false, true);
            auto targetParent =
                AndroidFileWrapper::getAndroidFileWrapper(newname.parentPath(), false, true);

            // Where the file actually lives on disk by the time we reach the
            // copy+delete fallback.
            LocalPath copySource = oldname;
            if (sourceParent == nullptr || targetParent == nullptr)
            {
                LOG_warn << "AndroidFileSystemAccess::renamelocal cannot resolve source/target "
                            "parent wrappers "
                            "(sourceParent="
                         << (sourceParent ? "ok" : "null")
                         << " targetParent=" << (targetParent ? "ok" : "null")
                         << "); falling back to copy+delete";
            }
            else
            {
                const std::string oldLeaf = oldname.leafName().toPath(false);
                const std::string newLeaf = newname.leafName().toPath(false);

                // Step 1: pre-move rename inside source parent (skip if leaf names already match).
                bool preRenameAttempted = (oldLeaf != newLeaf);
                bool preRenameOk = !preRenameAttempted;
                if (preRenameAttempted)
                {
                    // override always false, we don't want overwrite if file already exists.
                    // This file is going to be moved
                    preRenameOk = oldNameWrapper->rename(sourceParent->getURI(), newLeaf, false);
                    if (preRenameOk)
                    {
                        LOG_verbose << "AndroidFileSystemAccess::renamelocal pre-move rename OK ("
                                    << oldLeaf << " -> " << newLeaf << " in source parent)";
                    }
                }

                // Step 2: moveDocument to target parent.
                if (oldNameWrapper->move(sourceParent->getURI(), targetParent->getURI()))
                {
                    if (preRenameOk)
                    {
                        // File already carries the final leaf — no target-side rename needed.
                        const char* tag = preRenameAttempted ? "rename+move" : "move only";
                        LOG_verbose << "AndroidFileSystemAccess::renamelocal FAST PATH OK (" << tag
                                    << ") " << oldname.toPath(false) << " -> "
                                    << newname.toPath(false);
                        // Populate path → URI cache so the post-finalize fsFingerprint hits
                        // instantly instead of walking SAF segments.
                        AndroidFileWrapper::setLocalPathURI(newname.toPath(false),
                                                            oldNameWrapper->getURI());
                        AndroidFileWrapper::removeLocalPathURI(oldname.toPath(false));
                        return true;
                    }

                    // Pre-rename failed but move succeeded — fall back to target-parent rename.
                    bool renamed =
                        oldNameWrapper->rename(targetParent->getURI(), newLeaf, overwrite);
                    if (renamed)
                    {
                        LOG_info << "AndroidFileSystemAccess::renamelocal move+rename fallback OK "
                                 << oldname.toPath(false) << " -> " << newname.toPath(false);
                        AndroidFileWrapper::setLocalPathURI(newname.toPath(false),
                                                            oldNameWrapper->getURI());
                        AndroidFileWrapper::removeLocalPathURI(oldname.toPath(false));
                    }

                    target_exists = !overwrite && !renamed;
                    return renamed;
                }

                // moveDocument failed. If we pre-renamed, the source now lives at
                // <sourceParent>/<newLeaf>; route copy+delete from that effective source.
                if (preRenameAttempted && preRenameOk)
                {
                    // Try to undo the pre-rename so the source is restored to its original
                    // name, then we can fallback to copy+delete which uses oldname
                    if (oldNameWrapper->rename(sourceParent->getURI(), oldLeaf, false))
                    {
                        LOG_info << "AndroidFileSystemAccess::renamelocal: rolled back "
                                    "pre-rename; source restored to "
                                 << oldname.toPath(false);
                    }
                    else
                    {
                        // Rollback failed — file still sits at <sourceParent>/<newLeaf>.
                        // Point copySource there so the copy+delete below can still
                        // physically reach the file (last-resort to avoid orphaning)
                        copySource = oldname.parentPath();
                        copySource.appendWithSeparator(newname.leafName(), true);
                        LOG_warn << "AndroidFileSystemAccess::renamelocal: pre-rename "
                                    "rollback FAILED; copy+delete will use renamed "
                                    "source at "
                                 << copySource.toPath(false);
                    }
                }
            }
            // Unified copy+delete fallback. copySource was selected above:
            //   - oldname in the common case (or after a successful rollback)
            //   - <sourceParent>/<newLeaf> only if rollback failed
            LOG_warn << "AndroidFileSystemAccess::renamelocal SLOW PATH copy+delete: "
                     << copySource.toPath(false) << " -> " << newname.toPath(false);

            if (copy(copySource, newname, overwrite))
            {
                if (oldNameWrapper->isFolder())
                {
                    rmdirlocal(copySource);
                }
                else
                {
                    unlinklocal(copySource);
                }
                LOG_info << "AndroidFileSystemAccess::renamelocal SLOW PATH completed "
                         << "(copy+delete) -> " << copySource.toPath(false) << " -> "
                         << newname.toPath(false);
                return true;
            }

            LOG_err << "AndroidFileSystemAccess::renamelocal SLOW PATH copy+delete FAILED for "
                    << copySource.toPath(false) << " -> " << newname.toPath(false);
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
        if (!copy(oldname, newname, true))
        {
            return false;
        }

        setmtimelocal(newname, time);
        return true;
    }

    return LinuxFileSystemAccess::copylocal(oldname, newname, time);
}

bool AndroidFileSystemAccess::unlinklocal(const LocalPath& p1)
{
    if (auto wrapper{AndroidFileWrapper::getAndroidFileWrapper(p1, false, false)};
        wrapper && !wrapper->isFolder())
    {
        const bool ok = wrapper->deleteFile();
        if (ok)
        {
            AndroidFileWrapper::removeLocalPathURI(p1.toPath(false));
        }
        return ok;
    }

    return false;
}

bool AndroidFileSystemAccess::rmdirlocal(const LocalPath& p1)
{
    emptydirlocal(p1);

    auto androidFileWrapper{AndroidFileWrapper::getAndroidFileWrapper(p1, false, false)};
    if (!androidFileWrapper)
    {
        return false;
    }

    auto children = androidFileWrapper->getChildren();
    if (!children.has_value() || !children->empty())
    {
        return false;
    }

    const bool ok = androidFileWrapper->deleteEmptyFolder();
    if (ok)
    {
        AndroidFileWrapper::removeLocalPathURI(p1.toPath(false));
    }
    return ok;
}

bool AndroidFileSystemAccess::mkdirlocal(const LocalPath& name, bool, bool)
{
    auto wrapper = AndroidFileWrapper::getAndroidFileWrapper(name, true, true);
    if (!wrapper)
    {
        return false;
    }

    return true;
}

bool AndroidFileSystemAccess::setmtimelocal(const LocalPath& path, m_time_t mtime)
{
    auto standardPath = getStandartPath(path);
    if (standardPath.empty())
    {
        return false;
    }

    return LinuxFileSystemAccess::setmtimelocal(standardPath, mtime);
}

std::pair<bool, m_time_t> AndroidFileSystemAccess::getmtimelocal(const LocalPath& path)
{
    auto standardPath = getStandartPath(path);
    if (standardPath.empty())
    {
        return {false, 0};
    }

    return LinuxFileSystemAccess::getmtimelocal(standardPath);
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

    return expandLocalPathFileSystem(path, absolutepath);
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

    // Batch metadata retrieval — single JNI call regardless of directory size.
    auto childrenMetadata = targetWrapper->getChildrenWithMetadata();
    if (!childrenMetadata.has_value())
    {
        LOG_warn << "directoryScan: getChildrenWithMetadata() failed for: " << targetPath;
        return SCAN_INACCESSIBLE;
    }

    for (const auto& childMeta: childrenMetadata.value())
    {
        auto& result = (results.emplace_back(), results.back());
        result.localname = LocalPath::fromPlatformEncodedRelative(childMeta.name);

        struct stat childStat;
        bool hasStat = false;

        // Use the filesystem path returned by the batch query if available.
        // For external storage (ExternalStorageProvider) this is always non-empty.
        // For other authorities it's empty and we fall back to SCAN_UNKNOWN below.
        if (!childMeta.path.empty() && stat(childMeta.path.c_str(), &childStat) != -1)
        {
            hasStat = true;
        }

        if (!hasStat)
        {
            LOG_warn << "directoryScan: unable to stat child: " << childMeta.name
                     << " (path=" << childMeta.path << ")";
            result.type = TYPE_UNKNOWN;
            continue;
        }

        result.fsid = static_cast<handle>(childStat.st_ino);
        result.fingerprint.mtime = childStat.st_mtime;
        captimestamp(&result.fingerprint.mtime);

        if (S_ISDIR(childStat.st_mode))
        {
            result.fingerprint.size = 0;
            result.type = FOLDERNODE;
            if (device != childStat.st_dev)
            {
                result.type = TYPE_NESTED_MOUNT;
                LOG_warn << "directoryScan: nested mount: " << childMeta.name;
            }
            continue;
        }

        if (!S_ISREG(childStat.st_mode))
        {
            result.isSymlink = S_ISLNK(childStat.st_mode);
            result.type = result.isSymlink ? TYPE_SYMLINK : TYPE_SPECIAL;
            LOG_warn << "directoryScan: special/symlink file: " << childMeta.name;
            continue;
        }

        result.type = FILENODE;
        result.fingerprint.size = metadata.st_size;

        // Check if existing fingerprint can be reused (same inode, mtime, size).
        auto it = known.find(result.localname);
        if (it != known.end() && reuse(result, it->second))
        {
            result.fingerprint = std::move(it->second.fingerprint);
            continue;
        }

        // Fingerprint needed — use SAF-mediated AndroidFileAccess for parity with
        // the previously-working code. The batch metadata path still gives us
        // 1-IPC-per-directory for the listing; only the per-file open is via SAF.
        // (The earlier PosixFileAccess::fopen() shortcut was removed because it
        // produced fingerprint/scan instability that stalled sync.)
        LocalPath childUriPath = LocalPath::fromURIPath(childMeta.uri);
        AndroidFileAccess fAccess(nullptr);
        fAccess.updatelocalname(childUriPath, true);
        if (!fAccess.fopen(childUriPath, OPEN_RDONLY, FSLogging::logOnError))
        {
            LOG_warn << "directoryScan: unable to open for fingerprinting: " << childMeta.name;
            continue;
        }
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

    auto children = wrapper->getChildren();
    if (!children.has_value())
    {
        LOG_warn << "AndroidFileSystemAccess::emptydirlocal: getChildren() failed for "
                 << path.toPath(false);
        return;
    }

    for (const auto& child: children.value())
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

bool AndroidFileSystemAccess::copy(const LocalPath& oldname,
                                   const LocalPath& newname,
                                   bool overwrite)
{
    auto androidfileWrapper{AndroidFileWrapper::getAndroidFileWrapper(oldname, false, false)};

    if (!androidfileWrapper)
    {
        return false;
    }

    if (androidfileWrapper->isFolder())
    {
        if (!mkdirlocal(newname, false, true))
        {
            LOG_err << "AndroidFileSystemAccess::copy: mkdirlocal failed for "
                    << newname.toPath(false);
            return false;
        }

        auto children = androidfileWrapper->getChildren();
        if (!children.has_value())
        {
            LOG_err << "AndroidFileSystemAccess::copy: getChildren() failed for "
                    << oldname.toPath(false) << "; aborting to avoid partial copy";
            return false;
        }

        bool allOk = true;
        for (const auto& child: children.value())
        {
            LocalPath childNewPath{newname};
            childNewPath.appendWithSeparator(LocalPath::fromRelativePath(child->getName()), false);
            LocalPath childOldPath{oldname};
            childOldPath.appendWithSeparator(LocalPath::fromRelativePath(child->getName()), false);
            if (!copy(childOldPath, childNewPath, overwrite))
            {
                LOG_warn << "AndroidFileSystemAccess::copy: child copy failed: "
                         << childOldPath.toPath(false) << " -> " << childNewPath.toPath(false);
                allOk = false;
            }
        }

        return allOk;
    }

    unique_ptr<FileAccess> oldFile{newfileaccess()};
    if (!oldFile->fopen(oldname, OPEN_RDONLY, FSLogging::logOnError))
    {
        LOG_warn << "Unable to open source file, copy failed";
        return false;
    }

    // Check if destination exists if overwrite is false
    if (!overwrite)
    {
        if (auto exitingFileWrapper =
                AndroidFileWrapper::getAndroidFileWrapper(newname, false, false);
            exitingFileWrapper && exitingFileWrapper->exists())
        {
            target_exists = true;
            LOG_info << "Destination file already exists and overwrite is false";
            return false;
        }
    }

    unique_ptr<FileAccess> newFile{newfileaccess()};
    if (!newFile->fopen(newname, OPEN_RDWR, FSLogging::logOnError))
    {
        LOG_warn << "Unable to open target file, copy failed";
        return false;
    }

    {
        if (!newFile->ftruncate(0))
        {
            LOG_warn << "Failed to truncate destination before copy: " << newname;
        }
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
