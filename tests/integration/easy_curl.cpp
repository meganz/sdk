#include "easy_curl.h"

#include <stdexcept>
#include <utility>

namespace sdk_test
{

EasyCurl::~EasyCurl()
{
    if (mCurl)
    {
        curl_easy_cleanup(mCurl);
    }

    if (mChunk)
    {
        curl_slist_free_all(mChunk);
    }
}

EasyCurl::EasyCurl(EasyCurl&& other):
    mCurl(std::exchange(other.mCurl, nullptr)),
    mChunk(std::exchange(other.mChunk, nullptr))
{}

EasyCurl::EasyCurl():
    mCurl(curl_easy_init())
{
    if (!mCurl)
        throw std::runtime_error("curl_easy_init returns null");
}

EasyCurl& EasyCurl::operator=(EasyCurl&& other)
{
    if (this != &other)
    {
        using std::swap;
        swap(other.mCurl, mCurl);
        swap(other.mChunk, mChunk);
    }
    return *this;
}

CURL* EasyCurl::curl() const
{
    return mCurl;
}

curl_slist* EasyCurl::appendCurlList(const std::vector<std::string>& items)
{
    if (items.empty())
    {
        return mChunk;
    }
    for (const auto& item: items)
    {
        mChunk = curl_slist_append(mChunk, item.c_str());
    }
    return mChunk;
}

}

// namespace sdk_test
