/**
* @file MInputStreamAdapter.h
* @brief Adapter to use managed input streams on the SDK
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

#include "MInputStreamAdapter.h"

using namespace mega;
using namespace Platform;

MInputStreamAdapter::MInputStreamAdapter(MInputStream^ inputStream)
{
    this->inputStream = inputStream;
}

int64_t MInputStreamAdapter::getSize()
{
    if (inputStream != nullptr)
        return inputStream->Length();

    return 0;
}

bool MInputStreamAdapter::read(char *buffer, size_t size)
{
    if (inputStream == nullptr)
        return false;

    if (!buffer)
        return inputStream->Read(nullptr, size);
	
    return inputStream->Read(::Platform::ArrayReference<unsigned char>((unsigned char *)buffer, size), size);
}