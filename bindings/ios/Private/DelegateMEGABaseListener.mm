/**
 * @file DelegateMEGABaseListener.mm
 * @brief Base class for the delegates
 *
 * (c) 2013-present by Mega Limited, Auckland, New Zealand
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
#import "DelegateMEGABaseListener.h"

DelegateMEGABaseListener::DelegateMEGABaseListener(MEGASdk *megaSDK, bool singleListener) {
    this->megaSDK = megaSDK;
    this->singleListener = singleListener;
    this->validListener = true;
}

void DelegateMEGABaseListener::setValidListener(bool validListener) {
    this->validListener = validListener;
}
