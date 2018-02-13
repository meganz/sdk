/**
* @file MError.cpp
* @brief Provides information about an event.
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

#include "MEvent.h"

using namespace mega;
using namespace Platform;

MEvent::MEvent(MegaEvent *megaEvent, bool cMemoryOwn)
{
	this->megaEvent = megaEvent;
	this->cMemoryOwn = cMemoryOwn;
}

MEvent::~MEvent()
{
	if (cMemoryOwn)
		delete megaEvent;
}

MegaEvent* MEvent::getCPtr()
{
	return megaEvent;
}

MEvent^ MEvent::copy()
{
	return megaEvent ? ref new MEvent(megaEvent->copy(), true) : nullptr;
}

MEventType MEvent::getType()
{
	return (MEventType) (megaEvent ? megaEvent->getType() : 0);
}

String^ MEvent::getText()
{
	std::string utf16text;
	const char *utf8text = megaEvent->getText();

	MegaApi::utf8ToUtf16(utf8text, &utf16text);

	return ref new String((wchar_t *)utf16text.data());
}

int MEvent::getNumber()
{
	return megaEvent ? megaEvent->getNumber() : 0;
}
