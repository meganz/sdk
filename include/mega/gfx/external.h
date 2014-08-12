/**
 * @file freeimage.h
 * @brief Graphics layer implementation using FreeImage
 *
 * (c) 2014 by Mega Limited, Wellsford, New Zealand
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

#ifdef USE_EXTERNAL_GFX
#ifndef GFX_CLASS
#define GFX_CLASS GfxProcExternal

#include <string>
#include "mega/posix/megafs.h"

namespace mega {

class GfxProcessor
{
public:
	virtual bool readBitmap(const char* path) { return false; }
	virtual int getWidth() { return 0; }
	virtual int getHeight() { return 0; }
	virtual int getBitmapDataSize(int w, int h, int px, int py, int rw, int rh) { return 0; }
	virtual bool getBitmapData(char *bitmapData, size_t size) { return false; }
	virtual void freeBitmap() {}
	virtual ~GfxProcessor() {};
};
	
// bitmap graphics processor
class GfxProcExternal : public GfxProc
{
	GfxProcessor *processor;
    int w, h;

    bool readbitmap(FileAccess*, string*, int);
    bool resizebitmap(int, int, string*);
    void freebitmap();

public:
    bool isgfx(string*);
    void setProcessor(GfxProcessor *processor);
};
} // namespace

#endif
#endif
