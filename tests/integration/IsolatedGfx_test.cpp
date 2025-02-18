#include "SdkTest_test.h"
#include "sdk_test_utils.h"

namespace
{
bool isTransparency(const fs::path& image, FREE_IMAGE_FORMAT fif)
{
    const auto filepath = image.c_str();

    const auto dib = ::mega::makeUniqueFrom(FreeImage_Load(fif, filepath, 0), &FreeImage_Unload);
    if (!dib)
    {
        LOG_err << "Failed to load image";
        return false;
    }

    return FreeImage_IsTransparent(dib.get()) == TRUE;
}

// Get meta data tag count. -1 if there are errors
int getMetadataCount(const fs::path& image, FREE_IMAGE_FORMAT fif)
{
    const auto filepath = image.c_str();

    const auto dib = ::mega::makeUniqueFrom(FreeImage_Load(fif, filepath, 0), &FreeImage_Unload);
    if (!dib)
    {
        LOG_err << "Failed to load image";
        return -1;
    }

    return static_cast<int>(FreeImage_GetMetadataCount(FIMD_EXIF_MAIN, dib.get()));
};

}

class SdkTestIsolatedGfx : public SdkTest
{
protected:
    void SetUp() override;

    void TearDown() override;

    static constexpr const char* SOURCE = "test-data/gfx-processing-crash/SNC-2462__17D1439.tif";

    static constexpr const char* CRASH_IMAGE = "crash.tif";

    static constexpr const char* CRASH_THUMBNAIL = "crash_thumbnail.jpg";

    static constexpr const char* CRASH_PREVIEW = "crash_preview.jpg";

    static constexpr const char* INVALID_IMAGE = "invalid.jpg";

    static constexpr const char* INVALID_THUMBNAIL = "invalid_thumbnail.jpg";

    static constexpr const char* GOOD_IMAGE = "logo.png";

    static constexpr const char* GOOD_THUMBNAIL = "logo_thumbnail.png";

    static constexpr const char* GOOD_PREVIEW   = "logo_preview.png";

    static constexpr const char* TRANSPARENCY_IMAGE = "transparency.png";

    static constexpr const char* TRANSPARENCY_THUMBNAIL = "transparency_thumbnail.png";

    static constexpr const char* TRANSPARENCY_PREVIEW = "transparency_preview.jpg";

    static constexpr const char* ORIENTATION_IMAGE = "orientation.jpg";

    static constexpr const char* ORIENTATION_THUMBNAIL = "orientation_thumbnail.jpg";

    static constexpr const char* ORIENTATION_PREVIEW = "orientation_preview.jpg";
};

void SdkTestIsolatedGfx::SetUp()
{
    SdkTest::SetUp();

    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
}

void SdkTestIsolatedGfx::TearDown()
{
    SdkTest::TearDown();
}

/**
 * @brief GfxProcessingContinueSuccessfullyAfterCrash
 *          1. create thumbnail successfully
 *          2. create thumbnail and preview of a image which causes a gfx process crash.
 *          3. create preview still successfully after the crash
 *          4. create thumbnail of a not valid image expects false.
 *
 * @ Note:
 *          Basically a createThumbnail/createPreview might fail due to the following reason:
 *          
 *          1. The GFX process was already crashed (not running), therefore the error is 
 *             the pipe couldn't be connected
 *          2. The GFX process crashed while processing, therefore the error is others.
 *          
 *          For the 1st case, we'll retry so it is handled. For the 2nd case, we don't retry as
 *          we don't want to retry processing bad images which cause a crash. We have problems 
 *          here because gfxworker process uses multiple thread model.        
 *             When it is processing multiple GFX calls and crashes, we don't know which call is 
 *          processing bad images. So simply all calls are not retried.
 *             When the previous call results in a crash, the following immediate call may still
 *             connect to the pipe as the crash takes time to shutdown the whole process. Therefore
 *             the second call is dropped as well though it should be retried.
 *
 *          It has been discussed and we don't want to deal with these known problem at the moment
 *          as we want to start with simple. It happens rarely and the side effect is limited (thumbnail lost).
 *          We'll improve it when we find it is necessary.
 */
