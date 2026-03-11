/**
 * @file recent_actions.cpp
 * @brief Business logic for getRecentActions and getRecentActionById
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
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

#include "mega/recent_actions.h"

#include "mega.h"
#include "mega/megaclient.h"

#include <algorithm>
#include <vector>

namespace mega
{

namespace
{
constexpr m_time_t kSecondsPerHour = 3600;
constexpr m_time_t kSecondsPerDay = 24 * kSecondsPerHour;
constexpr int kWindowSizeHours = 6;

enum class BucketIdIndex
{
    DayStart = 0,
    WindowStart = 1,
    WindowEnd = 2,
    User = 3,
    Parent = 4,
    Media = 5,
    Updated = 6,
    ExcludeSensitives = 7
};
// Matches MegaApi::ORDER_CREATION_DESC. Defined locally to avoid depending on megaapi.h.
constexpr int kOrderCreationDesc = 6;

static bool nodes_ctime_greater(const Node* a, const Node* b)
{
    return a->ctime > b->ctime;
}

static bool nodes_ctime_greater_shared_ptr(const std::shared_ptr<Node>& a,
                                           const std::shared_ptr<Node>& b)
{
    return nodes_ctime_greater(a.get(), b.get());
}

namespace action_bucket_compare
{
static bool comparetime(const recentaction& a, const recentaction& b)
{
    return a.time > b.time;
}
} // end namespace action_bucket_compare

static m_time_t recentActionDayStartUtc(m_time_t ts)
{
    return (ts / kSecondsPerDay) * kSecondsPerDay;
}

static int recentActionWindowStartHourUtc(m_time_t ts, int windowSizeHours = kWindowSizeHours)
{
    if (windowSizeHours <= 0)
    {
        return 0;
    }

    const m_time_t secondsSinceDayStart = ts - recentActionDayStartUtc(ts);
    const int hourOfDay = static_cast<int>(secondsSinceDayStart / kSecondsPerHour);

    return (hourOfDay / windowSizeHours) * windowSizeHours;
}

static std::string buildRecentActionId(m_time_t ctime,
                                       handle userHandle,
                                       handle parent,
                                       bool media,
                                       bool update,
                                       bool excludeSensitives)
{
    const m_time_t dayStart = recentActionDayStartUtc(ctime);
    const int startWindow = recentActionWindowStartHourUtc(ctime);
    const int endWindow = startWindow + kWindowSizeHours;

    std::string parentStr = "UNDEF";
    if (!ISUNDEF(parent))
    {
        parentStr = Base64Str<MegaClient::NODEHANDLE>(parent);
    }

    std::string userHandleStr = "UNDEF";
    if (!ISUNDEF(userHandle))
    {
        userHandleStr = Base64Str<MegaClient::USERHANDLE>(userHandle);
    }

    std::string id;
    id.reserve(64);
    id.append(std::to_string(dayStart));
    id.append("|");
    id.append(std::to_string(startWindow));
    id.append("|");
    id.append(std::to_string(endWindow));
    id.append("|");
    id.append(userHandleStr);
    id.append("|");
    id.append(parentStr);
    id.append("|");
    id.append(media ? "1" : "0");
    id.append("|");
    id.append(update ? "1" : "0");
    id.append("|");
    id.append(excludeSensitives ? "1" : "0");
    return id;
}

static bool parseRecentActionBool(const std::string& token, bool& value)
{
    if (token == "1")
    {
        value = true;
        return true;
    }
    if (token == "0")
    {
        value = false;
        return true;
    }
    return false;
}

static bool parseRecentActionWindow(const std::string& token,
                                    int& window,
                                    int windowSizeHours = kWindowSizeHours)
{
    if (token.empty() || token.size() > 2)
    {
        return false;
    }

    if (token[0] == '-')
    {
        return false;
    }

    int temp = 0;
    try
    {
        temp = std::stoi(token);
    }
    catch (...)
    {
        return false;
    }

    if (temp < 0 || temp > 24 || (windowSizeHours > 0 && temp % windowSizeHours != 0))
    {
        return false;
    }

    window = temp;
    return true;
}

static bool parseRecentActionDate(const std::string& token, m_time_t& dayStart)
{
    if (token.empty() || token.size() > 10)
    {
        return false;
    }

    for (const char c: token)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
    }

    try
    {
        dayStart = static_cast<m_time_t>(std::stoll(token));
    }
    catch (...)
    {
        return false;
    }

    return dayStart > 0;
}

static bool parseRecentActionParentHandle(const std::string& token, handle& parent)
{
    if (token == "UNDEF")
    {
        return false;
    }

    handle parsedParent = 0;
    const int decoded =
        Base64::atob(token.c_str(), reinterpret_cast<byte*>(&parsedParent), MegaClient::NODEHANDLE);
    if (decoded != MegaClient::NODEHANDLE || ISUNDEF(parsedParent))
    {
        return false;
    }

    parent = parsedParent;
    return true;
}

static bool parseRecentActionUserHandle(const std::string& token, handle& userHandle)
{
    if (token == "UNDEF")
    {
        return false;
    }

    handle parsedUserHandle = 0;
    const int decoded = Base64::atob(token.c_str(),
                                     reinterpret_cast<byte*>(&parsedUserHandle),
                                     MegaClient::USERHANDLE);
    if (decoded != MegaClient::USERHANDLE || ISUNDEF(parsedUserHandle))
    {
        return false;
    }

    userHandle = parsedUserHandle;
    return true;
}

static bool parseRecentActionId(const std::string& id,
                                m_time_t& dayStart,
                                int& windowStart,
                                int& windowEnd,
                                RecentActionBucketMeta& meta,
                                bool& excludeSensitives)
{
    dayStart = 0;
    meta = RecentActionBucketMeta{};

    std::vector<std::string> tokens = splitString<std::vector<std::string>>(id, '|');

    if (tokens.size() != 8)
    {
        return false;
    }

    if (!parseRecentActionDate(tokens[static_cast<size_t>(BucketIdIndex::DayStart)], dayStart))
    {
        return false;
    }

    if (!parseRecentActionWindow(tokens[static_cast<size_t>(BucketIdIndex::WindowStart)],
                                 windowStart))
    {
        return false;
    }

    if (!parseRecentActionWindow(tokens[static_cast<size_t>(BucketIdIndex::WindowEnd)], windowEnd))
    {
        return false;
    }

    if (windowEnd <= windowStart || windowEnd > windowStart + kWindowSizeHours)
    {
        return false;
    }

    if (!parseRecentActionUserHandle(tokens[static_cast<size_t>(BucketIdIndex::User)], meta.user))
    {
        return false;
    }

    if (!parseRecentActionParentHandle(tokens[static_cast<size_t>(BucketIdIndex::Parent)],
                                       meta.parent))
    {
        return false;
    }

    if (!parseRecentActionBool(tokens[static_cast<size_t>(BucketIdIndex::Media)], meta.media))
    {
        return false;
    }

    if (!parseRecentActionBool(tokens[static_cast<size_t>(BucketIdIndex::Updated)], meta.updated))
    {
        return false;
    }

    if (!parseRecentActionBool(tokens[static_cast<size_t>(BucketIdIndex::ExcludeSensitives)],
                               excludeSensitives))
    {
        return false;
    }

    return true;
}

} // anonymous namespace

RecentActions::RecentActions(MegaClient* client):
    mClient(client)
{}

recentactions_vector RecentActions::getRecentActions(unsigned maxcount,
                                                     m_time_t since,
                                                     bool excludeSensitives) const
{
    sharedNode_vector v = mClient->mNodeManager.getRecentNodes(maxcount, since, excludeSensitives);
    return buildFromNodes(std::move(v), excludeSensitives);
}

recentactions_vector RecentActions::buildFromNodes(sharedNode_vector&& v,
                                                   bool excludeSensitives) const
{
    recentactions_vector rav;
    std::unordered_map<std::string, size_t> bucketIndex;

    for (const auto& node: v)
    {
        if (!node)
        {
            continue;
        }

        const handle parentHandle = node->parent ? node->parent->nodehandle : UNDEF;
        const bool updated = mClient->getNumberOfChildren(node->nodeHandle()) > 0;
        const bool media = mClient->nodeIsMedia(node.get(), nullptr, nullptr);

        const std::string key = buildRecentActionId(node->ctime,
                                                    node->owner,
                                                    parentHandle,
                                                    media,
                                                    updated,
                                                    excludeSensitives);

        auto it = bucketIndex.find(key);
        if (it == bucketIndex.end())
        {
            const User* user = mClient->finduser(node->owner, 0);
            const std::string email = user ? user->email : std::string();

            recentaction ra;
            ra.time = node->ctime;
            ra.meta.user = node->owner;
            ra.meta.userEmail = email;
            ra.meta.parent = parentHandle;
            ra.meta.updated = updated;
            ra.meta.media = media;
            ra.id = key;
            rav.push_back(std::move(ra));
            it = bucketIndex.emplace(std::move(key), rav.size() - 1).first;
        }

        rav[it->second].nodes.push_back(node);
    }
    // sort nodes inside each bucket
    for (recentactions_vector::iterator i = rav.begin(); i != rav.end(); ++i)
    {
        // for the bucket vector, most recent (larger ctime) first
        sortBucketNodes(i->nodes);
        i->time = i->nodes.front()->ctime;
    }
    // sort buckets in the vector
    std::sort(rav.begin(), rav.end(), action_bucket_compare::comparetime);
    return rav;
}

sharedNode_vector RecentActions::getBucketCandidates(m_time_t startTime,
                                                     m_time_t endTime,
                                                     handle parent,
                                                     bool isMedia,
                                                     bool excludeSensitives) const
{
    NodeSearchFilter filter;
    filter.byNodeType(FILENODE);
    filter.byCreationTimeLowerLimitInSecs(startTime - 1);
    filter.byCreationTimeUpperLimitInSecs(endTime);
    if (isMedia)
    {
        filter.byCategory(MIME_TYPE_ALL_VISUAL_MEDIA);
    }
    filter.byLocationHandle(parent);
    filter.bySensitivity(excludeSensitives ? NodeSearchFilter::BoolFilter::onlyTrue :
                                             NodeSearchFilter::BoolFilter::disabled);

    return mClient->mNodeManager.getChildren(filter,
                                             kOrderCreationDesc,
                                             CancelToken(),
                                             NodeSearchPage{0, 0});
}

void RecentActions::sortBucketNodes(sharedNode_vector& nodes) const
{
    std::sort(nodes.begin(), nodes.end(), nodes_ctime_greater_shared_ptr);
}

sharedNode_vector RecentActions::filterCandidatesByMeta(const sharedNode_vector& candidates,
                                                        const RecentActionBucketMeta& meta,
                                                        m_time_t startTime,
                                                        m_time_t endTime) const
{
    sharedNode_vector bucketNodes;
    for (const auto& node: candidates)
    {
        if (!node)
        {
            continue;
        }
        if (node->owner != meta.user)
        {
            continue;
        }
        // byLocationHandle already restricts the DB query to meta.parent; this
        // guards against any in-memory inconsistency.
        assert(!node->parent || node->parent->nodehandle == meta.parent);
        if (node->ctime < startTime || node->ctime >= endTime)
        {
            continue;
        }

        const bool updated = mClient->getNumberOfChildren(node->nodeHandle()) > 0;
        if (updated != meta.updated)
        {
            continue;
        }

        const bool media = mClient->nodeIsMedia(node.get(), nullptr, nullptr);
        if (media != meta.media)
        {
            continue;
        }

        bucketNodes.push_back(node);
    }
    return bucketNodes;
}

error RecentActions::getById(const char* id, recentaction& ra) const
{
    if (!id || !*id)
    {
        return API_EARGS;
    }

    m_time_t dayStart = 0;
    int windowStart = 0;
    int windowEnd = 0;
    bool excludeSensitives = false;
    RecentActionBucketMeta meta;
    if (!parseRecentActionId(id, dayStart, windowStart, windowEnd, meta, excludeSensitives))
    {
        return API_EARGS;
    }

    const m_time_t startTime = dayStart + windowStart * kSecondsPerHour;
    const m_time_t endTime = dayStart + windowEnd * kSecondsPerHour;

    sharedNode_vector candidates =
        getBucketCandidates(startTime, endTime, meta.parent, meta.media, excludeSensitives);

    sharedNode_vector bucketNodes = filterCandidatesByMeta(candidates, meta, startTime, endTime);

    if (bucketNodes.empty())
    {
        return API_ENOENT;
    }

    sortBucketNodes(bucketNodes);

    User* user =
        mClient->finduser(meta.user,
                          0); // to ensure that user email is available in cache for the result
    meta.userEmail = user ? user->email : std::string();

    ra.time = bucketNodes.front()->ctime;
    ra.meta = std::move(meta);
    ra.id = id;
    ra.nodes = std::move(bucketNodes);

    return API_OK;
}

} // namespace mega
