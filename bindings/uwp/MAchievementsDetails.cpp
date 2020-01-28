/**
* @file MAchievementsDetails.cpp
* @brief Get Achievements that a user can unlock.
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

#include "MAchievementsDetails.h"

using namespace mega;
using namespace Platform;

MAchievementsDetails::MAchievementsDetails(MegaAchievementsDetails *achievementsDetails, bool cMemoryOwn)
{
    this->achievementsDetails = achievementsDetails;
    this->cMemoryOwn = cMemoryOwn;
}

MAchievementsDetails::~MAchievementsDetails()
{
    if (cMemoryOwn)
        delete achievementsDetails;
}

MegaAchievementsDetails* MAchievementsDetails::getCPtr()
{
    return achievementsDetails;
}

int64 MAchievementsDetails::getBaseStorage()
{
    return achievementsDetails ? achievementsDetails->getBaseStorage() : -1;
}

int64 MAchievementsDetails::getClassStorage(int class_id)
{
    return achievementsDetails ? achievementsDetails->getClassStorage(class_id) : -1;
}

int64 MAchievementsDetails::getClassTransfer(int class_id)
{
    return achievementsDetails ? achievementsDetails->getClassTransfer(class_id) : -1;
}

int MAchievementsDetails::getClassExpire(int class_id)
{
    return achievementsDetails ? achievementsDetails->getClassExpire(class_id) : 0;
}

unsigned int MAchievementsDetails::getAwardsCount()
{
    return achievementsDetails ? achievementsDetails->getAwardsCount() : 0;
}

int MAchievementsDetails::getAwardClass(unsigned int index)
{
    return achievementsDetails ? achievementsDetails->getAwardClass(index) : 0;
}

int MAchievementsDetails::getAwardId(unsigned int index)
{
    return achievementsDetails ? achievementsDetails->getAwardId(index) : 0;
}

int64 MAchievementsDetails::getAwardTimestamp(unsigned int index)
{
    return achievementsDetails ? achievementsDetails->getAwardTimestamp(index) : -1;
}

int64 MAchievementsDetails::getAwardExpirationTs(unsigned int index)
{
    return achievementsDetails ? achievementsDetails->getAwardExpirationTs(index) : -1;
}

MStringList^ MAchievementsDetails::getAwardEmails(unsigned int index)
{
    return achievementsDetails ? ref new MStringList(achievementsDetails->getAwardEmails(index), true) : nullptr;
}

int MAchievementsDetails::getRewardsCount()
{
    return achievementsDetails ? achievementsDetails->getRewardsCount() : -1;
}

int MAchievementsDetails::getRewardAwardId(unsigned int index)
{
    return achievementsDetails ? achievementsDetails->getRewardAwardId(index) : -1;
}

int64 MAchievementsDetails::getRewardStorage(unsigned int index)
{
    return achievementsDetails ? achievementsDetails->getRewardStorage(index) : -1;
}

int64 MAchievementsDetails::getRewardTransfer(unsigned int index)
{
    return achievementsDetails ? achievementsDetails->getRewardTransfer(index) : -1;
}

int64 MAchievementsDetails::getRewardStorageByAwardId(int award_id)
{
    return achievementsDetails ? achievementsDetails->getRewardStorageByAwardId(award_id) : -1;
}

int64 MAchievementsDetails::getRewardTransferByAwardId(int award_id)
{
    return achievementsDetails ? achievementsDetails->getRewardTransferByAwardId(award_id) : -1;
}

int MAchievementsDetails::getRewardExpire(unsigned int index)
{
    return achievementsDetails ? achievementsDetails->getRewardExpire(index) : 0;
}

MAchievementsDetails^ MAchievementsDetails::copy()
{
    return achievementsDetails ? ref new MAchievementsDetails(achievementsDetails->copy(), true) : nullptr;
}

int64 MAchievementsDetails::currentStorage()
{
    return achievementsDetails ? achievementsDetails->currentStorage() : -1;
}

int64 MAchievementsDetails::currentTransfer()
{
    return achievementsDetails ? achievementsDetails->currentTransfer() : -1;
}

int64 MAchievementsDetails::currentStorageReferrals()
{
    return achievementsDetails ? achievementsDetails->currentStorageReferrals() : -1;
}

int64 MAchievementsDetails::currentTransferReferrals()
{
    return achievementsDetails ? achievementsDetails->currentTransferReferrals() : -1;
}