TEST_F(SdkTestIsolatedGfx, GfxProcessingContinueSuccessfullyAfterCrash)
{
    LOG_info << "___TEST GfxProcessingContinueSuccessfullyAfterCrash";

    MegaApi* api = megaApi[0].get();

    // 1. Create a thumbnail successfully
    ASSERT_TRUE(getFileFromArtifactory(std::string{"test-data/"} + GOOD_IMAGE, GOOD_IMAGE));
    ASSERT_TRUE(api->createThumbnail(GOOD_IMAGE, GOOD_THUMBNAIL)) << "create thumbnail should succeed";

    // 2. Create thumbnail and preview of a image which result in a crash
    // the image is selected by testing, thus not guaranteed. we'd either
    // find another media file or need another alternative if it couldn't
    // consistently result in a crash

    // Get the test media file
    fs::path destination{CRASH_IMAGE};
    ASSERT_TRUE(getFileFromArtifactory(SOURCE, destination));
    ASSERT_TRUE(fs::exists(destination));

    // Gfx process would crash due to the bad media file
    ASSERT_FALSE(api->createThumbnail(CRASH_IMAGE, CRASH_THUMBNAIL));
    ASSERT_FALSE(api->createPreview(CRASH_IMAGE, CRASH_PREVIEW));

    // Don't make a call too quickly. Workaround: see note in test case desription
    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    // 3. Create a preview successfully
    ASSERT_TRUE(api->createPreview(GOOD_IMAGE, GOOD_PREVIEW)) << "create preview should succeed";

    // 4. Create thumbnail of a not valid image
    ASSERT_TRUE(getFileFromArtifactory(std::string{"test-data/"} + INVALID_IMAGE, INVALID_IMAGE));
    ASSERT_FALSE(api->createThumbnail(INVALID_IMAGE, INVALID_THUMBNAIL)) << "create invalid image's thumbnail should fail";

    LOG_info << "___TEST GfxProcessingContinueSuccessfullyAfterCrash end___";
}

TEST_F(SdkTestIsolatedGfx, ThumbnailSupportTransparency)
{
    LOG_info << "___TEST ThumbnailSupportTransparency";

    // Download test data
    ASSERT_TRUE(
        getFileFromArtifactory(std::string{"test-data/"} + TRANSPARENCY_IMAGE, TRANSPARENCY_IMAGE));

    // Thumbnail and preview
    MegaApi* api = megaApi[0].get();
    ASSERT_TRUE(api->createThumbnail(TRANSPARENCY_IMAGE, TRANSPARENCY_THUMBNAIL))
        << "create thumbnail should succeed";
    ASSERT_TRUE(api->createPreview(TRANSPARENCY_IMAGE, TRANSPARENCY_PREVIEW))
        << "create preview should succeed";

    // Use this to ensure FreeImage library is initialized for once
    [[maybe_unused]] const auto a =
        ::mega::makeUniqueFrom(MegaGfxProvider::createInternalInstance());

    // Check all are transparency images
    ASSERT_TRUE(isTransparency(fs::path{TRANSPARENCY_IMAGE}, FIF_PNG));
    ASSERT_EQ(FreeImage_GetFileType(TRANSPARENCY_THUMBNAIL, 0), FIF_PNG);
    ASSERT_TRUE(isTransparency(fs::path{TRANSPARENCY_THUMBNAIL}, FIF_PNG));
    ASSERT_EQ(FreeImage_GetFileType(TRANSPARENCY_PREVIEW, 0), FIF_JPEG);
    ASSERT_FALSE(isTransparency(fs::path{TRANSPARENCY_PREVIEW}, FIF_JPEG));

    LOG_info << "___TEST ThumbnailSupportTransparency end___";
}

TEST_F(SdkTestIsolatedGfx, MetaDataIsRemoved)
{
    LOG_info << "___TEST MetaDataIsRemoved";

    // Download test data
    ASSERT_TRUE(
        getFileFromArtifactory(std::string{"test-data/"} + ORIENTATION_IMAGE, ORIENTATION_IMAGE));

    // Thumbnail and preview
    MegaApi* api = megaApi[0].get();
    ASSERT_TRUE(api->createThumbnail(ORIENTATION_IMAGE, ORIENTATION_THUMBNAIL))
        << "create thumbnail should succeed";
    ASSERT_TRUE(api->createPreview(ORIENTATION_IMAGE, ORIENTATION_PREVIEW))
        << "create preview should succeed";

    // Use this to ensure FreeImage library is initialized for once
    [[maybe_unused]] const auto a =
        ::mega::makeUniqueFrom(MegaGfxProvider::createInternalInstance());

    // The original image has more than one EXIF data
    ASSERT_GT(getMetadataCount(fs::path{ORIENTATION_IMAGE}, FIF_JPEG), 1);

    // The thumbnail and preview has none EXIF data
    ASSERT_EQ(getMetadataCount(fs::path{ORIENTATION_PREVIEW}, FIF_JPEG), 0);
    ASSERT_EQ(getMetadataCount(fs::path{ORIENTATION_THUMBNAIL}, FIF_JPEG), 0);

    LOG_info << "___TEST MetaDataIsRemoved end___";
}
