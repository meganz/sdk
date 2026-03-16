/**
 * @brief Mega SDK test file for network commands
 *
 * (c) 2025 by Mega Limited, New Zealand
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

#include "SdkTest_test.h"

#include <gmock/gmock.h>

namespace
{

/**
 * @brief SdkTest.NetworkConnectivityTest
 *
 * Test for MegaApi::runNetworkConnectivityTest(), which should consist of:
 * - get ServerInfo from remote API
 * - send and receive simple UDP messages
 * - send and receive UDP messages for DNS lookup
 * - send event 99495
 */
TEST_F(SdkTest, NetworkConnectivityTest)
{
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));

    RequestTracker tracker(megaApi[0].get());
    megaApi[0]->runNetworkConnectivityTest(&tracker);
    ASSERT_EQ(API_OK, tracker.waitForResult(10))
        << "Network connectivity test took way more than the expected 1 second";
    auto* testResults = tracker.request->getMegaNetworkConnectivityTestResults();
    ASSERT_THAT(testResults, ::testing::NotNull());

    ASSERT_THAT(testResults->getIPv4UDP(),
                ::testing::AnyOf(
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_PASS,
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_NET_UNREACHABLE));

    ASSERT_THAT(testResults->getIPv4DNS(),
                ::testing::AnyOf(
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_PASS,
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_NET_UNREACHABLE));

    ASSERT_THAT(testResults->getIPv6UDP(),
                ::testing::AnyOf(
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_PASS,
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_NET_UNREACHABLE));

    ASSERT_THAT(testResults->getIPv6DNS(),
                ::testing::AnyOf(
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_PASS,
                    MegaNetworkConnectivityTestResults::NETWORK_CONNECTIVITY_TEST_NET_UNREACHABLE));
}

/**
 * @brief SdkTestStorageServerAccessProtocol.testProtocol
 *
 * Test for MegaApi::getUploadURL, MegaApi::getDownloadUrl, MegaApi::getThumbnailUploadURL,
 * MegaApi::getPreviewUploadURL
 * - Return HTTPS address if forceSSL is true, or HTTP address if false
 */
class SdkTestStorageServerAccessProtocol: public ::testing::WithParamInterface<bool>, public SdkTest
{};

TEST_P(SdkTestStorageServerAccessProtocol, testProtocol)
{
    // setup
    ASSERT_NO_FATAL_FAILURE(getAccountsForTest(1));
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + AVATARSRC, AVATARSRC));
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + THUMBNAIL, THUMBNAIL));
    ASSERT_TRUE(getFileFromArtifactory("test-data/" + PREVIEW, PREVIEW));

    const bool forceSSL = GetParam();
    const char* protocolPrefix = forceSSL ? "https://" : "http://";
    std::unique_ptr<MegaNode> rootNode(megaApi[0]->getRootNode());

    // UploadURL
    {
        RequestTracker rt(megaApi[0].get());
        int64_t fileSize = getFilesize(AVATARSRC.c_str());

        // get URL
        ASSERT_GT(fileSize, 0);
        megaApi[0]->getUploadURL(fileSize, forceSSL, &rt);
        ASSERT_EQ(API_OK, rt.waitForResult());

        const char* fileURL = rt.request->getName();
        ASSERT_NE(fileURL, nullptr);
        EXPECT_TRUE(Utils::startswith(fileURL, protocolPrefix))
            << "url " << fileURL << " for file upload is not " << protocolPrefix << " address";
        // getLink() will return a pure ip address
        // getText() will return a pure ipv6 address

        // Create a "media upload" instance
        std::unique_ptr<MegaBackgroundMediaUpload> req(
            MegaBackgroundMediaUpload::createInstance(megaApi[0].get()));

        // encrypt file contents with the file key and get URL suffix
        std::unique_ptr<char[]> suffix(
            req->encryptFile(AVATARSRC.c_str(), 0, &fileSize, AVATARDST.c_str(), false));
        ASSERT_NE(suffix, nullptr) << "Got NULL suffix after encryption";

        std::unique_ptr<char[]> fingerprint(megaApi[0]->getFingerprint(AVATARDST.c_str()));

        string finalurl(fileURL);
        if (suffix)
            finalurl.append(suffix.get());

        // upload file
        string binaryUploadToken;
        synchronousHttpPOSTFile(finalurl, AVATARDST.c_str(), binaryUploadToken);

        ASSERT_NE(binaryUploadToken.size(), 0u);
        ASSERT_GT(binaryUploadToken.size(), 3u)
            << "POST failed, fa server error: " << binaryUploadToken;

        std::unique_ptr<char[]> base64UploadToken(
            megaApi[0]->binaryToBase64(binaryUploadToken.data(), binaryUploadToken.length()));

        int err = synchronousMediaUploadComplete(0,
                                                 req.get(),
                                                 AVATARSRC.c_str(),
                                                 rootNode.get(),
                                                 fingerprint.get(),
                                                 nullptr,
                                                 base64UploadToken.get(),
                                                 nullptr);

        ASSERT_EQ(API_OK, err) << "Cannot complete media upload (error: " << err << ")";
    }

    std::unique_ptr<MegaNode> uploadNode{
        megaApi[0]->getNodeByPath(AVATARSRC.c_str(), rootNode.get())};
    ASSERT_TRUE(uploadNode) << "could not find uploaded file";

    // file download URL
    {
        RequestTracker rt(megaApi[0].get());
        megaApi[0]->getDownloadUrl(uploadNode.get(), true, forceSSL, &rt);
        ASSERT_EQ(API_OK, rt.waitForResult());
        const char* url = rt.request->getName();
        ASSERT_NE(url, nullptr) << "nullptr for file download";
        EXPECT_TRUE(Utils::startswith(url, protocolPrefix))
            << "url " << url << " for file download is not " << protocolPrefix << " address";
    }

    // thumbnail upload URL
    {
        RequestTracker rt(megaApi[0].get());
        int64_t fileSize = getFilesize(THUMBNAIL.c_str());
        ASSERT_GT(fileSize, 0);

        // get URL
        megaApi[0]->getThumbnailUploadURL(uploadNode->getHandle(), fileSize, forceSSL, &rt);
        ASSERT_EQ(API_OK, rt.waitForResult());
        const char* url = rt.request->getName();
        ASSERT_NE(url, nullptr) << "nullptr for thumbnail upload";
        EXPECT_TRUE(Utils::startswith(url, protocolPrefix))
            << "url " << url << " for thumbnail is not " << protocolPrefix << " address";
    }

    // preview upload URL
    {
        RequestTracker rt(megaApi[0].get());
        int64_t fileSize = getFilesize(PREVIEW.c_str());
        ASSERT_GT(fileSize, 0);
        megaApi[0]->getPreviewUploadURL(uploadNode->getHandle(), fileSize, forceSSL, &rt);
        ASSERT_EQ(API_OK, rt.waitForResult());
        const char* url = rt.request->getName();
        ASSERT_NE(url, nullptr) << "nullptr for preview upload";
        EXPECT_TRUE(Utils::startswith(url, protocolPrefix))
            << "url " << url << " for preview is not " << protocolPrefix << " address";
    }
}

INSTANTIATE_TEST_SUITE_P(StorageAccessProtocol,
                         SdkTestStorageServerAccessProtocol,
                         ::testing::Values(false, true));
}
