#pragma once

#include "mega/types.h"

#include <curl/curl.h>

#include <memory>

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

    /**
     * @brief Append a list of strings to the internal curl_slist
     * @param items The list of strings to append
     * @return Pointer to the updated curl_slist
     */
    curl_slist* appendCurlList(const std::vector<std::string>& items);

private:
    CURL* mCurl{nullptr}; ///< The underlying libcurl handle
    curl_slist* mChunk{nullptr}; ///< Custom headers or commands list
};

} // namespace sdk_test
