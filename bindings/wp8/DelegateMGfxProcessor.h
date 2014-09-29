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
