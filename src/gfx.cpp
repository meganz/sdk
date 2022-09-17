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

bool GfxProc::isgfx(const LocalPath& localfilename)
{
    const char* supported;

    if (!(supported = mGfxProvider->supportedformats()))
    {
        // We don't have supported formats, so the build was without FREEIMAGE or other graphics processing libraries
        // Therefore we cannot graphics process any file, so return false so that we don't try.
        return false;
    }

    if (0 == strcmp(supported, "all"))
    {
        // special case for client app provided MegaGfxProcessor
        // and for our Android app.  If they don't supply a list
        // of extensions, then we don't filter.
        return true;
    }

    string ext;
    if (client->fsaccess->getextension(localfilename, ext))
    {
        const char* ptr;

        // FIXME: use hash
        if ((ptr = strstr(supported, ext.c_str())) && ptr[ext.size()] == '.')
        {
            return true;
        }
    }

    return false;
}

bool GfxProc::isvideo(const LocalPath& localfilename)
{
    const char* supported;

    if (!(supported = mGfxProvider->supportedvideoformats()))
    {
        return false;
    }

    if (0 == strcmp(supported, "all"))
    {
        // special case for client app provided MegaGfxProcessor
        // and for our Android app.  If they don't supply a list
        // of extensions, then we don't filter.
        return true;
    }

    string ext;
    if (client->fsaccess->getextension(localfilename, ext))
    {
        const char* ptr;

        // FIXME: use hash
        if ((ptr = strstr(supported, ext.c_str())) && ptr[ext.size()] == '.')
        {
            return true;
        }
    }

    return false;
}

void *GfxProc::threadEntryPoint(void *param)
{
    GfxProc* gfxProcessor = (GfxProc*)param;
    gfxProcessor->loop();
    return NULL;
}

void GfxProc::loop()
{
    GfxJob *job = NULL;
    while (!finished)
    {
        waiter.init(NEVER);
        waiter.wait();
        while ((job = requests.pop()))
        {
            if (finished)
            {
                delete job;
                break;
            }

            mutex.lock();
            LOG_debug << "Processing media file: " << job->h;

            // (this assumes that the width of the largest dimension is max)
            if (mGfxProvider->readbitmap(client->fsaccess.get(), job->localfilename, dimensions[sizeof dimensions/sizeof dimensions[0]-1][0]))
            {
                for (unsigned i = 0; i < job->imagetypes.size(); i++)
                {
                    // successively downscale the original image
                    string* jpeg = new string();
                    int w = dimensions[job->imagetypes[i]][0];
                    int h = dimensions[job->imagetypes[i]][1];

                    if (mGfxProvider->width() < w && mGfxProvider->height() < h)
                    {
                        LOG_debug << "Skipping upsizing of preview or thumbnail";
                        w = mGfxProvider->width();
                        h = mGfxProvider->height();
                    }

                    if (!mGfxProvider->resizebitmap(w, h, jpeg))
                    {
                        delete jpeg;
                        jpeg = NULL;
                    }
                    job->images.push_back(jpeg);
                }
                mGfxProvider->freebitmap();
            }
            else
            {
                for (unsigned i = 0; i < job->imagetypes.size(); i++)
                {
                    job->images.push_back(NULL);
                }
            }

            mutex.unlock();
            responses.push(job);
            client->waiter->notify();
        }
    }

    while ((job = requests.pop()))
    {
        delete job;
    }

    while ((job = responses.pop()))
    {
        for (unsigned i = 0; i < job->imagetypes.size(); i++)
        {
            delete job->images[i];
        }
        delete job;
    }
}

