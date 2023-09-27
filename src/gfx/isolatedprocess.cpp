#include "mega/gfx/isolatedprocess.h"

namespace mega {

std::vector<std::string> GfxProviderIsolatedProcess::generateImages(
    FileSystemAccess* fa,
    const LocalPath& localfilepath,
    const std::vector<Dimension>& dimensions)
{
    std::vector<std::string> result;
    return result;
}

const char* GfxProviderIsolatedProcess::supportedformats()
{
    if (mformats.empty())
    {
        // hard coded at moment, need to get from isolated process once
        mformats+=".jpg.png.bmp.jpeg.cut.dds.g3.gif.hdr.ico.iff.ilbm"
           ".jbig.jng.jif.koala.pcd.mng.pcx.pbm.pgm.ppm.pfm.pds.raw.3fr.ari"
           ".arw.bay.crw.cr2.cap.dcs.dcr.dng.drf.eip.erf.fff.iiq.k25.kdc.mdc.mef.mos.mrw"
           ".nef.nrw.obm.orf.pef.ptx.pxn.r3d.raf.raw.rwl.rw2.rwz.sr2.srf.srw.x3f.ras.tga"
           ".xbm.xpm.jp2.j2k.jpf.jpx.";
    }

    return mformats.c_str();
}

// video formats are not supported at moment.
const char* GfxProviderIsolatedProcess::supportedvideoformats()
{
    return nullptr;
}

}