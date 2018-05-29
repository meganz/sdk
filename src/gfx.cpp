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
        if (finished)
        {
            break;
        }

        while (job = requests.pop())
        {
            mutex.lock();
            LOG_debug << "Processing media file: " << job->h;

            // (this assumes that the width of the largest dimension is max)
            if (readbitmap(NULL, &job->localfilename, dimensions[sizeof dimensions/sizeof dimensions[0]-1][0]))
            {
                for (int i = 0; i < job->imagetypes.size(); i++)
                {
                    // successively downscale the original image
                    string* jpeg = new string();
                    int w = dimensions[job->imagetypes[i]][0];
                    int h = dimensions[job->imagetypes[i]][1];

                    if (job->imagetypes[i] == PREVIEW && this->w < w && this->h < h )
                    {
                        LOG_debug << "Skipping upsizing of preview";
                        w = this->w;
                        h = this->h;
                    }

                    if (!resizebitmap(w, h, jpeg))
                    {
                        delete jpeg;
                        jpeg = NULL;
                    }
                    job->images.push_back(jpeg);
                }
                freebitmap();
            }
            else
            {
                for (int i = 0; i < job->imagetypes.size(); i++)
                {
                    job->images.push_back(NULL);
                }
            }

            mutex.unlock();
            responses.push(job);
            client->waiter->notify();
        }
    }

    while (job = requests.pop())
    {
        delete job;
    }

    while (job = responses.pop())
    {
        for (int i = 0; i < job->imagetypes.size(); i++)
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
    SymmCipher key;
    while (job = responses.pop())
    {
        for (int i = 0; i < job->images.size(); i++)
        {
            if (job->images[i])
            {
                LOG_debug << "Media file correctly processed. Attaching file attribute: " << job->h;

                // store the file attribute data - it will be attached to the file
                // immediately if the upload has already completed; otherwise, once
                // the upload completes
                key.setkey(job->key);
                int creqtag = client->reqtag;
                client->reqtag = 0;
                client->putfa(job->h, (meta_t)job->imagetypes[i], &key, job->images[i], job->flag);
                client->reqtag = creqtag;
            }
            else
            {
                LOG_debug << "Unable to process media file: " << job->h;

                Transfer *transfer = NULL;
                handletransfer_map::iterator htit = client->faputcompletion.find(job->h);
                if (htit != client->faputcompletion.end())
                {
                    transfer = htit->second;
                }
                else
                {
                    // check if the failed attribute belongs to an active upload
                    for (transfer_map::iterator it = client->transfers[PUT].begin(); it != client->transfers[PUT].end(); it++)
                    {
                        if (it->second->uploadhandle == job->h)
                        {
                            transfer = it->second;
                            break;
                        }
                    }
                }

                if (transfer)
                {
                    // reduce the number of required attributes to let the upload continue
                    transfer->minfa--;
                    client->checkfacompletion(job->h);
                }
                else
                {
                    LOG_debug << "Transfer related to media file not found: " << job->h;
                }
            }
            needexec = true;
        }
        delete job;
    }

    return needexec ? Waiter::NEEDEXEC : 0;
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
    if (SimpleLogger::logCurrentLevel >= logDebug)
    {
        string utf8path;
        client->fsaccess->local2path(localfilename, &utf8path);
        LOG_debug << "Creating thumb/preview for " << utf8path;
    }

    GfxJob *job = new GfxJob();
    job->h = th;
    job->flag = checkAccess;
    memcpy(job->key, key->key, SymmCipher::KEYLENGTH);
    job->localfilename = *localfilename;
    for (int i = sizeof dimensions/sizeof dimensions[0]; i--; )
    {
        if (missing & (1 << i))
        {
            job->imagetypes.push_back(i);
        }
    }

    if (!job->imagetypes.size())
    {
        delete job;
        return 0;
    }

    requests.push(job);
    waiter.notify();
    return job->imagetypes.size();
}

bool GfxProc::savefa(string *localfilepath, int width, int height, string *localdstpath)
{
    if (!isgfx(localfilepath))
    {
        return false;
    }

    mutex.lock();
    if (!readbitmap(NULL, localfilepath, width > height ? width : height))
    {
        mutex.unlock();
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
    mutex.unlock();

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

GfxProc::GfxProc() : mutex(false)
{
    client = NULL;
    finished = false;
    thread.start(threadEntryPoint, this);
}

GfxProc::~GfxProc()
{
    finished = true;
    waiter.notify();
    thread.join();
}

GfxJobQueue::GfxJobQueue() : mutex(false)
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
