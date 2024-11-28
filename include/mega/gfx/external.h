/**
 * @file gfx/external.h
 * @brief Graphics layer interface for an external implementation
 *
 * (c) 2014 by Mega Limited, Auckland, New Zealand
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

#ifndef GFX_PROC_EXTERNAL
#define GFX_PROC_EXTERNAL

#include <string>
#include "mega.h"
#include "megaapi.h"
#include "mega/gfx.h"

namespace mega {

// bitmap graphics processor
class GfxProviderExternal : public IGfxLocalProvider
{
    MegaGfxProcessor* processor{};

    bool readbitmap(const LocalPath&, int) override;
    bool resizebitmap(int, int, string* result) override;
    void freebitmap() override;

    const char* supportedformats() override;
    const char* supportedvideoformats() override;
public:
    GfxProviderExternal() = default;
    GfxProviderExternal(MegaGfxProcessor* gfxProcessor):
        processor(gfxProcessor){};
    void setProcessor(MegaGfxProcessor* gfxProcessor);
};
} // namespace

#endif

