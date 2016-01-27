/**
* @file MError.cpp
* @brief Error info.
*
* (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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

#include "MError.h"

using namespace mega;
using namespace Platform;

MError::MError(MegaError *megaError, bool cMemoryOwn)
{
    this->megaError = megaError;
    this->cMemoryOwn = cMemoryOwn;
}

MError::~MError()
{
    if (cMemoryOwn)
        delete megaError;
}

MegaError* MError::getCPtr()
{
    return megaError;
}

MError^ MError::copy()
{
    return megaError ? ref new MError(megaError->copy(), true) : nullptr;
}

MErrorType MError::getErrorCode()
{
    return (MErrorType) (megaError ? megaError->getErrorCode() : 0);
}

uint64 MError::getValue()
{
    return megaError ? megaError->getValue() : 0;
}

String^ MError::getErrorString()
{
    std::string utf16error;
    const char *utf8error = megaError->getErrorString();
    MegaApi::utf8ToUtf16(utf8error, &utf16error);

    return ref new String((wchar_t *)utf16error.data());
}

String^ MError::getErrorString(int errorCode)
{
    std::string utf16error;
    const char *utf8error = MegaError::getErrorString(errorCode);
    MegaApi::utf8ToUtf16(utf8error, &utf16error);

    return utf8error ? ref new String((wchar_t *)utf16error.data()) : nullptr;
}

String^ MError::toString()
{
    return getErrorString();
}
