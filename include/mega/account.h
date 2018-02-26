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


// subtree's total storage footprint (excluding the root folder itself)
struct MEGA_API NodeStorage
{
    m_off_t bytes;
    uint32_t files;
    uint32_t folders;
    m_off_t version_bytes;
    m_off_t version_files;
};

typedef map<handle, NodeStorage> handlestorage_map;

struct MEGA_API AccountDetails
{
    // subscription information (summarized)
    int pro_level;
    char subscription_type;
    char subscription_cycle[4];
    m_time_t subscription_renew;
    string subscription_method;

    m_time_t pro_until;

    // quota related to the session account
    m_off_t storage_used, storage_max;
    m_off_t transfer_own_used, transfer_srv_used, transfer_max;
    m_off_t transfer_own_reserved, transfer_srv_reserved;
    double srv_ratio;

    // storage used for all relevant nodes (root nodes, incoming shares)
    handlestorage_map storage;

    // transfer history pertaining to requesting IP address
    m_time_t transfer_hist_starttime;     // transfer history start timestamp
    m_time_t transfer_hist_interval;      // timespan that a single transfer
                                        // window record covers
    vector<m_off_t> transfer_hist;      // transfer window - oldest to newest,
                                        // bytes consumed per twrtime interval

    m_off_t transfer_reserved;          // byte quota reserved for the
                                        // completion of active transfers

    m_off_t transfer_limit;             // current byte quota for the
                                        // requesting IP address (dynamic,
                                        // overage will be drawn from account
                                        // quota)

    bool transfer_hist_valid;           // transfer hist valid for overquota
                                        // accounts

    vector<AccountBalance> balances;
    vector<AccountSession> sessions;
    vector<AccountPurchase> purchases;
    vector<AccountTransaction> transactions;
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
} // namespace

#endif
