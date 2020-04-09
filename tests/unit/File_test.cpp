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
    ASSERT_EQ(exp.localname, act.localname);
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
    ASSERT_EQ(static_cast<const mega::FileFingerprint&>(exp), static_cast<const mega::FileFingerprint&>(act));
}

}

TEST(File, serialize_unserialize)
{
    mega::MegaApp app;
    ::mega::FSACCESS_CLASS fsaccess;
    auto client = mt::makeClient(app, fsaccess);
    mega::File file;
    file.name = "foo";
    file.localname = ::mega::LocalPath::fromPath(file.name, fsaccess);
    file.h = 42;
    file.hprivate = true;
    file.hforeign = true;
    file.syncxfer = true;
    file.temporaryfile = true;
    file.privauth = "privauth";
    file.pubauth = "pubauth";
    file.chatauth = new char[4]{'b', 'a', 'r', '\0'}; // owned by file
    std::fill(file.filekey, file.filekey + mega::FILENODEKEYLENGTH, 'X');
    file.targetuser = "targetuser";
    file.transfer = new mega::Transfer{client.get(), mega::NONE}; // owned by client
    file.transfer->files.push_back(&file);
    file.file_it = file.transfer->files.begin();

    std::string d;
    file.serialize(&d);

    auto newFile = std::unique_ptr<mega::File>{mega::File::unserialize(&d)};
    checkFiles(file, *newFile);
}

#ifndef WIN32   // data was recorded with "mock" utf-8 not the actual utf-16
TEST(File, unserialize_32bit)
{
    mega::MegaApp app;
    ::mega::FSACCESS_CLASS fsaccess;
    auto client = mt::makeClient(app, fsaccess);
    mega::File file;
    file.name = "foo";
    file.localname = ::mega::LocalPath::fromPath(file.name, fsaccess);
    file.h = 42;
    file.hprivate = true;
    file.hforeign = true;
    file.syncxfer = true;
    file.temporaryfile = true;
    file.privauth = "privauth";
    file.pubauth = "pubauth";
    file.chatauth = new char[4]{'b', 'a', 'r', '\0'}; // owned by file
    std::fill(file.filekey, file.filekey + mega::FILENODEKEYLENGTH, 'X');
    file.targetuser = "targetuser";
    file.transfer = new mega::Transfer{client.get(), mega::NONE}; // owned by client
    file.transfer->files.push_back(&file);
    file.file_it = file.transfer->files.begin();

    // This is the result of serialization on 32bit Windows
    const std::array<unsigned char, 133> rawData = {
        0x03, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 
        0xff, 0xff, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
        0x66, 0x6f, 0x6f, 0x03, 0x00, 0x66, 0x6f, 0x6f, 0x0a, 0x00, 0x74, 0x61,
        0x72, 0x67, 0x65, 0x74, 0x75, 0x73, 0x65, 0x72, 0x08, 0x00, 0x70, 0x72,
        0x69, 0x76, 0x61, 0x75, 0x74, 0x68, 0x07, 0x00, 0x70, 0x75, 0x62, 0x61,
        0x75, 0x74, 0x68, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58,
        0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58,
        0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58,
        0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x62, 0x61,
        0x72
    };
    std::string d(reinterpret_cast<const char*>(rawData.data()), rawData.size());

    auto newFile = std::unique_ptr<mega::File>{mega::File::unserialize(&d)};
    checkFiles(file, *newFile);
}
#endif
