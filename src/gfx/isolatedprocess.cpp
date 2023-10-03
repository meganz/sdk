#include "mega/gfx/isolatedprocess.h"
#include "mega/gfx/worker/client.h"
#include "mega/gfx/worker/tasks.h"
#include <algorithm>
#include <iterator>
#include <vector>

using mega::gfx::GfxClient;
using mega::gfx::GfxSize;
namespace mega {

std::vector<GfxSize> GfxProviderIsolatedProcess::toGfxSize(const std::vector<Dimension>& dimensions)
{
    std::vector<GfxSize> sizes;
    std::transform(std::begin(dimensions),
                   std::end(dimensions),
                   std::back_insert_iterator<std::vector<GfxSize>>(sizes),
                   [](const Dimension &d){ return GfxSize(d.width, d.height);});
    return sizes;
}

std::vector<std::string> GfxProviderIsolatedProcess::generateImages(
    FileSystemAccess* fa,
    const LocalPath& localfilepath,
    const std::vector<Dimension>& dimensions)
{
    auto sizes = toGfxSize(dimensions);

    // default return
    std::vector<std::string> images(dimensions.size());

    auto gfxclient = GfxClient::create();
    gfxclient.runGfxTask(localfilepath.toPath(false), std::move(sizes), images);

    return images;
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