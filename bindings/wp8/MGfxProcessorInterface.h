/**
* @file MGfxProcessorInterface.h
* @brief Interface to provide an external GFX processor.
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
