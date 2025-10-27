%module(directors="1") mega
%{
#include "megaapi.h"

#ifdef SWIGJAVA
#include <android/log.h>
#include <jni.h>

extern JavaVM* MEGAjvm;
jstring strEncodeUTF8 = NULL;
jclass clsString = NULL;
jmethodID ctorString = NULL;
jmethodID getBytes = NULL;
extern jclass fileWrapper;
extern jclass integerClass;
extern jclass arrayListClass;

namespace megajni
{

/** --- JNI Exception Safety Helpers --- **/
// Clear any pending Java exception; return true if there was one.
static inline bool megajni_clear_pending_exception(JNIEnv* env, const char* where = nullptr) {
    if (!env) return false;
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe(); // useful during bring-up; remove if too chatty
        env->ExceptionClear();
        // You can also log `where` to your native logger if desired.
        return true;
    }
    return false;
}

// After making *any* JNI call (NewObject, CallObjectMethod, FindClass, etc.)
#define MEGAJNI_CHECK(env, where) do { megajni::megajni_clear_pending_exception((env), (where)); } while(0)

// Optional: safely create a global ref (returns nullptr on failure, with exception cleared)
static inline jclass megajni_new_global_ref_class(JNIEnv* env, jclass local, const char* where) {
    if (!env || !local) return nullptr;
    jclass g = (jclass)env->NewGlobalRef(local);
    MEGAJNI_CHECK(env, where);
    return g;
}

jint on_load(JavaVM *jvm, void *reserved)
{
    MEGAjvm = jvm;
    JNIEnv* jenv = NULL;
    jvm->GetEnv((void**)&jenv, JNI_VERSION_1_6);

    jclass clsStringLocal = jenv->FindClass("java/lang/String");
    clsString = (jclass)jenv->NewGlobalRef(clsStringLocal);
    jenv->DeleteLocalRef(clsStringLocal);
    ctorString = jenv->GetMethodID(clsString, "<init>", "([BLjava/lang/String;)V");
    getBytes = jenv->GetMethodID(clsString, "getBytes", "(Ljava/lang/String;)[B");
    jstring strEncodeUTF8Local = jenv->NewStringUTF("UTF-8");
    strEncodeUTF8 = (jstring)jenv->NewGlobalRef(strEncodeUTF8Local);
    jenv->DeleteLocalRef(strEncodeUTF8Local);

    jclass localfileWrapper = jenv->FindClass("mega/privacy/android/data/filewrapper/FileWrapper");
    if (!localfileWrapper)
    {
        jenv->ExceptionDescribe();
        jenv->ExceptionClear();
    }

    fileWrapper = megajni_new_global_ref_class(jenv, localfileWrapper, "global ref FileWrapper");
    if (localfileWrapper) jenv->DeleteLocalRef(localfileWrapper);

    jclass localIntegerClass = jenv->FindClass("java/lang/Integer");
    if (!localIntegerClass)
    {
        jenv->ExceptionDescribe();
        jenv->ExceptionClear();
    }

    integerClass = megajni_new_global_ref_class(jenv, localIntegerClass, "global ref Integer");
    if (localIntegerClass) jenv->DeleteLocalRef(localIntegerClass);

    jclass localArrayListClass = jenv->FindClass("java/util/ArrayList");
    if (!localArrayListClass)
    {
        jenv->ExceptionDescribe();
        jenv->ExceptionClear();
    }

    arrayListClass = megajni_new_global_ref_class(jenv, localArrayListClass, "global ref ArrayList");
    if (localArrayListClass) jenv->DeleteLocalRef(localArrayListClass);

    return JNI_VERSION_1_6;
}

} // namespace megajni

#ifdef SDKLIB_ONLOAD
extern "C" jint JNIEXPORT JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved)
{
    return megajni::on_load(jvm, reserved);
}
#endif // SDKLIB_ONLOAD

#endif // SWIGJAVA
%}

