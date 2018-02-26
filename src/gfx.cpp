/**
 * @file gfx.cpp
 * @brief Platform-independent bitmap graphics transformation functionality
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
#include "mega/gfx.h"

namespace mega {
const int GfxProc::dimensions[][2] = {
    { 200, 0 },     // THUMBNAIL: square thumbnail, cropped from near center
    { 1000, 1000 }  // PREVIEW: scaled version inside 1000x1000 bounding square
};

const int GfxProc::dimensionsavatar[][2] = {
    { 250, 0 }      // AVATAR250X250: square thumbnail, cropped from near center
};

bool GfxProc::isgfx(string* localfilename)
{
    char ext[8];
    const char* supported;

    if (!(supported = supportedformats()))
    {
        return true;
    }

    if (client->fsaccess->getextension(localfilename, ext, sizeof ext))
    {
        const char* ptr;

        // FIXME: use hash
        if ((ptr = strstr(supported, ext)) && ptr[strlen(ext)] == '.')
        {
            return true;
        }
    }

    return false;
}

bool GfxProc::isvideo(string *localfilename)
{
    char ext[8];
    const char* supported;

    if (!(supported = supportedvideoformats()))
    {
        return false;
    }

    if (client->fsaccess->getextension(localfilename, ext, sizeof ext))
    {
        const char* ptr;

        // FIXME: use hash
        if ((ptr = strstr(supported, ext)) && ptr[strlen(ext)] == '.')
        {
            return true;
        }
    }

    return false;
}

const char* GfxProc::supportedformats()
{
    return NULL;
}

const char* GfxProc::supportedvideoformats()
{
    return NULL;
}

void GfxProc::transform(int& w, int& h, int& rw, int& rh, int& px, int& py)
{
    if (rh)
    {
        // rectangular rw*rh bounding box
        if (h*rw > w*rh)
        {
            w = w * rh / h;
            h = rh;
        }
        else
        {
            h = h * rw / w;
            w = rw;
        }

        px = 0;
        py = 0;

        rw = w;
        rh = h;
    }
    else
    {
        // square rw*rw crop thumbnail
        if (w < h)
        {
            h = h * rw / w;
            w = rw;
        }
        else
        {
            w = w * rw / h;
            h = rw;
        }

        px = (w - rw) / 2;
        py = (h - rw) / 3;
        
        rh = rw;
    }
}

// load bitmap image, generate all designated sizes, attach to specified upload/node handle
// FIXME: move to a worker thread to keep the engine nonblocking
int GfxProc::gendimensionsputfa(FileAccess* fa, string* localfilename, handle th, SymmCipher* key, int missing, bool checkAccess)
{
    int numputs = 0;

    if (SimpleLogger::logCurrentLevel >= logDebug)
    {
        string utf8path;
        client->fsaccess->local2path(localfilename, &utf8path);
        LOG_debug << "Creating thumb/preview for " << utf8path;
    }

    // (this assumes that the width of the largest dimension is max)
    if (readbitmap(fa, localfilename, dimensions[sizeof dimensions/sizeof dimensions[0]-1][0]))
    {
        string* jpeg = NULL;

        // successively downscale the original image
        for (int i = sizeof dimensions/sizeof dimensions[0]; i--; )
        {
            if (!jpeg)
            {
                jpeg = new string;
            }

            int w = dimensions[i][0];
            int h = dimensions[i][1];
            if (i == (sizeof dimensions/sizeof dimensions[0] - 1)
                    && this->w < w && this->h < h )
            {
                LOG_debug << "Skipping upsizing of preview";
                w = this->w;
                h = this->h;
            }

            if (missing & (1 << i) && resizebitmap(w, h, jpeg))
            {
                // store the file attribute data - it will be attached to the file
                // immediately if the upload has already completed; otherwise, once
                // the upload completes
                int creqtag = client->reqtag;
                client->reqtag = 0;
                client->putfa(th, (meta_t)i, key, jpeg, checkAccess);
                client->reqtag = creqtag;
                numputs++;

                jpeg = NULL;
            }
        }

        if (jpeg)
        {
            delete jpeg;
        }

        freebitmap();
    }

    return numputs;
}

bool GfxProc::savefa(string *localfilepath, int width, int height, string *localdstpath)
{
    if (!isgfx(localfilepath)
            // (this assumes that the width of the largest dimension is max)
        || !readbitmap(NULL, localfilepath, width > height ? width : height))
    {
        return false;
    }

    int w = width;
    int h = height;
    if (this->w < w && this->h < h)
    {
        LOG_debug << "Skipping upsizing of local preview";
        w = this->w;
        h = this->h;
    }

    string jpeg;
    bool success = resizebitmap(w, h, &jpeg);
    freebitmap();

    if (!success)
    {
        return false;
    }

    FileAccess *f = client->fsaccess->newfileaccess();
    client->fsaccess->unlinklocal(localdstpath);
    if (!f->fopen(localdstpath, false, true))
    {
        delete f;
        return false;
    }

    if (!f->fwrite((const byte*)jpeg.data(), jpeg.size(), 0))
    {
        delete f;
        return false;
    }

    delete f;
    return true;
}

GfxProc::GfxProc()
{
    client = NULL;
}
} // namespace
