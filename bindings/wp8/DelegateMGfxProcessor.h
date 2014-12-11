/**
* @file DelegateMGfxProcessor.h
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

#pragma once

#include "MGfxProcessorInterface.h"

#include "megaapi.h"

namespace mega
{
	private class DelegateMGfxProcessor : public MegaGfxProcessor
	{
	public:
		DelegateMGfxProcessor(MGfxProcessorInterface^ processor);
		virtual bool readBitmap(const char* path);
		virtual int getWidth();
		virtual int getHeight();
		virtual int getBitmapDataSize(int w, int h, int px, int py, int rw, int rh);
		virtual bool getBitmapData(char *bitmapData, size_t size);
		virtual void freeBitmap();
	
	private:
		MGfxProcessorInterface^ processor;
	};
}
