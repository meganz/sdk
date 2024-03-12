/**
 * @file GfxProviderCG.h
 * @brief Graphics layer using Cocoa Touch
 *
 * (c) 2013-2015 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifdef USE_IOS
#ifndef GFX_CLASS
#define GFX_CLASS GfxProviderCG

#include "mega.h"

// bitmap graphics processor
class MEGA_API GfxProviderCG : public mega::IGfxLocalProvider
{
    dispatch_semaphore_t semaphore;
    CFURLRef sourceURL;
private: // mega::GfxProc implementations
    const char* supportedformats() override;
    const char* supportedvideoformats() override;
    bool readbitmap(mega::FileSystemAccess*, const mega::LocalPath&, int) override;
    bool resizebitmap(int, int, mega::string*) override;
    void freebitmap() override;
public:
    GfxProviderCG();
    ~GfxProviderCG();
};
#endif

void ios_statsid(std::string *statsid);
void ios_appbasepath(std::string *appbasepath);
#endif
