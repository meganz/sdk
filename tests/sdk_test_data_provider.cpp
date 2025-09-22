/**
 * @file tests/integration/sdk_test_data_provider.cpp
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
 * You should have received a copy of the license along with this

 * program.
 */

#include "sdk_test_data_provider.h"

#include "mega/logging.h"

#include <curl/curl.h>

#include <fstream>
#include <memory>

namespace
{
// cURL Callback function to write downloaded data to a stream
// See https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
// See https://github.com/curl/curl/pull/9874 returning CURL_WRITEFUNC_ERROR
//     is better than 0 on errors.
size_t writeData(void* ptr, size_t size, size_t nmemb, std::ofstream* stream)
{
    if (stream->write((char*)ptr, static_cast<std::streamsize>(size * nmemb)))
    {
        return size * nmemb;
    }
    else
    {
#ifdef CURL_WRITEFUNC_ERROR
        return CURL_WRITEFUNC_ERROR;
#else
        return 0;
#endif
    }
}

/**
 * @brief Download a file from a URL using cURL
 *
 * @param url The URL of the File
 * @param dstPath The destination file path to write
 * @return True if the file was downloaded successfully, otherwise false
 */
bool getFileFromURL(const std::string& url, const fs::path& dstPath)
{
    auto curlCleaner = [](CURL* curl)
    {
        curl_easy_cleanup(curl);
    };

    // Initialize libcurl
    std::unique_ptr<CURL, decltype(curlCleaner)> curl{curl_easy_init(), curlCleaner};
    if (!curl)
    {
        LOG_err << "Failed to initialize libcurl";
        return false;
    }

    // Open file to save downloaded data
    std::ofstream ofs(dstPath, std::ios::binary | std::ios::out);
    if (!ofs)
    {
        LOG_err << "Error opening file for writing:" << dstPath.u8string();
        return false;
    }

    // Download
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeData);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &ofs);
    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK)
    {
        LOG_err << "curl_easy_perform() failed: " << curl_easy_strerror(res);
        return false;
    }

    // Close file
    ofs.close();
    if (!ofs)
    {
        LOG_verbose << "Error closing file:" << dstPath.u8string();
        return false;
    }

    LOG_verbose << "File " << dstPath.u8string() << " downloaded successfully";
    return true;
}
}

bool getFileFromArtifactory(const std::string& relativeUrl, const fs::path& dstPath)
{
    static const std::string baseUrl{
        "https://artifactory.developers.mega.co.nz:443/artifactory/sdk"};

    // Join base URL and relatvie URL
    bool startedWithBackSlash = !relativeUrl.empty() && relativeUrl[0] == '/';
    std::string seperator = startedWithBackSlash ? "" : "/";
    const auto absoluateUrl = baseUrl + seperator + relativeUrl;

    return getFileFromURL(absoluateUrl, dstPath);
}
