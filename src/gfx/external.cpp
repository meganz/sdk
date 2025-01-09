/**
 * @file gfx/external.cpp
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

#include "mega.h"
#include "mega/gfx/external.h"

namespace mega {

void GfxProviderExternal::setProcessor(MegaGfxProcessor* gfxProcessor)
{
    processor = gfxProcessor;
}

bool GfxProviderExternal::readbitmap(const LocalPath& localname, int /*size*/)
{
    if(!processor) return false;

    bool result = processor->readBitmap(localname.platformEncoded().c_str());
    if(!result) return false;

    w = processor->getWidth();
    if(w <= 0)
    {
        return false;
    }

    h = processor->getHeight();
    if(h <= 0)
    {
        return false;
    }

    return true;
}

bool GfxProviderExternal::resizebitmap(int rw, int rh, string* jpegout)
{
    if(!processor) return false;

    int px, py;

    if (!w || !h) return false;
    transform(w, h, rw, rh, px, py);
    if (!w || !h) return false;

    int size = processor->getBitmapDataSize(w, h, px, py, rw, rh);
    if(size <= 0) return false;
    jpegout->resize(static_cast<size_t>(size));

    return processor->getBitmapData((char *)jpegout->data(), jpegout->size());
}

void GfxProviderExternal::freebitmap()
{
    if(!processor) return;

    processor->freeBitmap();
}

const char *GfxProviderExternal::supportedformats()
{
    return processor ? processor->supportedImageFormats() : nullptr;
}

const char *GfxProviderExternal::supportedvideoformats()
{
    return processor ? processor->supportedVideoFormats() : nullptr;
}

} // namespace
