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

void GfxProcExternal::setProcessor(MegaGfxProcessor *processor)
{
	this->processor = processor;
}

bool GfxProcExternal::isgfx(string* name)
{
	if(!processor) return false;

    size_t p = name->find_last_of('.');

    if (!(p + 1))
    {
        return false;
    }

    string ext(*name,p);

    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    char* ptr =
            strstr((char*) ".jpg.png.bmp.tif.tiff.jpeg.cut.dds.exr.g3.gif.hdr.ico.iff.ilbm"
            ".jbig.jng.jif.koala.pcd.mng.pcx.pbm.pgm.ppm.pfm.pict.pic.pct.pds.raw.3fr.ari"
            ".arw.bay.crw.cr2.cap.dcs.dcr.dng.drf.eip.erf.fff.iiq.k25.kdc.mdc.mef.mos.mrw"
            ".nef.nrw.obm.orf.pef.ptx.pxn.r3d.raf.raw.rwl.rw2.rwz.sr2.srf.srw.x3f.ras.tga"
            ".xbm.xpm.jp2.j2k.jpf.jpx.", ext.c_str());

    return ptr && ptr[ext.size()] == '.';
}

bool GfxProcExternal::readbitmap(FileAccess* /*fa*/, string* localname, int /*size*/)
{
    if(!processor) return false;

	bool result = processor->readBitmap(localname->c_str());
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

bool GfxProcExternal::resizebitmap(int rw, int rh, string* jpegout)
{
    int px, py;

    if (!w || !h) return false;
    transform(w, h, rw, rh, px, py);
    if (!w || !h) return false;

    int size = processor->getBitmapDataSize(w, h, px, py, rw, rh);
    jpegout->resize(size);
    if(size <= 0) return false;

    return processor->getBitmapData((char *)jpegout->data(), jpegout->size());
}

void GfxProcExternal::freebitmap()
{
	processor->freeBitmap();
}
} // namespace
