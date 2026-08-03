#include <gtest/gtest.h>
#include <mega/node.h>

using namespace mega;

TEST(FileSubType, ClassifiesGifRawAndNone)
{
    // getFileSubType receives an already-lowercased extension (callers lowercase via
    // Node::getExtension); case-folding of e.g. ".GIF" is covered at the name level, not here.
    EXPECT_EQ(Node::getFileSubType("gif"), FILE_SUBTYPE_GIF);
    EXPECT_EQ(Node::getFileSubType("cr2"), FILE_SUBTYPE_RAW);
    EXPECT_EQ(Node::getFileSubType("nef"), FILE_SUBTYPE_RAW);
    EXPECT_EQ(Node::getFileSubType("arw"), FILE_SUBTYPE_RAW);
    EXPECT_EQ(Node::getFileSubType("jpg"), FILE_SUBTYPE_NONE);
    EXPECT_EQ(Node::getFileSubType("png"), FILE_SUBTYPE_NONE);
    EXPECT_EQ(Node::getFileSubType(""), FILE_SUBTYPE_NONE);
}

// Guards the invariant documented on Node::getFileSubType (gif/raw must also be a photo).
TEST(FileSubType, SubCategoryExtensionsAreAlsoPhotos)
{
    for (const char* ext: {"gif", "cr2", "nef", "arw", "dng"})
    {
        SCOPED_TRACE(ext);
        ASSERT_NE(Node::getFileSubType(ext), FILE_SUBTYPE_NONE);
        EXPECT_EQ(Node::getMimetype(ext), MIME_TYPE_PHOTO);
    }
}
