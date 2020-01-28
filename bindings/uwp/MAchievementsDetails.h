/**
* @file MAchievementsDetails.h
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

#pragma once

#include "MStringList.h"

#include <megaapi.h>

namespace mega
{
    using namespace Windows::Foundation;
    using Platform::String;

    public enum class MAchievementClass
    {
        MEGA_ACHIEVEMENT_WELCOME            = 1,
        MEGA_ACHIEVEMENT_INVITE             = 3,
        MEGA_ACHIEVEMENT_DESKTOP_INSTALL    = 4,
        MEGA_ACHIEVEMENT_MOBILE_INSTALL     = 5
    };

    public ref class MAchievementsDetails sealed
    {
        friend ref class MRequest;
        friend ref class MStringList;

    public:
        virtual ~MAchievementsDetails();
        int64 getBaseStorage();
        int64 getClassStorage(int class_id);
        int64 getClassTransfer(int class_id);
        int getClassExpire(int class_id);
        unsigned int getAwardsCount();
        int getAwardClass(unsigned int index);
        int getAwardId(unsigned int index);
        int64 getAwardTimestamp(unsigned int index);
        int64 getAwardExpirationTs(unsigned int index);
        MStringList^ getAwardEmails(unsigned int index);
        int getRewardsCount();
        int getRewardAwardId(unsigned int index);
        int64 getRewardStorage(unsigned int index);
        int64 getRewardTransfer(unsigned int index);
        int64 getRewardStorageByAwardId(int award_id);
        int64 getRewardTransferByAwardId(int award_id);
        int getRewardExpire(unsigned int index);

        MAchievementsDetails^ copy();

        int64 currentStorage();
        int64 currentTransfer();
        int64 currentStorageReferrals();
        int64 currentTransferReferrals();

    private:
        MAchievementsDetails(MegaAchievementsDetails *achievementsDetails, bool cMemoryOwn);
        MegaAchievementsDetails *achievementsDetails;
        bool cMemoryOwn;
        MegaAchievementsDetails *getCPtr();
    };
}