#ifdef SWIGJAVA

//Automatic load of the native library
%pragma(java) jniclasscode=%{
  static {
        try {
            System.loadLibrary("mega");
        } catch (UnsatisfiedLinkError e1) {
            try {
                System.load(System.getProperty("user.dir") + "/libmega.so");
            } catch (UnsatisfiedLinkError e2) {
                try {
                    System.load(System.getProperty("user.dir") + "/libs/libmegajava.so");
                } catch (UnsatisfiedLinkError e3) {
                    try {
                        System.load(System.getProperty("user.dir") + "/libs/mega.dll");
                    } catch (UnsatisfiedLinkError e4) {
                        try {
                            System.load(System.getProperty("user.dir") + "/libmega.dylib");
                        } catch (UnsatisfiedLinkError e5) {
                            try {
                                System.load(System.getProperty("user.dir") + "/libs/libmegajava.dylib");
                            } catch (UnsatisfiedLinkError e6) {
                                System.err.println("Native code library failed to load. \n" + e1 + "\n" + e2 + "\n" + e3 + "\n" + e4 + "\n" + e5 + "\n" + e6);
                                System.exit(1);
                            }
                        }
                    }
                }
            }
        }
    }
%}

%exception {
  // If a Java exception is already pending when entering native, clear it so JNI is usable.
  megajni::megajni_clear_pending_exception(jenv, "pre-native-entry");

  try {
    $action
  } catch (const std::exception& e) {
    jclass ex = jenv->FindClass("java/lang/RuntimeException");
    if (ex) jenv->ThrowNew(ex, e.what());
    // leave with a Java exception pending; JNI will return safely to JVM
  } catch (...) {
    jclass ex = jenv->FindClass("java/lang/RuntimeException");
    if (ex) jenv->ThrowNew(ex, "Native code threw an unknown exception");
  }
}

//Use compilation-time constants in Java
%javaconst(1);

//Reduce the visibility of internal classes
%typemap(javaclassmodifiers) mega::MegaApi "class";
%typemap(javaclassmodifiers) mega::MegaListener "class";
%typemap(javaclassmodifiers) mega::MegaRequestListener "class";
%typemap(javaclassmodifiers) mega::MegaTransferListener "class";
%typemap(javaclassmodifiers) mega::MegaGlobalListener "class";
%typemap(javaclassmodifiers) mega::MegaTreeProcessor "class";
%typemap(javaclassmodifiers) mega::MegaLogger "class";
%typemap(javaclassmodifiers) mega::NodeList "class";
%typemap(javaclassmodifiers) mega::TransferList "class";
%typemap(javaclassmodifiers) mega::ShareList "class";
%typemap(javaclassmodifiers) mega::UserList "class";
%typemap(javaclassmodifiers) mega::UserAlertList "class";

%typemap(out) char*
%{
    if ($1) {
        int len = (int)strlen($1);
        jbyteArray $1_array = jenv->NewByteArray(len);
        MEGAJNI_CHECK(jenv, "out char* NewByteArray");
        if ($1_array) {
            jenv->SetByteArrayRegion($1_array, 0, len, (const jbyte*)$1);
            MEGAJNI_CHECK(jenv, "out char* SetByteArrayRegion");
            $result = (jstring) jenv->NewObject(clsString, ctorString, $1_array, strEncodeUTF8);
            MEGAJNI_CHECK(jenv, "out char* NewObject(String)");
            jenv->DeleteLocalRef($1_array);
            MEGAJNI_CHECK(jenv, "out char* DeleteLocalRef");
        } else {
            $result = nullptr; // OOM or pending exception just cleared
        }
    } else {
        $result = nullptr;
    }
%}

