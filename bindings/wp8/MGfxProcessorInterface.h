#pragma once

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	public interface class MGfxProcessorInterface
	{
	public:
		bool readBitmap(String^ path);
		int getWidth();
		int getHeight();
		int getBitmapDataSize(int w, int h, int px, int py, int rw, int rh);
		bool getBitmapData(Platform::WriteOnlyArray<unsigned char>^ data);
		void freeBitmap();
	};
}
