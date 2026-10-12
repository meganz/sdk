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

#include "mega/gfx.h"

#include "mega.h"
#include "mega/gfx/GfxProcCG.h"
#include "mega/logging.h"
#include "mega/utils.h"

#include <numeric>
#include <tuple>

namespace mega {

// from low resolution to high resolution. gendimensionsputfa relies on this order.
const std::vector<GfxDimension> GfxProc::DIMENSIONS = {
    { 200, 0 },     // THUMBNAIL: square thumbnail, cropped from near center
    { 1000, 1000 }  // PREVIEW: scaled version inside 1000x1000 bounding square
};

const std::vector<GfxDimension> GfxProc::DIMENSIONS_AVATAR = {
    { 250, 0 }      // AVATAR250X250: square thumbnail, cropped from near center
};

std::unique_ptr<IGfxProvider> IGfxProvider::createInternalGfxProvider()
{
#if USE_FREEIMAGE
    return std::make_unique<::mega::GfxProviderFreeImage>();
#elif USE_IOS
    return std::make_unique<GfxProviderCG>();
#else
    return nullptr;
#endif
}

bool GfxProc::isgfx(const std::string& conceptualname) const
{
    // Clarity.
    auto callback = std::bind(&IGfxProvider::supportedformats, mGfxProvider.get());

    // Let our caller know if localfilename is a supported format.
    return isSupportedFormat(std::move(callback), conceptualname);
}

bool GfxProc::isvideo(const std::string& conceptualname) const
{
    // Clarity.
    auto callback = std::bind(&IGfxProvider::supportedvideoformats, mGfxProvider.get());

    // Let our caller know if localfilename is a supported format.
    return isSupportedFormat(std::move(callback), conceptualname);
}

void *GfxProc::threadEntryPoint(void *param)
{
    GfxProc* gfxProcessor = (GfxProc*)param;
    gfxProcessor->loop();
    return NULL;
}

std::vector<GfxDimension> GfxProc::getJobDimensions(const GfxJob& job) const
{
    std::vector<GfxDimension> jobDimensions;

    for (auto i: job.imagetypes)
    {
        assert(i < DIMENSIONS.size());
        jobDimensions.push_back(DIMENSIONS[i]);
    }

    return jobDimensions;
}

void GfxProc::loop()
{
    GfxJob *job = NULL;
    while (!finished)
    {
        waiter.init(NEVER);
        waiter.wait();
        while ((job = requests.pop()) != nullptr)
        {
            if (finished)
            {
                delete job;
                break;
            }

            LOG_debug << "Processing media file: " << job->h;

            auto images = generateImages(*job);
            for (auto& image : images)
            {
                job->images.push_back(image.empty() ? nullptr : new string(std::move(image)));
            }

            responses.push(job);
            client->waiter->notify();
        }
    }

    while ((job = requests.pop()) != nullptr)
    {
        delete job;
    }

    while ((job = responses.pop()) != nullptr)
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
    while ((job = responses.pop()) != nullptr)
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
                if (client->putfa(job->h,
                                  job->imagetypes[i],
                                  &mCheckEventsKey,
                                  0,
                                  std::unique_ptr<string>(job->images[i])) != API_OK)
                {
                    continue; // no needexec for this one
                }
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

std::vector<std::string>
    IGfxLocalProvider::generateImages(const std::string& conceptualname,
                                      const LocalPath& localfilepath,
                                      const std::vector<GfxDimension>& dimensions)
{
    std::vector<std::string> images(dimensions.size());

    int maxDimension = std::accumulate(
        dimensions.begin(),
        dimensions.end(),
        0,
        [](int max, const GfxDimension& d) { return std::max(max, std::max(d.w(), d.h())); });

    if (readbitmap(conceptualname, localfilepath, maxDimension))
    {
        for (unsigned int i = 0; i < dimensions.size(); ++i)
        {
            int targetWidth = dimensions[i].w(), targetHeight = dimensions[i].h();
            if (width() < targetWidth && height() < targetHeight)
            {
                LOG_debug << "Skipping upsizing of local preview";
                targetWidth = width();
                targetHeight = height();
            }
            // LOG_verbose << "resizebitmap w/h: " << targetWidth << "/" << targetHeight;

            // For thumbnail, PNG is allowed images with transparency
            const auto hint = (dimensions[i] == GfxProc::DIMENSIONS[GfxProc::THUMBNAIL]) ?
                                  Hint::FORMAT_PNG :
                                  Hint::NONE;
            string image;
            if (resizebitmap(targetWidth, targetHeight, &image, hint))
            {
                images[i] = std::move(image);
            }
        }
        freebitmap();
    }
    else
    {
        LOG_err << "Error reading bitmap for " << localfilepath;
    }

    return images;
}

void IGfxLocalProvider::transform(int& w, int& h, int& rw, int& rh, int& px, int& py)
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
int GfxProc::gendimensionsputfa(const std::string& conceptualname,
                                const LocalPath& localfilename,
                                NodeOrUploadHandle th,
                                SymmCipher* key,
                                int missing)
{
    LOG_debug << "Creating thumb/preview for " << localfilename << "(" << conceptualname << ")";

    GfxJob *job = new GfxJob();
    job->h = th;
    memcpy(job->key, key->key, SymmCipher::KEYLENGTH);
    job->localfilename = localfilename;
    job->name = conceptualname;

    int generatingAttrs = 0;
    for (fatype i = static_cast<fatype>(DIMENSIONS.size()); i--; )
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

std::vector<std::string> GfxProc::generateImages(const GfxJob& job)
{
    auto dimensions = getJobDimensions(job);

    std::lock_guard<std::mutex> g(mutex);

    return mGfxProvider->generateImages(job.name, job.localfilename, dimensions);
}

std::string GfxProc::generateOneImage(const std::string& conceptualname,
                                      const LocalPath& localfilepath,
                                      const GfxDimension& dimension)
{
    std::lock_guard<std::mutex> g(mutex);

    auto images = mGfxProvider->generateImages(conceptualname,
                                               localfilepath,
                                               std::vector<GfxDimension>{dimension});

    return images[0];
}

bool GfxProc::savefa(const LocalPath& localfilepath,
                     const GfxDimension& dimension,
                     const LocalPath& localdstpath)
{
    auto conceptualname = localfilepath.leafName().toName(*client->fsaccess);

    if (!isgfx(conceptualname))
    {
        return false;
    }

    string image = generateOneImage(conceptualname, localfilepath, dimension);

    if (image.empty())
    {
        return false;
    }

    auto f = client->fsaccess->newfileaccess();
    client->fsaccess->unlinklocal(localdstpath);
    if (!f->fopen(localdstpath, OPEN_WRONLY, FSLogging::logOnError))
    {
        return false;
    }

    if (!f->fwrite((const byte*)image.data(), unsigned(image.size()), 0))
    {
        return false;
    }

    return true;
}

GfxProc::GfxProc(std::unique_ptr<IGfxProvider> middleware)
    : mGfxProvider(std::move(middleware))
{
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

bool isSupportedFormat(SupportedFormatsCallback callback, const std::string& name)
{
    // Can't be supported if we have no name.
    if (name.empty())
        return false;

    // What formats do we support?
    const auto* supported = callback();

    // We don't support any formats.
    if (!supported)
        return false;

    // Extract the name's extension.
    auto extension = extensionOf(name);

    // Can't be supported if we have no extension.
    if (extension.empty())
        return false;

    // We support all formats.
    if (std::strcmp(supported, "all") == 0)
        return true;

    // Check if name's extension is present in supported.
    const auto* position = std::strstr(supported, extension.c_str());

    // Let our caller know if name's extension is present in supported.
    return position && position[extension.size()] == '.';
}

} // namespace
