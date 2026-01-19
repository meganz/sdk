#pragma once

#include "mega/types.h"

#include <curl/curl.h>

#include <map>
#include <string>
#include <vector>

namespace sdk_test
{

/**
 * @brief RAII wrapper for libcurl's CURL handle
 *
 * EasyCurl provides a safe, modern C++ interface for managing libcurl's CURL handles.
 * It ensures proper cleanup of resources and follows RAII principles.
 *
 * This class is move-only to prevent accidental copying of CURL handles.
 */
class EasyCurl final
{
public:
    /**
     * @brief Destructor - cleans up the CURL handle
     */
    ~EasyCurl();

    /**
     * @brief constructor - init libcurl's CURL handle
     */
    EasyCurl();

    /**
     * @brief Copy constructor - deleted to prevent copying
     */
    EasyCurl(const EasyCurl&) = delete;

    /**
     * @brief Copy assignment operator - deleted to prevent copying
     */
    EasyCurl& operator=(const EasyCurl&) = delete;

    /**
     * @brief Move constructor
     * @param other The EasyCurl object to move from
     */
    explicit EasyCurl(EasyCurl&& other);

    /**
     * @brief Move assignment operator
     * @param other The EasyCurl object to move from
     * @return Reference to this object
     */
    EasyCurl& operator=(EasyCurl&& other);

    /**
     * @brief Get the underlying CURL handle
     * @return Raw pointer to the CURL handle
     */
    CURL* curl() const;

private:
    CURL* mCurl{nullptr}; ///< The underlying libcurl handle
};

/**
 * @brief RAII wrapper for libcurl's curl_slist handle
 *
 * EasyCurl provides a safe, modern C++ interface for managing libcurl's curl_slist handles.
 * It ensures proper cleanup of resources and follows RAII principles.
 *
 * This class is move-only to prevent accidental copying of curl_slist handles.
 */
class EasyCurlSlist final
{
public:
    /**
     * @brief Constructs an empty EasyCurlSlist
     */
    EasyCurlSlist();

    /**
     * @brief Destructor - cleans up the curl_slist handle
     */
    ~EasyCurlSlist();

    /**
     * @brief Copy constructor - deleted to prevent copying
     */
    EasyCurlSlist(const EasyCurlSlist&) = delete;

    /**
     * @brief Copy assignment operator - deleted to prevent copying
     */
    EasyCurlSlist& operator=(const EasyCurlSlist&) = delete;

    /**
     * @brief Move constructor
     * @param other The EasyCurlSlist to move from
     */
    EasyCurlSlist(EasyCurlSlist&& other) noexcept;

    /**
     * @brief Move assignment operator
     * @param other The EasyCurlSlist to move from
     * @return Reference to this EasyCurlSlist
     */
    EasyCurlSlist& operator=(EasyCurlSlist&& other) noexcept;

    /**
     * @brief Appends multiple headers to the curl_slist
     * @param headers A map of header names and values to append
     * @return true on success, false on failure
     */
    bool appendHttpHeaders(const std::map<std::string, std::string>& headers);

    /**
     * @brief Appends multiple FTP commands to the curl_slist
     * @param commands A vector of FTP command strings to append
     * @return true on success, false on failure
     */
    bool appendFtpCommands(const std::vector<std::string>& commands);

    /**
     * @brief Get the underlying curl_slist handle
     * @return Raw pointer to the curl_slist handle
     */
    curl_slist* slist() const;

private:
    curl_slist* mSlist{nullptr}; ///< The underlying libcurl curl_slist handle
};

} // namespace sdk_test
