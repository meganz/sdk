/**
* @file DelegateMGfxProcessor.cpp
* @brief Delegate to get a graphics processor.
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

#include "DelegateMGfxProcessor.h"

using namespace mega;
using namespace Platform;

DelegateMGfxProcessor::DelegateMGfxProcessor(MGfxProcessorInterface^ processor)
{
	this->processor = processor;
}

bool DelegateMGfxProcessor::readBitmap(const char* path)
{
	std::string utf16path;
	if (path)
		MegaApi::utf8ToUtf16(path, &utf16path);

	if (processor != nullptr)
		return processor->readBitmap(path ? ref new String((wchar_t *)utf16path.c_str()) : nullptr);
	
	return false;
}

int DelegateMGfxProcessor::getWidth()
{
	if (processor != nullptr)
		return processor->getWidth();

	return 0;
}

int DelegateMGfxProcessor::getHeight()
{
	if (processor != nullptr)
		return processor->getHeight();

	return 0;
}

int DelegateMGfxProcessor::getBitmapDataSize(int w, int h, int px, int py, int rw, int rh)
{
	if (processor != nullptr)
		return processor->getBitmapDataSize(w, h, px, py, rw, rh);

	return 0;
}

bool DelegateMGfxProcessor::getBitmapData(char *bitmapData, size_t size)
{
	if (processor != nullptr)
		return processor->getBitmapData(::Platform::ArrayReference<unsigned char>((unsigned char *)bitmapData, size));

	return false;
}

void DelegateMGfxProcessor::freeBitmap()
{
	if (processor != nullptr)
		processor->freeBitmap();
}
