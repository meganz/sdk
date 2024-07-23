/**
 * @file mega/account.h
 * @brief Classes for manipulating Account data
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

#ifndef MEGA_ACCOUNT_H
#define MEGA_ACCOUNT_H 1

#include "types.h"

namespace mega {
// account details/history
struct MEGA_API AccountBalance
{
    double amount;
    char currency[4];
};

struct MEGA_API AccountSession
{
    m_time_t timestamp, mru;
    string useragent;
    string ip;
    char country[3];
    int current;
    handle id;
    int alive;
    string deviceid;
};

struct MEGA_API AccountPurchase
{
    m_time_t timestamp;
    char handle[12];
    char currency[4];
    double amount;
    int method;
};

struct MEGA_API AccountTransaction
{
    m_time_t timestamp;
    char handle[12];
    char currency[4];
    double delta;
};

struct MEGA_API AccountFeature
{
    m_time_t expiryTimestamp = 0;
    string featureId;
};


// subtree's total storage footprint (excluding the root folder itself)
struct MEGA_API NodeStorage
{
    m_off_t bytes;
    uint32_t files;
    uint32_t folders;
    m_off_t version_bytes;
    uint32_t version_files;
};

typedef map<handle, NodeStorage> handlestorage_map;

struct MEGA_API AccountSubscription
{
    string id; // Encrypted subscription ID
    char type = 0; // 'S' for active payment provider, 'R' otherwise
    string cycle; // Subscription billing period
    string paymentMethod; // Payment provider name
    int32_t paymentMethodId = 0; // Payment provider ID
    m_time_t renew = mega_invalid_timestamp; // Renewal time
    int32_t level = ACCOUNT_TYPE_FREE; // Account level
    vector<string> features; // List of features the subscription grants
};

struct MEGA_API AccountPlan
{
    int32_t level = ACCOUNT_TYPE_FREE; // Account level
    vector<string> features; // List of features the plan grants
    m_time_t expiration = mega_invalid_timestamp; // The time the plan expires
    int32_t type = 0; // Why the plan was granted: payment, achievement, etc. Not included in
                      // Bussiness/Pro Flexi
    string subscriptionId; // The relating subscription ID if the plan relates to a subscription.

    bool isProPlan() const
    {
        return level > ACCOUNT_TYPE_FREE && level != ACCOUNT_TYPE_FEATURE;
    }
};

struct MEGA_API AccountDetails
{
    vector<AccountSubscription> subscriptions;

    // quota related to the session account
    m_off_t storage_used = 0;
    m_off_t storage_max = 0;

    // Own user transfer
    m_off_t transfer_max = 0;
    m_off_t transfer_own_used = 0;
    m_off_t transfer_srv_used = 0;  // 3rd party served quota to other users

    // ratio of your PRO transfer quota that is able to be served to 3rd party
    double srv_ratio = 0;

    // storage used for all relevant nodes (root nodes, incoming shares)
    handlestorage_map storage;

    // Free IP-based transfer quota related:
    m_time_t transfer_hist_starttime = 0;   // transfer history start timestamp
    m_time_t transfer_hist_interval = 3600; // timespan that a single transfer window record covers
    vector<m_off_t> transfer_hist;          // transfer window - oldest to newest, bytes consumed per time interval
    bool transfer_hist_valid = true;        // transfer hist valid for overquota accounts

    // Reserved transfer quota for ongoing transfers (currently ignored by clients)
    m_off_t transfer_reserved = 0;      // free IP-based
    m_off_t transfer_srv_reserved = 0;  // 3rd party
    m_off_t transfer_own_reserved = 0;  // own account

    vector<AccountBalance> balances;
    vector<AccountSession> sessions;
    vector<AccountPurchase> purchases;
    vector<AccountTransaction> transactions;

    // Features
    vector<AccountFeature> activeFeatures;

    // Active plans for the account. Both PRO and feature plans.
    vector<AccountPlan> plans;
};

// award classes with the award values the class is supposed to get
struct MEGA_API Achievement
{
    m_off_t storage;
    m_off_t transfer;
    int expire;    // in days
};

// awarded to the user
struct MEGA_API Award
{
    achievement_class_id achievement_class;
    int award_id;   // not unique, do not use it as key
    m_time_t ts;
    m_time_t expire;    // not compulsory, some awards don't expire
    // int c;  --> always 0, will be removed (obsolete)

    // for invites only
    vector<string> emails_invited;    // successfully invited user's emails
    // int csu;  --> always 0, will be removed (obsolete)
};

// reward the user has achieved and can see
struct MEGA_API Reward
{
    int award_id;
    m_off_t storage;
    m_off_t transfer;
    int expire;    // in days
};

struct MEGA_API AchievementsDetails
{
    m_off_t permanent_size;   // permanent base storage value
    achievements_map achievements; // map<class_id, Achievement>
    vector<Award> awards;
    vector<Reward> rewards;
};

struct MEGA_API BusinessPlan
{
    int gbStoragePerUser = 0;   // -1 means unlimited
    int gbTransferPerUser = 0;   // -1 means unlimited

    unsigned int minUsers = 0;

    unsigned int pricePerUser = 0;
    unsigned int localPricePerUser = 0;

    unsigned int pricePerStorage = 0;
    unsigned int localPricePerStorage = 0;
    int gbPerStorage = 0;

    unsigned int pricePerTransfer = 0;
    unsigned int localPricePerTransfer = 0;
    int gbPerTransfer = 0;
};

struct MEGA_API CurrencyData
{
    std::string currencySymbol;         // ie. €, encoded in B64url
    std::string currencyName;           // ie. EUR

    std::string localCurrencySymbol;    // ie. $, encoded in B64url
    std::string localCurrencyName;      // ie. NZD
};

} // namespace

#endif
