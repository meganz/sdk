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
}

EasyCurl::EasyCurl(EasyCurl&& other):
    mCurl(std::exchange(other.mCurl, nullptr))
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
    }
    return *this;
}

CURL* EasyCurl::curl() const
{
    return mCurl;
}

EasyCurlSlist::EasyCurlSlist():
    mSlist(nullptr)
{}

EasyCurlSlist::~EasyCurlSlist()
{
    if (mSlist)
    {
        curl_slist_free_all(mSlist);
    }
}

EasyCurlSlist::EasyCurlSlist(EasyCurlSlist&& other) noexcept:
    mSlist(std::exchange(other.mSlist, nullptr))
{}

EasyCurlSlist& EasyCurlSlist::operator=(EasyCurlSlist&& other) noexcept
{
    if (this != &other)
    {
        std::swap(mSlist, other.mSlist);
    }
    return *this;
}

bool EasyCurlSlist::append(const std::string& value)
{
    curl_slist* newSlist = curl_slist_append(mSlist, value.c_str());
    if (!newSlist)
    {
        return false;
    }
    mSlist = newSlist;
    return true;
}

curl_slist* EasyCurlSlist::slist() const
{
    return mSlist;
}

} // namespace sdk_test