%typemap(in) char*
%{
    jbyteArray $1_array = 0;
    $1 = 0;
    if ($input) {
        $1_array = (jbyteArray) jenv->CallObjectMethod($input, getBytes, strEncodeUTF8);
        MEGAJNI_CHECK(jenv, "in char* String.getBytes");
        if ($1_array) {
            jsize $1_size = jenv->GetArrayLength($1_array);
            MEGAJNI_CHECK(jenv, "in char* GetArrayLength");
            $1 = new char[$1_size + 1];
            if ($1 && $1_size) {
                jenv->GetByteArrayRegion($1_array, 0, $1_size, (jbyte*)$1);
                MEGAJNI_CHECK(jenv, "in char* GetByteArrayRegion");
            }
            if ($1) $1[$1_size] = '\0';
        } // else: exception cleared; $1 remains null
    }
%}

%typemap(freearg) char*
%{
    if ($1) {
        delete [] $1;
    }
    if ($1_array) {
        jenv->DeleteLocalRef($1_array);
        MEGAJNI_CHECK(jenv, "freearg char* DeleteLocalRef");
    }
%}

%typemap(directorin,descriptor="Ljava/lang/String;") char *
%{
    $input = 0;
    if ($1)
    {
        int len = strlen($1);
        jbyteArray $1_array = jenv->NewByteArray(len);
        jenv->SetByteArrayRegion($1_array, 0, len, (const jbyte*)$1);
        $input = (jstring) jenv->NewObject(clsString, ctorString, $1_array, strEncodeUTF8);
        jenv->DeleteLocalRef($1_array);
    }
    Swig::LocalRefGuard $1_refguard(jenv, $input);
%}


//Make the "delete" method protected
%typemap(javadestruct, methodname="delete", methodmodifiers="protected synchronized") SWIGTYPE 
{   
    if (swigCPtr != 0) {
      if (swigCMemOwn) {
        swigCMemOwn = false;
        $jnicall;
      }
      swigCPtr = 0;
    }
}

%javamethodmodifiers copy ""

#endif

%feature("director:before") {
    jenv->PushLocalFrame(32); // adjust number based on expected refs per callback
}

%feature("director:after") {
    jenv->PopLocalFrame(nullptr);
    MEGAJNI_CHECK(jenv, "director PopLocalFrame");
}

%feature("director:except") {
    if (jenv->ExceptionCheck()) {
        jthrowable pending = jenv->ExceptionOccurred();
        jenv->ExceptionClear();

        // Use the app's class loader to reliably find the Kotlin class on any thread
        jclass reporterClass = nullptr;
        jclass activityThread = jenv->FindClass("android/app/ActivityThread");
        if (activityThread) {
            jmethodID currentApp = jenv->GetStaticMethodID(
                activityThread,
                "currentApplication",
                "()Landroid/app/Application;"
            );
            jobject app = jenv->CallStaticObjectMethod(activityThread, currentApp);
            if (app) {
                jclass appClass = jenv->GetObjectClass(app);
                jmethodID getClassLoader = jenv->GetMethodID(
                    appClass,
                    "getClassLoader",
                    "()Ljava/lang/ClassLoader;"
                );
                jobject loader = jenv->CallObjectMethod(app, getClassLoader);
                if (loader) {
                    jclass loaderClass = jenv->FindClass("java/lang/ClassLoader");
                    jmethodID loadClass = jenv->GetMethodID(
                        loaderClass,
                        "loadClass",
                        "(Ljava/lang/String;)Ljava/lang/Class;"
                    );
                    jstring className = jenv->NewStringUTF("mega.privacy.android.app.jni.JniExceptionReporter");
                    reporterClass = (jclass) jenv->CallObjectMethod(loader, loadClass, className);

                    jenv->DeleteLocalRef(className);
                    jenv->DeleteLocalRef(loaderClass);
                    jenv->DeleteLocalRef(loader);
                }
                jenv->DeleteLocalRef(appClass);
                jenv->DeleteLocalRef(app);
            }
            jenv->DeleteLocalRef(activityThread);
        }

        __android_log_print(ANDROID_LOG_ERROR, "MEGAJNI",
            "JniExceptionReporter class %sfound", reporterClass ? "" : "NOT ");

        if (reporterClass) {
            jfieldID handlerField = jenv->GetStaticFieldID(
                reporterClass, "handler", "Lmega/privacy/android/app/jni/JniExceptionHandler;"
            );
            jobject handler = jenv->GetStaticObjectField(reporterClass, handlerField);
            if (handler) {
                jclass handlerClass = jenv->GetObjectClass(handler);
                jmethodID onException = jenv->GetMethodID(
                    handlerClass,
                    "onJniException",
                    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"
                );

                if (onException) {
                    jclass throwableClass = jenv->GetObjectClass(pending);
                    jmethodID getMessage = jenv->GetMethodID(throwableClass, "getMessage", "()Ljava/lang/String;");
                    jmethodID toString = jenv->GetMethodID(throwableClass, "toString", "()Ljava/lang/String;");
                    jstring jmsg = (jstring) jenv->CallObjectMethod(pending, getMessage);
                    jstring jstack = (jstring) jenv->CallObjectMethod(pending, toString);

                    jstring jlocation = jenv->NewStringUTF("$action");

                    jenv->CallVoidMethod(handler, onException, jlocation, jmsg, jstack);

                    jenv->DeleteLocalRef(jlocation);
                    jenv->DeleteLocalRef(jmsg);
                    jenv->DeleteLocalRef(jstack);
                    jenv->DeleteLocalRef(throwableClass);
                }

                jenv->DeleteLocalRef(handlerClass);
                jenv->DeleteLocalRef(handler);
            }

            jenv->DeleteLocalRef(reporterClass);
        }

        jenv->DeleteLocalRef(pending);
    }
}





