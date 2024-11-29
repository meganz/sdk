/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
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

#include <gtest/gtest.h>

#include <mega/megaclient.h>
#include <mega/megaapp.h>
#include <mega/file.h>

#include "DefaultedFileSystemAccess.h"
#include "utils.h"
#include "mega.h"

namespace
{

//class MockFileSystemAccess : public mt::DefaultedFileSystemAccess
//{
//public:
//    bool unlinklocal(std::string*) override
//    {
//        return true;
//    }
//};

void checkFiles(const mega::File& exp, const mega::File& act)
{
    ASSERT_EQ(exp.name, act.name);
    ASSERT_EQ(exp.getLocalname(), act.getLocalname());
    ASSERT_EQ(exp.h, act.h);
    ASSERT_EQ(exp.hprivate, act.hprivate);
    ASSERT_EQ(exp.hforeign, act.hforeign);
    ASSERT_EQ(exp.syncxfer, act.syncxfer);
    ASSERT_EQ(exp.temporaryfile, act.temporaryfile);
    ASSERT_EQ(exp.privauth, act.privauth);
    ASSERT_EQ(exp.pubauth, act.pubauth);
    ASSERT_EQ(std::string{exp.chatauth}, std::string{act.chatauth});
    ASSERT_TRUE(std::equal(exp.filekey, exp.filekey + mega::FILENODEKEYLENGTH, act.filekey));
    ASSERT_EQ(exp.targetuser, act.targetuser);
    ASSERT_EQ(nullptr, act.transfer);
    if (static_cast<const mega::FileFingerprint&>(exp).isvalid ||
        static_cast<const mega::FileFingerprint&>(act).isvalid)
    {
        ASSERT_EQ(static_cast<const mega::FileFingerprint&>(exp), static_cast<const mega::FileFingerprint&>(act));
    }
}

}

TEST(File, serialize_unserialize)
{
    mega::MegaApp app;
    auto client = mt::makeClient(app);
    mega::File file;
    file.name = "foo";
    file.setLocalname(::mega::LocalPath::fromAbsolutePath(file.name));
    file.h.set6byte(42);
    file.hprivate = true;
    file.hforeign = true;
    file.syncxfer = true;
    file.temporaryfile = true;
    file.privauth = "privauth";
    file.pubauth = "pubauth";
    file.chatauth = new char[4]{'b', 'a', 'r', '\0'}; // owned by file
    std::fill(file.filekey, file.filekey + mega::FILENODEKEYLENGTH, 'X');
    file.targetuser = "targetuser";
    file.transfer = new mega::Transfer{client.get(), mega::GET}; // owned by client
    file.transfer->files.push_back(&file);
    file.file_it = file.transfer->files.begin();

    std::string d;
    file.serialize(&d);

    auto newFile = std::unique_ptr<mega::File>{mega::File::unserialize(&d)};
    checkFiles(file, *newFile);
}


