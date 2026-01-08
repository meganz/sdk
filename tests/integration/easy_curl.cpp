#include "easy_curl.h"

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
{}

EasyCurl& EasyCurl::operator=(EasyCurl&& other)
{
    if (this != &other)
    {
        mCurl = std::exchange(other.mCurl, nullptr);
    }
    return *this;
}

std::unique_ptr<EasyCurl> EasyCurl::create()
{
    return std::unique_ptr<EasyCurl>(new EasyCurl());
}

CURL* EasyCurl::curl() const
{
    return mCurl;
}

} // namespace sdk_test
