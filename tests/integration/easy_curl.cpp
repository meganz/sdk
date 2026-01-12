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

} // namespace sdk_test