//Generate inheritable wrappers for listener objects
%feature("director") mega::MegaRequestListener;
%feature("director") mega::MegaTransferListener;
%feature("director") mega::MegaLogger;


#ifdef SWIGJAVA
#if SWIG_VERSION < 0x030000
%typemap(directorargout) (const char *time, int loglevel, const char *source, const char *message)
%{
	jenv->DeleteLocalRef(jtime); 
	jenv->DeleteLocalRef(jsource);
	jenv->DeleteLocalRef(jmessage); 
%}
#endif

%apply (char *STRING, size_t LENGTH) {(char *buffer, size_t size)};

#if SWIG_VERSION < 0x030012
%typemap(directorargout) (char *buffer, size_t size)
%{
    jenv->DeleteLocalRef($input);
%}
#else
%typemap(directorargout) (char *buffer, size_t size)
%{
   // not copying the buffer back to improve performance
%}
#endif

#endif

%feature("director") mega::MegaGlobalListener;
%feature("director") mega::MegaListener;
%feature("director") mega::MegaTreeProcessor;
%feature("director") mega::MegaGfxProcessor;

#ifdef SWIGJAVA

#if SWIG_VERSION < 0x030000
%typemap(directorargout) (const char* path)
%{ 
	jenv->DeleteLocalRef(jpath); 
%}
#endif

%apply (char *STRING, size_t LENGTH) {(char *bitmapData, size_t size)};
%typemap(directorin, descriptor="[B") (char *bitmapData, size_t size)
%{ 
	jbyteArray jb = (jenv)->NewByteArray($2);
	MEGAJNI_CHECK(jenv, "bitmap NewByteArray");
	$input = jb;
%}
%typemap(directorargout) (char *bitmapData, size_t size)
%{ 
	if ($input && $1 && $2 > 0) {
        jenv->GetByteArrayRegion($input, 0, $2, (jbyte*)$1);
        MEGAJNI_CHECK(jenv, "bitmap GetByteArrayRegion");
    }
    if ($input) {
        jenv->DeleteLocalRef($input);
        MEGAJNI_CHECK(jenv, "bitmap DeleteLocalRef");
    }
%}
#endif

