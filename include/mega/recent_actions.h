/**
 * @file recent_actions.h
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

#pragma once

#include "mega/types.h"

namespace mega
{

class MegaClient;

class RecentActions
{
public:
    explicit RecentActions(MegaClient* client);

    /**
     * @brief Fetch recent action buckets for an account.
     *
     * Queries the node tree for recently changed files, then groups them into action
     * buckets (by owner, parent, time window, media/updated flags), sorts nodes within
     * each bucket and sorts buckets by time.
     *
     * @param maxcount          Maximum number of nodes to consider.
     * @param since             Only consider nodes changed after this timestamp.
     * @param excludeSensitives Exclude nodes marked as sensitive.
     * @return The resulting bucket vector, sorted most-recent-first.
     */
    recentactions_vector getRecentActions(unsigned maxcount,
                                          m_time_t since,
                                          bool excludeSensitives) const;

    /**
     * @brief Retrieve a single recent action bucket by its string identifier.
     *
     * Parses the id, queries the node tree for matching nodes, applies all bucket
     * membership filters and fills @p output with the matching recentaction entry.
     *
     * @param id     The bucket identifier returned by MegaRecentActionBucket::getId().
     * @param output The recentaction struct to fill with the result. Only valid if the return value
     * is API_OK.
     * @return API_OK on success, API_EARGS if id is invalid, API_ENOENT if no nodes match.
     */
    error getById(const char* id, recentaction& output) const;

    /**
     * @brief Retrieve a single recent action bucket by its string identifier, overriding the
     * excludeSensitives flag embedded in the id.
     *
     * Parses the id, queries the node tree for matching nodes using @p excludeSensitives
     * (ignoring the value encoded in the id), applies all bucket membership filters and fills
     * @p output with the matching recentaction entry.
     *
     * @param id               The bucket identifier returned by MegaRecentActionBucket::getId().
     * @param excludeSensitives If true, sensitive nodes are excluded regardless of the flag in
     * the id. If false, sensitive nodes are included.
     * @param output           The recentaction struct to fill with the result. Only valid if the
     * return value is API_OK.
     * @return API_OK on success, API_EARGS if id is invalid, API_ENOENT if no nodes match.
     */
    error getById(const char* id, bool excludeSensitives, recentaction& output) const;

private:
    /**
     * @brief Build a recentactions_vector from a pre-fetched node vector.
     */
    recentactions_vector buildFromNodes(sharedNode_vector&& v, bool excludeSensitives) const;

    /**
     * @brief Fetch candidate nodes for a recent action bucket.
     */
    sharedNode_vector getBucketCandidates(m_time_t startTime,
                                          m_time_t endTime,
                                          handle parent,
                                          bool isMedia,
                                          bool excludeSensitives) const;

    /**
     * @brief Sort bucket nodes by creation time descending.
     */
    void sortBucketNodes(sharedNode_vector& nodes) const;

    /**
     * @brief Core implementation shared by both public getById overloads.
     *
     * Parses @p id, then uses @p excludeSensitives to filter candidate nodes.
     * If @p excludeSensitives is std::nullopt the value encoded in the id is used;
     * otherwise the provided value overrides it.
     *
     * @param id                Original bucket identifier string.
     * @param excludeSensitives Override for the sensitivity filter, or std::nullopt to use the
     *                          value embedded in @p id.
     * @param output            Filled on API_OK.
     */
    error getById(const char* id,
                  std::optional<bool> excludeSensitives,
                  recentaction& output) const;

    /**
     * @brief Filter candidate nodes to those that match the given bucket metadata and time window.
     *
     * Retains only nodes whose owner, ctime range, updated flag and media flag all match @p meta.
     *
     * @param candidates  Nodes pre-fetched from the node tree.
     * @param meta        Bucket identity (user, parent, media, updated).
     * @param startTime   Inclusive lower bound of the time window (epoch seconds).
     * @param endTime     Exclusive upper bound of the time window (epoch seconds).
     * @return Nodes that belong to the bucket.
     */
    sharedNode_vector filterCandidatesByMeta(const sharedNode_vector& candidates,
                                             const RecentActionBucketMeta& meta,
                                             m_time_t startTime,
                                             m_time_t endTime) const;

    MegaClient* mClient;
};

} // namespace mega
