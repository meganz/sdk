/**
 * @file gfx.h
 * @brief Bitmap graphics processing
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

#ifndef GFX_H
#define GFX_H 1

#include <mutex>

#include "mega/types.h"
#include "mega/filesystem.h"
#ifdef USE_IOS
#include "mega/posix/megawaiter.h"
#else
#include "megawaiter.h"
#endif
#include "mega/thread/posixthread.h"
#include "mega/thread/cppthread.h"

namespace mega {

class MEGA_API GfxJob
{
public:
    GfxJob();

    // locally encoded path of the image
    LocalPath localfilename;

    // vector with the required image type
    vector<fatype> imagetypes;

    // handle related to the image
    NodeOrUploadHandle h;

    // key related to the image
    byte key[SymmCipher::KEYLENGTH];

    // resulting images
    vector<string *> images;
};

class MEGA_API GfxJobQueue
{
    protected:
        std::deque<GfxJob *> jobs;
        std::mutex mutex;

    public:
        GfxJobQueue();
        void push(GfxJob *job);
        GfxJob *pop();
};

class MEGA_API GfxDimension
{
public:
    GfxDimension() = default;

    GfxDimension(int w, int h) : mWidth(w), mHeight(h) {}

    bool operator==(const GfxDimension& other) const { return mWidth == other.mWidth && mHeight == other.mHeight; }

    bool operator!=(const GfxDimension& other) const { return !(*this == other); }

    int w() const { return mWidth; }

    int h() const { return mHeight; }

    void setW(const int width) { mWidth = width; }

    void setH(const int height) { mHeight = height; }
private:
    int mWidth = 0;

    int mHeight = 0;
};

// Interface for graphic processor provider used by GfxProc
class MEGA_API IGfxProvider
{
public:
    virtual ~IGfxProvider() = default;

    // It generates thumbnails for the file at localfilepath. The function will return
    // the same number of thumbnails as the size of dimensions vector. On error it will
    // return a vector of empty strings.
    virtual std::vector<std::string> generateImages(const LocalPath& localfilepath,
                                                    const std::vector<GfxDimension>& dimensions) = 0;

    // list of supported extensions (NULL if no pre-filtering is needed)
    virtual const char* supportedformats() = 0;

    // list of supported video extensions (NULL if no pre-filtering is needed)
    virtual const char* supportedvideoformats() = 0;

    static std::unique_ptr<IGfxProvider> createInternalGfxProvider();
};

// Interface for the local graphic processor provider
// Implementations should be able to allocate/deallocate and manipulate bitmaps,
// No thread safety is required among the operations
class MEGA_API IGfxLocalProvider : public IGfxProvider
{
public: // read and store bitmap
    virtual ~IGfxLocalProvider() = default;

    virtual std::vector<std::string> generateImages(const LocalPath& localfilepath,
                                                    const std::vector<GfxDimension>& dimensions) override;

    enum class Hint
    {
        NONE = 0,
        FORMAT_PNG = 1, // Format can be in PNG
    };

private:
    virtual bool readbitmap(const LocalPath&, int) = 0;

    // Resize stored bitmap and store result as JPEG by default or PNG if the bitmap has
    // transparency and the requested width and height is for the thumbnail.
    //
    // @param rw The requested width
    // @param rh The requested height
    // @param result The pointer to string where the resized bitmap is stored
    // @param hint Gives the hint about behaviour.
    virtual bool resizebitmap(int rw, int rh, string* result, Hint hint) = 0;

    // free stored bitmap
    virtual void freebitmap() = 0;

    int width() { return w; }
    int height() { return h; }

protected:
    // coordinate transformation
    static void transform(int&, int&, int&, int&, int&, int&);

    int w, h;
};

// bitmap graphics processor
class MEGA_API GfxProc
{
    std::atomic<bool> finished{false};
    WAIT_CLASS waiter;
    std::mutex mutex;
    THREAD_CLASS thread;
    bool threadstarted = false;
    SymmCipher mCheckEventsKey;
    GfxJobQueue requests;
    GfxJobQueue responses;
    std::unique_ptr<IGfxProvider>  mGfxProvider;

    static void *threadEntryPoint(void *param);
    void loop();

    std::vector<GfxDimension> getJobDimensions(GfxJob *job);

    // Caller should give dimensions from high resolution to low resolution
    std::vector<std::string> generateImages(const LocalPath& localfilepath, const std::vector<GfxDimension>& dimensions);

    std::string generateOneImage(const LocalPath& localfilepath, const GfxDimension& dimension);

public:
    // synchronously processes the results of gendimensionsputfa() (if any) in a thread safe manner
    int checkevents(Waiter*);

    // synchronously check whether the filename looks like a supported media type
    bool isgfx(const LocalPath&);

    // synchronously check whether the filename looks like a video
    bool isvideo(const LocalPath&);

    // synchronously generate all gfx sizes and returns the count
    // asynchronously write to metadata server and attach to PUT transfer or existing node,
    // upon finalization the job is stored in responses object in a thread safe manner, and client waiter is notified
    // The results can be processed by calling checkevents()
    // handle is uploadhandle or nodehandle
    // - must respect JPEG EXIF rotation tag
    // - must save at 85% quality (120*120 pixel result: ~4 KB)
    int gendimensionsputfa(const LocalPath&, NodeOrUploadHandle, SymmCipher*, int missingattr);

    // FIXME: read dynamically from API server
    typedef enum { THUMBNAIL, PREVIEW } meta_t;
    typedef enum { AVATAR250X250 } avatar_t;

    // synchronously generate and save a fa to a file
    bool savefa(const LocalPath& source,
                const GfxDimension& dimension,
                const LocalPath& destination);

    // - w*0: largest square crop at the center (landscape) or at 1/6 of the height above center (portrait)
    // - w*h: resize to fit inside w*h bounding box
    static const std::vector<GfxDimension> DIMENSIONS;
    static const std::vector<GfxDimension> DIMENSIONS_AVATAR;

    MegaClient* client = nullptr;

    // start a thread that will do the processing
    void startProcessingThread();

    // The provided IGfxProvider implements library specific image processing
    // Thread safety among IGfxProvider methods is guaranteed by GfxProc
    GfxProc(std::unique_ptr<IGfxProvider>);
    virtual ~GfxProc();
};
} // namespace

#endif
