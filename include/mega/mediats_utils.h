/**
 * @file mediats_utils.h
 * @brief Utilities for computing media capture timestamps (mediats).
 *
 * (c) 2013-2024 by Mega Limited, Auckland, New Zealand
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

#include "mega/types.h"

#include <cstdint>
#include <string>

namespace mega
{

/**
 * @brief Compute the media capture timestamp only if the file is a photo, video, or audio file.
 *
 * Splits the filename into stem + extension in a single pass, checks the extension
 * against known media types, then extracts a timestamp from the stem using generic
 * digit extraction. Falls back to mtime, then ctime.
 *
 * Priority: filename timestamp > mtime > ctime.
 *
 * @param filename The file's display name (basename, e.g. "IMG_20240115_103045.jpg").
 * @param mtime    File modification time in seconds since epoch.
 * @param ctime    File creation time in seconds since epoch (0 if unavailable).
 * @return Milliseconds since epoch (UTC), or 0 if not a media file or no timestamp found.
 */
uint64_t computeMediaTsIfMediaFile(const std::string& filename, m_time_t mtime, m_time_t ctime);

} // namespace mega
