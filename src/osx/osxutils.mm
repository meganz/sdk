#include "mega/osx/osxutils.h"
#if defined(__APPLE__) && !(TARGET_OS_IPHONE)
#include <Cocoa/Cocoa.h>
#include <SystemConfiguration/SystemConfiguration.h>
#elif TARGET_OS_IOS
#include <Foundation/Foundation.h>
#endif

#include "mega.h"

using namespace mega;
using namespace std;

enum { HTTP_PROXY = 0, HTTPS_PROXY };

void path2localMac(const string* path, string* local)
{
    if (!path->size())
    {
        *local = "";
        return;
    }
    // Multiple calls to path2localMac cause a high memory usage on macOS. To avoid it, use autorelease pool to release any temp object at the end of the pool.
    // At the end of the block, the temporary objects are released, which typically results in their deallocation thereby reducing the program’s memory footprint.
    @autoreleasepool {

        // Compatibility with new APFS filesystem
        // Use the fileSystemRepresentation property of NSString objects when creating and opening
        // files with lower-level filesystem APIs such as POSIX open(2), or when storing filenames externally from the filesystem`
        NSString *tempPath = [[NSString alloc] initWithUTF8String:path->c_str()];
        const char *pathRepresentation = NULL;
        @try
        {
            pathRepresentation = [tempPath fileSystemRepresentation];
        }
        @catch (NSException *e)
        {
             LOG_err << "Failed getting file system representation (APFS filesystem)";
             local->clear();
    #if !__has_feature(objc_arc)
             [tempPath release];
    #endif
             return;
        }

        if (pathRepresentation)
        {
           *local = pathRepresentation;
        }
        else
        {
            local->clear();
        }
    #if !__has_feature(objc_arc)
        [tempPath release];
    #endif

    }
}

#if defined(__APPLE__) && !(TARGET_OS_IPHONE)

CFTypeRef getValueFromKey(CFDictionaryRef dict, const void *key, CFTypeID type)
{
    CFTypeRef value = CFDictionaryGetValue(dict, key);
    if (!value)
    {
        return NULL;
    }

    if ((CFGetTypeID(value) == type))
    {
        return value;
    }

    return NULL;
}

bool getProxyConfiguration(CFDictionaryRef dict, int proxyType, Proxy* proxy)
{
    int isEnabled = 0;
    int port;

    if (proxyType != HTTPS_PROXY && proxyType != HTTP_PROXY)
    {
        return false;
    }

    CFStringRef proxyEnableKey = proxyType == HTTP_PROXY ? kSCPropNetProxiesHTTPEnable : kSCPropNetProxiesHTTPSEnable;
    CFStringRef proxyHostKey   = proxyType == HTTP_PROXY ? kSCPropNetProxiesHTTPProxy : kSCPropNetProxiesHTTPSProxy;
    CFStringRef portKey        = proxyType == HTTP_PROXY ? kSCPropNetProxiesHTTPPort : kSCPropNetProxiesHTTPSPort;

    CFNumberRef proxyEnabledRef = (CFNumberRef)getValueFromKey(dict, proxyEnableKey, CFNumberGetTypeID());
    if (proxyEnabledRef && CFNumberGetValue(proxyEnabledRef, kCFNumberIntType, &isEnabled) && (isEnabled != 0))
    {
        if ([(__bridge NSDictionary*)dict valueForKey: proxyType == HTTP_PROXY ? @"HTTPUser" : @"HTTPSUser"] != nil)
        {
            // Username set, skip proxy configuration. We only allow proxies withouth user/pw credentials
            // to not have to request the password to the user to read the keychain
            return false;
        }

        CFStringRef hostRef = (CFStringRef)getValueFromKey(dict, proxyHostKey, CFStringGetTypeID());
        if (hostRef)
        {
            CFIndex length = CFStringGetLength(hostRef);
            CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
            char *buffer = new char[maxSize];
            if (CFStringGetCString(hostRef, buffer, maxSize, kCFStringEncodingUTF8))
            {
                CFNumberRef portRef = (CFNumberRef)getValueFromKey(dict, portKey, CFNumberGetTypeID());
                if (portRef && CFNumberGetValue(portRef, kCFNumberIntType, &port))
                {
                    ostringstream oss;
                    oss << (proxyType == HTTP_PROXY ? "http://" : "https://") << buffer << ":" << port;
                    string link = oss.str();
                    proxy->setProxyType(Proxy::CUSTOM);
                    proxy->setProxyURL(&link);
                    delete [] buffer;
                    return true;
                }
            }
            delete [] buffer;
        }
    }
    return false;
}

void getOSXproxy(Proxy* proxy)
{
    CFDictionaryRef proxySettings = NULL;
    proxySettings = SCDynamicStoreCopyProxies(NULL);
    if (!proxySettings)
    {
        return;
    }

    if (!getProxyConfiguration(proxySettings, HTTPS_PROXY, proxy))
    {
        getProxyConfiguration(proxySettings, HTTP_PROXY, proxy);
    }
    CFRelease(proxySettings);
}
#endif