%ignore mega::MegaApi::MEGA_DEBRIS_FOLDER;
%ignore mega::MegaNode::getNodeKey;
%ignore mega::MegaNode::getAttrString;
%ignore mega::MegaNode::getPrivateAuth;
%ignore mega::MegaNode::getPublicAuth;
%ignore mega::MegaApi::createForeignFileNode;
%ignore mega::MegaApi::createForeignFolderNode;
%ignore mega::MegaListener::onSyncFileStateChanged;
%ignore mega::MegaTransfer::getListener;
%ignore mega::MegaRequest::getListener;
%ignore mega::MegaHashSignature;
%ignore mega::SynchronousRequestListener;
%ignore mega::SynchronousTransferListener;

%newobject mega::MegaError::copy;
%newobject mega::MegaRequest::copy;
%newobject mega::MegaTransfer::copy;
%newobject mega::MegaTransferList::copy;
%newobject mega::MegaNode::copy;
%newobject mega::MegaNodeList::copy;
%newobject mega::MegaChildrenList::copy;
%newobject mega::MegaShare::copy;
%newobject mega::MegaShareList::copy;
%newobject mega::MegaUser::copy;
%newobject mega::MegaUserList::copy;
%newobject mega::MegaSetElement::copy;
%newobject mega::MegaSet::copy;
%newobject mega::MegaEvent::copy;
%newobject mega::MegaSync::copy;
%newobject mega::MegaSyncStats::copy;
%newobject mega::MegaRecentActionBucket::copy;
%newobject mega::MegaRecentActionBucketList::copy;
%newobject mega::MegaSyncStall::copy;
%newobject mega::MegaSyncStallList::copy;
%newobject mega::MegaStringMap::copy;
%newobject mega::MegaContactRequest::copy;
%newobject mega::MegaContactRequestList::copy;
%newobject mega::MegaStringList::copy;
%newobject mega::MegaAchievementsDetails::copy;
%newobject mega::MegaTimeZoneDetails::copy;
%newobject mega::MegaUserAlert::copy;
%newobject mega::MegaUserAlertList::copy;
%newobject mega::MegaAchievementsDetails::getAwardEmails;
%newobject mega::MegaTransfer::getPublicMegaNode;

%newobject mega::MegaApi::getBase64PwKey;
%newobject mega::MegaApi::getStringHash;
%newobject mega::MegaApi::handleToBase64;
%newobject mega::MegaApi::userHandleToBase64;
%newobject mega::MegaApi::dumpSession;
%newobject mega::MegaApi::dumpXMPPSession;
%newobject mega::MegaApi::getMyEmail;
%newobject mega::MegaApi::getMyUserHandle;
%newobject mega::MegaApi::getMyUser;
%newobject mega::MegaApi::getMyXMPPJid;
%newobject mega::MegaApi::getMyFingerprint;
%newobject mega::MegaApi::exportMasterKey;
%newobject mega::MegaApi::getTransfers;
%newobject mega::MegaApi::getTransferByTag;
%newobject mega::MegaApi::getTransferData;
%newobject mega::MegaApi::getChildTransfers;
%newobject mega::MegaApi::getChildren;
%newobject mega::MegaApi::getChildNode;
%newobject mega::MegaApi::getParentNode;
%newobject mega::MegaApi::getNodePath;
%newobject mega::MegaApi::getNodePathByNodeHandle;
%newobject mega::MegaApi::getNodeByPath;
%newobject mega::MegaApi::getNodeByHandle;
%newobject mega::MegaApi::getContactRequestByHandle;
%newobject mega::MegaApi::getContacts;
%newobject mega::MegaApi::getContact;
%newobject mega::MegaApi::getUserAlerts;
%newobject mega::MegaApi::getInShares;
%newobject mega::MegaApi::getInSharesList;
%newobject mega::MegaApi::getOutShares;
%newobject mega::MegaApi::getPendingOutShares;
%newobject mega::MegaApi::getPublicLinks;
%newobject mega::MegaApi::getIncomingContactRequests;
%newobject mega::MegaApi::getOutgoingContactRequests;
%newobject mega::MegaApi::getFingerprint;
%newobject mega::MegaApi::getNodeByFingerprint;
%newobject mega::MegaApi::getNodesByFingerprint;
%newobject mega::MegaApi::getNodesByOriginalFingerprint;
%newobject mega::MegaApi::getExportableNodeByFingerprint;
%newobject mega::MegaApi::getCRC;
%newobject mega::MegaApi::getNodeByCRC;
%newobject mega::MegaApi::getRootNode;
%newobject mega::MegaApi::getInboxNode;
%newobject mega::MegaApi::getRubbishNode;
%newobject mega::MegaApi::escapeFsIncompatible;
%newobject mega::MegaApi::unescapeFsIncompatible;
%newobject mega::MegaApi::base64ToBase32;
%newobject mega::MegaApi::base32ToBase64;
%newobject mega::MegaApi::search;
%newobject mega::MegaApi::getCRCFromFingerprint;
%newobject mega::MegaApi::getSessionTransferURL;
%newobject mega::MegaApi::getAccountAuth;
%newobject mega::MegaApi::authorizeNode;
%newobject mega::MegaApi::getSyncs;
%newobject mega::MegaApi::getEnabledNotifications;
%newobject mega::MegaApi::getMimeType;
%newobject mega::MegaApi::isNodeSyncableWithError;
%newobject mega::MegaApi::getVersions;

