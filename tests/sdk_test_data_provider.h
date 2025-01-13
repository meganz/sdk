/**
 * @file tests/integration/sdk_test_data_provider.h
 * @brief Mega SDK test file
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

#include "stdfs.h"

#include <string>

class SdkTestDataProvider
{
public:
    /**
     * @brief Download a file from the Artifactory
     *
     * @param relativeUrl The relative URL to the base URL
                          "https://artifactory.developers.mega.co.nz:443/artifactory/sdk/"
     * @param dstPath The destination file path to write
     * @return True if the file was downloaded successfully, otherwise false
     */
    bool getFileFromArtifactory(const std::string& relativeUrl, const fs::path& dstPath);

    /**
     * @brief Download a file from a URL using cURL
     *
     * @param url The URL of the File
     * @param dstPath The destination file path to write
     * @return True if the file was downloaded successfully, otherwise false
     */
    bool getFileFromURL(const std::string& url, const fs::path& dstPath);
};
