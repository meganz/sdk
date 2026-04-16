/**
 * @file mediats_utils.cpp
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

#include "mega/mediats_utils.h"

#include "mega/node.h"

#include <algorithm>

namespace mega
{

namespace
{

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

// Skip non-digits, then read exactly `n` consecutive digits starting at `pos`.
// Returns the parsed integer and advances `pos`, or returns -1 on failure.
int getDigits(const std::string& s, size_t& pos, size_t n)
{
    // skip non-digits
    while (pos < s.size() && !isDigit(s[pos]))
        ++pos;

    if (pos + n > s.size())
        return -1;

    int val = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (!isDigit(s[pos + i]))
            return -1;
        val = val * 10 + (s[pos + i] - '0');
    }
    pos += n;
    return val;
}

bool isLeapYear(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int daysInMonth(int y, int mo)
{
    static const int kDays[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mo < 1 || mo > 12)
        return 0;
    if (mo == 2 && isLeapYear(y))
        return 29;
    return kDays[mo];
}

// Convert UTC calendar fields to milliseconds since Unix epoch.
// Returns 0 on invalid input (callers treat 0 as "no result").
uint64_t toEpochMs(int y, int mo, int d, int h, int mi, int sec, int ms)
{
    // Days from 1970-01-01 to the given date.
    auto countLeaps = [](int yr) -> int64_t
    {
        return yr / 4 - yr / 100 + yr / 400;
    };
    int64_t days = static_cast<int64_t>(y - 1970) * 365 + countLeaps(y - 1) - countLeaps(1969);
    for (int m = 1; m < mo; ++m)
        days += daysInMonth(y, m);
    days += d - 1;

    int64_t secs = days * 86400 + h * 3600 + mi * 60 + sec;
    if (secs < 0)
        return 0;
    if (secs == 0 && ms == 0)
        return 0;
    return static_cast<uint64_t>(secs) * 1000 + static_cast<uint64_t>(ms);
}

// ---------------------------------------------------------------------------
// Filename splitting
// ---------------------------------------------------------------------------

// Split a filename into stem (for timestamp extraction) and lowercase extension
// (for media-type checking). Same rfind('.') logic as Node::getExtension.
// Note: when dot is at position 0 (e.g. ".jpg"), the entire string becomes the
// stem and ext is empty, so dot-files are not recognised as media.
void splitFilename(const std::string& filename, std::string& stem, std::string& ext)
{
    auto dot = filename.rfind('.');
    if (dot != std::string::npos && dot > 0)
    {
        stem = filename.substr(0, dot);
        ext = filename.substr(dot + 1);
        for (auto& c: ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return;
    }
    stem = filename;
    ext.clear();
}

// ---------------------------------------------------------------------------
// Timestamp extraction from stem
// ---------------------------------------------------------------------------

// Extract a timestamp from a filename stem (extension already stripped by
// splitFilename). Scans left-to-right, skipping non-digit characters, and
// reads groups of digits as YYYY MM DD HH MM SS. Optionally parses fractional
// seconds (separator: '.' or '_') and timezone offset (+HHMM / -HHMM).
uint64_t extractTsFromStem(const std::string& stem)
{
    size_t i = 0;

    // --- Extract 6 digit groups: YYYY MM DD HH MM SS ---
    int year = getDigits(stem, i, 4);
    if (year < 0)
        return 0;

    int month = getDigits(stem, i, 2);
    if (month < 0)
        return 0;

    int day = getDigits(stem, i, 2);
    if (day < 0)
        return 0;

    int hour = getDigits(stem, i, 2);
    if (hour < 0)
        return 0;

    int minute = getDigits(stem, i, 2);
    if (minute < 0)
        return 0;

    int second = getDigits(stem, i, 2);
    if (second < 0)
        return 0;

    // --- Strict validation ---
    if (year < 1970 || year > 9999)
        return 0;
    if (month < 1 || month > 12)
        return 0;
    if (day < 1 || day > daysInMonth(year, month))
        return 0;
    if (hour > 23)
        return 0;
    if (minute > 59)
        return 0;
    if (second > 59)
        return 0;

    // --- Optional fractional seconds (separator: '.' or '_') ---
    int millisecond = 0;
    if (i < stem.size() && (stem[i] == '.' || stem[i] == '_') && i + 1 < stem.size() &&
        isDigit(stem[i + 1]))
    {
        size_t fracStart = i + 1;
        size_t fracEnd = fracStart;
        while (fracEnd < stem.size() && isDigit(stem[fracEnd]))
            ++fracEnd;

        // Integer-only: parse up to 3 digits, pad/truncate to ms.
        // Max possible value with 3 digits and padding is 999, so no overflow.
        size_t digits = std::min(fracEnd - fracStart, size_t{3});
        int ms = 0;
        for (size_t f = 0; f < digits; ++f)
            ms = ms * 10 + (stem[fracStart + f] - '0');
        for (size_t f = digits; f < 3; ++f)
            ms *= 10; // pad: "1" → 100, "12" → 120
        millisecond = ms;

        i = fracEnd;
    }

    // --- Optional timezone offset (+HHMM / -HHMM) ---
    // Only scan a small window after seconds/fractional to avoid matching
    // unrelated +/- characters deep in the filename suffix.
    int tzOffsetMinutes = 0;
    const size_t tzScanLimit = std::min(i + 6, stem.size());
    while (i < tzScanLimit)
    {
        char sign = stem[i];
        if (sign == '+' || sign == '-')
        {
            size_t tzPos = i + 1;
            // Read 4 digits for HHMM (no skip-non-digits here)
            if (tzPos + 4 > stem.size())
                break;

            int tzVal = 0;
            bool validTz = true;
            for (size_t t = 0; t < 4; ++t)
            {
                if (!isDigit(stem[tzPos + t]))
                {
                    validTz = false;
                    break;
                }
                tzVal = tzVal * 10 + (stem[tzPos + t] - '0');
            }

            if (validTz)
            {
                int tzHH = tzVal / 100;
                int tzMM = tzVal % 100;

                if (tzHH > 14)
                    break;
                if (tzMM != 0 && tzMM != 15 && tzMM != 30 && tzMM != 45)
                    break;
                if (tzHH == 14 && tzMM != 0)
                    break;

                tzOffsetMinutes = tzHH * 60 + tzMM;
                if (sign == '-')
                    tzOffsetMinutes = -tzOffsetMinutes;
            }
            break;
        }
        ++i;
    }

    // --- Convert to UTC epoch milliseconds ---
    uint64_t utcMs = toEpochMs(year, month, day, hour, minute, second, millisecond);
    if (utcMs == 0)
        return 0;

    // Adjust for timezone: UTC = local - offset
    int64_t adjusted =
        static_cast<int64_t>(utcMs) - static_cast<int64_t>(tzOffsetMinutes) * 60 * 1000;
    if (adjusted <= 0)
        return 0;

    return static_cast<uint64_t>(adjusted);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

uint64_t computeMediaTsIfMediaFile(const std::string& filename, m_time_t mtime, m_time_t ctime)
{
    // Split filename into stem + extension in one pass.
    // This avoids the old double-rfind problem where stemName and getExtension
    // could disagree on where the extension boundary was.
    std::string stem, ext;
    splitFilename(filename, stem, ext);

    if (!isPhotoVideoAudioByName(ext))
        return 0;

    // Priority 1: filename timestamp (generic digit extraction on the stem)
    uint64_t fromFilename = extractTsFromStem(stem);
    if (fromFilename > 0)
        return fromFilename;

    // Priority 2: mtime (seconds → ms)
    if (mtime > 0)
        return static_cast<uint64_t>(mtime) * 1000;

    // Priority 3: ctime (seconds → ms)
    if (ctime > 0)
        return static_cast<uint64_t>(ctime) * 1000;

    return 0;
}

} // namespace mega