%newobject mega::MegaApi::getAutoProxySettings;
%newobject mega::MegaApi::getOverquotaWarningsTs;
%newobject mega::MegaApi::getMyCredentials;
%newobject mega::MegaApi::getUserAvatarColor;
%newobject mega::MegaApi::getUserAvatarSecondaryColor;
%newobject mega::MegaApi::getDeviceId;
%newobject mega::MegaApi::getFirstTransfer;
%newobject mega::MegaApi::getTransferByUniqueId;
%newobject mega::MegaApi::getAllNodeTags;

%newobject mega::MegaRequest::getPublicMegaNode;
%newobject mega::MegaRequest::getMegaTimeZoneDetails;
%newobject mega::MegaRequest::getMegaAccountDetails;
%newobject mega::MegaRequest::getPricing;
%newobject mega::MegaRequest::getMegaAchievementsDetails;
%newobject mega::MegaRequest::getCurrency;

%newobject mega::MegaAccountDetails::getSubscriptionMethod;
%newobject mega::MegaAccountDetails::getSubscriptionCycle;
%newobject mega::MegaAccountDetails::copy;
%newobject mega::MegaAccountDetails::getBalance;
%newobject mega::MegaAccountDetails::getSession;
%newobject mega::MegaAccountDetails::getPurchase;
%newobject mega::MegaAccountDetails::getTransaction;
%newobject mega::MegaAccountDetails::getPlan;
%newobject mega::MegaAccountDetails::getSubscription;

%newobject mega::MegaNode::getBase64Handle;
%newobject mega::MegaNode::getFileAttrString;
%newobject mega::MegaNode::PasswordNodeData::createInstance;
%newobject mega::MegaNode::unserialize;
%newobject mega::MegaNode::getTags;
%newobject mega::MegaNode::getCustomAttrNames;
%newobject mega::MegaNode::copy;
%newobject mega::MegaNode::getPublicNode;
%newobject mega::MegaNode::getPublicLink;
%newobject mega::MegaNode::getCreditCardData;
%newobject mega::MegaNode::getPasswordData;
%newobject mega::MegaNode::serialize;

typedef long long time_t;
typedef long long uint64_t;
typedef long long int64_t;
typedef long long uint32_t;
typedef long long int32_t;

%include "std_string.i"

%include "megaapi.h"