int GfxProc::checkevents(Waiter *)
{
    if (!client)
    {
        return 0;
    }

    GfxJob *job = NULL;
    bool needexec = false;
    while ((job = responses.pop()))
    {
        for (unsigned i = 0; i < job->images.size(); i++)
        {
            if (job->images[i])
            {
                LOG_debug << "Media file correctly processed. Attaching file attribute: " << job->h;

                // thumbnail/preview has been extracted from the main image.
                // Now we upload those to the file attribute servers
                // The file attribute will either be added to an existing node
                // or added to the eventual putnodes if this was started as part of an upload transfer
                mCheckEventsKey.setkey(job->key);
                client->putfa(job->h, job->imagetypes[i], &mCheckEventsKey, 0, std::unique_ptr<string>(job->images[i]));
            }
            else
            {
                LOG_debug << "Unable to process media file: " << job->h;

                if (job->h.isNodeHandle())
                {
                    // This case is for the automatic "Restoring missing attributes" case (from syncup() or from download completion).
                    // It doesn't matter much if we can't do it
                    // App requests don't come by this route (they supply already generated preview/thumnnail) so no need to make any callbacks
                    LOG_warn << "Media file processing failed for existing Node";
                }
                else
                {
                    if (auto it = client->fileAttributesUploading.lookupExisting(job->h.uploadHandle()))
                    {
                        // reduce the number of required attributes to let the upload continue
                        it->pendingfa.erase(job->imagetypes[i]);
                        client->checkfacompletion(job->h.uploadHandle());
                    }
                    else
                    {
                        LOG_debug << "Transfer related to media file not found: " << job->h;
                    }
                }
            }
            needexec = true;
        }
        delete job;
    }

    return needexec ? Waiter::NEEDEXEC : 0;
}

IGfxProvider::~IGfxProvider()
{
}

void IGfxProvider::transform(int& w, int& h, int& rw, int& rh, int& px, int& py)
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
int GfxProc::gendimensionsputfa(FileAccess* /*fa*/, const LocalPath& localfilename, NodeOrUploadHandle th, SymmCipher* key, int missing)
{
    if (SimpleLogger::logCurrentLevel >= logDebug)
    {
        LOG_debug << "Creating thumb/preview for " << localfilename;
    }

    GfxJob *job = new GfxJob();
    job->h = th;
    memcpy(job->key, key->key, SymmCipher::KEYLENGTH);
    job->localfilename = localfilename;

    int generatingAttrs = 0;
    for (fatype i = sizeof dimensions/sizeof dimensions[0]; i--; )
    {
        if (missing & (1 << i))
        {
            job->imagetypes.push_back(i);
            generatingAttrs += 1 << i;
        }
    }

    if (!generatingAttrs)
    {
        delete job;
        return 0;
    }

    requests.push(job);
    waiter.notify();
    return generatingAttrs;
}

bool GfxProc::savefa(const LocalPath& localfilepath, int width, int height, LocalPath& localdstpath)
{
    if (!isgfx(localfilepath))
    {
        return false;
    }

    mutex.lock();
    if (!mGfxProvider->readbitmap(client->fsaccess.get(), localfilepath, width > height ? width : height))
    {
        mutex.unlock();
        return false;
    }

    int w = width;
    int h = height;
    if (mGfxProvider->width() < w && mGfxProvider->height() < h)
    {
        LOG_debug << "Skipping upsizing of local preview";
        w = mGfxProvider->width();
        h = mGfxProvider->height();
    }

    string jpeg;
    bool success = mGfxProvider->resizebitmap(w, h, &jpeg);
    mGfxProvider->freebitmap();
    mutex.unlock();

    if (!success)
    {
        return false;
    }

    auto f = client->fsaccess->newfileaccess();
    client->fsaccess->unlinklocal(localdstpath);
    if (!f->fopen(localdstpath, false, true))
    {
        return false;
    }

    if (!f->fwrite((const byte*)jpeg.data(), unsigned(jpeg.size()), 0))
    {
        return false;
    }

    return true;
}

GfxProc::GfxProc(std::unique_ptr<IGfxProvider> middleware)
    : mGfxProvider(std::move(middleware))
{
    client = NULL;
    finished = false;
}

void GfxProc::startProcessingThread()
{
    thread.start(threadEntryPoint, this);
    threadstarted = true;
}

GfxProc::~GfxProc()
{
    finished = true;
    waiter.notify();
    assert(threadstarted);
    if (threadstarted)
    {
        thread.join();
    }
}

GfxJobQueue::GfxJobQueue()
{

}

void GfxJobQueue::push(GfxJob *job)
{
    mutex.lock();
    jobs.push_back(job);
    mutex.unlock();
}

GfxJob *GfxJobQueue::pop()
{
    mutex.lock();
    if (jobs.empty())
    {
        mutex.unlock();
        return NULL;
    }
    GfxJob *job = jobs.front();
    jobs.pop_front();
    mutex.unlock();
    return job;
}

GfxJob::GfxJob()
{

}

} // namespace
