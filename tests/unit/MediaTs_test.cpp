/**
 * @file MediaTs_test.cpp
 * @brief Unit tests for media capture timestamp utilities.
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

#include <gtest/gtest.h>

#include <string>

using namespace mega;

// ---------------------------------------------------------------------------
// Helpers: compute expected epoch ms for a UTC date/time independently,
// plus a shared parameter type for the TEST_P suites below.
// ---------------------------------------------------------------------------

namespace
{

int64_t daysToYear(int y)
{
    int64_t days = 0;
    for (int yr = 1970; yr < y; ++yr)
        days += ((yr % 4 == 0 && yr % 100 != 0) || yr % 400 == 0) ? 366 : 365;
    return days;
}

const int kDays[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

int daysInMo(int y, int mo)
{
    if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
        return 29;
    return kDays[mo];
}

uint64_t utcMs(int y, int mo, int d, int h, int mi, int sec, int ms = 0)
{
    int64_t days = daysToYear(y);
    for (int m = 1; m < mo; ++m)
        days += daysInMo(y, m);
    days += d - 1;
    return static_cast<uint64_t>((days * 86400 + h * 3600 + mi * 60 + sec) * 1000 + ms);
}

// Row type for TEST_P suites. test_name is surfaced via the name generator so
// gtest output shows e.g. "Filename/FilenamePrefix.Parses/IMG_basic" on failure
// rather than an opaque index.
struct FilenameCase
{
    const char* filename;
    uint64_t expected_ms;
    const char* test_name;
};

std::string filenameCaseName(const ::testing::TestParamInfo<FilenameCase>& info)
{
    return info.param.test_name;
}

// Row type for the computeMediaTsIfMediaFile priority-chain suite, which exercises
// extension gating and mtime/ctime fallback alongside the filename input.
struct PriorityChainCase
{
    const char* filename;
    m_time_t mtime;
    m_time_t ctime;
    uint64_t expected_ms;
    const char* test_name;
};

std::string priorityChainCaseName(const ::testing::TestParamInfo<PriorityChainCase>& info)
{
    return info.param.test_name;
}

} // anonymous namespace

// ===========================================================================
// Filename-based parsing — computeMediaTsIfMediaFile(filename, 0, 0)
//
// All suites below pass mtime=0 and ctime=0 so the result is purely from the
// filename pattern (or 0 if no pattern matches). Structurally-identical cases
// are grouped into TEST_P suites, with any rationale comments placed inline
// above the FilenameCase row they explain. A single standalone TEST remains
// (timestamps_are_UTC) because of its multi-assertion cross-check shape.
// ===========================================================================

// ---------------------------------------------------------------------------
// Filename prefix patterns — IMG/VID/PXL/DSC/... + compact date + time
// ---------------------------------------------------------------------------

class FilenamePrefix: public ::testing::TestWithParam<FilenameCase>
{};

TEST_P(FilenamePrefix, Parses)
{
    EXPECT_EQ(GetParam().expected_ms, computeMediaTsIfMediaFile(GetParam().filename, 0, 0));
}

INSTANTIATE_TEST_SUITE_P(
    Filename,
    FilenamePrefix,
    ::testing::Values(
        FilenameCase{"IMG_20240115_103045.jpg", utcMs(2024, 1, 15, 10, 30, 45), "IMG_basic"},
        FilenameCase{"VID_20230620_080500.mp4", utcMs(2023, 6, 20, 8, 5, 0), "VID_prefix"},
        FilenameCase{"PXL_20221231_235959.jpg", utcMs(2022, 12, 31, 23, 59, 59), "PXL_prefix"},
        FilenameCase{"DSC_20210307_140000.jpg", utcMs(2021, 3, 7, 14, 0, 0), "DSC_prefix"},
        FilenameCase{"DSCN_20210307_140000.jpg", utcMs(2021, 3, 7, 14, 0, 0), "DSCN_prefix"},
        FilenameCase{"DSCF_20210307_140000.jpg", utcMs(2021, 3, 7, 14, 0, 0), "DSCF_prefix"},
        FilenameCase{"PHOTO_20230715_143022.jpg", utcMs(2023, 7, 15, 14, 30, 22), "PHOTO_basic"},
        FilenameCase{"DCIM_20230715_143022.jpg", utcMs(2023, 7, 15, 14, 30, 22), "DCIM_basic"},
        FilenameCase{"20231125_091530.jpg", utcMs(2023, 11, 25, 9, 15, 30), "Samsung_generic"},
        FilenameCase{"20230715-143022.jpg", utcMs(2023, 7, 15, 14, 30, 22), "hyphen_compact"}),
    filenameCaseName);

// ---------------------------------------------------------------------------
// Separator-agnostic: the generic parser skips any non-digit characters
// ---------------------------------------------------------------------------

class SeparatorAgnostic: public ::testing::TestWithParam<FilenameCase>
{};

TEST_P(SeparatorAgnostic, Parses)
{
    EXPECT_EQ(GetParam().expected_ms, computeMediaTsIfMediaFile(GetParam().filename, 0, 0));
}

INSTANTIATE_TEST_SUITE_P(
    Filename,
    SeparatorAgnostic,
    ::testing::Values(
        FilenameCase{"Screenshot_2024-01-15-10-30-45.png",
                     utcMs(2024, 1, 15, 10, 30, 45),
                     "Screenshot_android"},
        FilenameCase{"2024-01-15 10.30.45.png", utcMs(2024, 1, 15, 10, 30, 45), "macOS_screenshot"},
        FilenameCase{"2024-01-15 10.30.45 (1).png",
                     utcMs(2024, 1, 15, 10, 30, 45),
                     "macOS_screenshot_with_counter"},
        FilenameCase{"WhatsApp Image 2023-07-15 at 14.30.22.jpeg",
                     utcMs(2023, 7, 15, 14, 30, 22),
                     "WhatsApp_Image"},
        FilenameCase{"WhatsApp Video 2023-07-15 at 14.30.22.mp4",
                     utcMs(2023, 7, 15, 14, 30, 22),
                     "WhatsApp_Video"},
        FilenameCase{"WhatsApp Image 2023-07-15 at 14.30.22 (1).jpeg",
                     utcMs(2023, 7, 15, 14, 30, 22),
                     "WhatsApp_Image_with_duplicate_suffix"},
        // Generic parser skips " at " — extracts digits fine
        FilenameCase{"2024-01-15 at 10.30.45.png",
                     utcMs(2024, 1, 15, 10, 30, 45),
                     "macOS_screenshot_at_variant"},
        // "AT" vs "at" — both are non-digits, skipped
        FilenameCase{"WhatsApp Image 2023-07-15 AT 14.30.22.jpeg",
                     utcMs(2023, 7, 15, 14, 30, 22),
                     "WhatsApp_Image_uppercase_AT"}),
    filenameCaseName);

// ---------------------------------------------------------------------------
// Case insensitivity: generic parser only cares about digit positions
// ---------------------------------------------------------------------------

class CaseInsensitive: public ::testing::TestWithParam<FilenameCase>
{};

TEST_P(CaseInsensitive, Parses)
{
    EXPECT_EQ(GetParam().expected_ms, computeMediaTsIfMediaFile(GetParam().filename, 0, 0));
}

INSTANTIATE_TEST_SUITE_P(Filename,
                         CaseInsensitive,
                         ::testing::Values(FilenameCase{"img_20240115_103045.jpg",
                                                        utcMs(2024, 1, 15, 10, 30, 45),
                                                        "lowercase_prefix"},
                                           FilenameCase{"Img_20240115_103045.JPG",
                                                        utcMs(2024, 1, 15, 10, 30, 45),
                                                        "mixed_case_prefix"}),
                         filenameCaseName);

// ---------------------------------------------------------------------------
// Fractional seconds (and fractional-vs-timezone disambiguation)
// ---------------------------------------------------------------------------

class FractionalSeconds: public ::testing::TestWithParam<FilenameCase>
{};

TEST_P(FractionalSeconds, Parses)
{
    EXPECT_EQ(GetParam().expected_ms, computeMediaTsIfMediaFile(GetParam().filename, 0, 0));
}

INSTANTIATE_TEST_SUITE_P(
    Filename,
    FractionalSeconds,
    ::testing::Values(
        FilenameCase{"IMG_20240115_103045.123.jpg",
                     utcMs(2024, 1, 15, 10, 30, 45, 123),
                     "fractional_dot"},
        FilenameCase{"IMG_20240115_103045_123.jpg",
                     utcMs(2024, 1, 15, 10, 30, 45, 123),
                     "fractional_underscore"},
        // '-' is NOT a fractional separator (to avoid conflict with timezone offset).
        // "-123" has only 3 digits so it fails timezone parsing too → no fractional, no tz.
        FilenameCase{"IMG_20240115_103045-123.jpg",
                     utcMs(2024, 1, 15, 10, 30, 45),
                     "hyphen_not_fractional_separator"},
        // "-0500" is a timezone offset, not fractional seconds.
        // 14:30:22 -0500 → UTC = 14:30:22 + 5h = 19:30:22
        FilenameCase{"IMG_20230715_143022-0500.jpg",
                     utcMs(2023, 7, 15, 19, 30, 22),
                     "timezone_minus_without_fractional"},
        // ".1" → 0.1 → 100 ms
        FilenameCase{"IMG_20240115_103045.1.jpg",
                     utcMs(2024, 1, 15, 10, 30, 45, 100),
                     "fractional_single_digit"},
        // ".123456" → 0.123456 → floor(123.456) = 123 ms
        FilenameCase{"IMG_20240115_103045.123456.jpg",
                     utcMs(2024, 1, 15, 10, 30, 45, 123),
                     "fractional_six_digits"}),
    filenameCaseName);

// ---------------------------------------------------------------------------
// Timezone offset
// ---------------------------------------------------------------------------

class TimezoneOffset: public ::testing::TestWithParam<FilenameCase>
{};

TEST_P(TimezoneOffset, Parses)
{
    EXPECT_EQ(GetParam().expected_ms, computeMediaTsIfMediaFile(GetParam().filename, 0, 0));
}

INSTANTIATE_TEST_SUITE_P(
    Filename,
    TimezoneOffset,
    ::testing::Values(
        // 14:30:22 +0530 → UTC = 14:30:22 - 5h30m = 09:00:22
        FilenameCase{"IMG_20230715_143022+0530.jpg", utcMs(2023, 7, 15, 9, 0, 22), "timezone_plus"},
        // +0000 → no adjustment
        FilenameCase{"IMG_20230715_143022+0000.jpg",
                     utcMs(2023, 7, 15, 14, 30, 22),
                     "timezone_plus_zero"},
        // 14:30:22.000 -0500 → UTC = 14:30:22 + 5h = 19:30:22
        FilenameCase{"IMG_20230715_143022.000-0500.jpg",
                     utcMs(2023, 7, 15, 19, 30, 22),
                     "timezone_minus"},
        // +1400 is the maximum real offset (Kiribati). Accepted.
        // 02:00:00 +1400 → UTC = 02:00:00 - 14h → previous day 12:00:00
        FilenameCase{"IMG_20230715_020000+1400.jpg",
                     utcMs(2023, 7, 14, 12, 0, 0),
                     "timezone_plus_1400_valid"},
        // +1430 does not exist — ignored, no tz adjustment applied
        FilenameCase{"IMG_20230715_143022+1430.jpg",
                     utcMs(2023, 7, 15, 14, 30, 22),
                     "timezone_plus_1430_rejected"}),
    filenameCaseName);

// ---------------------------------------------------------------------------
// Parse failures — filenames where the parser returns 0
// ---------------------------------------------------------------------------

class ParseFailure: public ::testing::TestWithParam<FilenameCase>
{};

TEST_P(ParseFailure, ReturnsZero)
{
    EXPECT_EQ(GetParam().expected_ms, computeMediaTsIfMediaFile(GetParam().filename, 0, 0));
}

INSTANTIATE_TEST_SUITE_P(
    Filename,
    ParseFailure,
    ::testing::Values(
        FilenameCase{"IMG_20241300_103045.jpg", 0, "invalid_month_13"},
        FilenameCase{"IMG_20241132_103045.jpg", 0, "invalid_day_32"},
        FilenameCase{"IMG_20240115_250045.jpg", 0, "invalid_hour_25"},
        FilenameCase{"IMG_20240115_106045.jpg", 0, "invalid_minute_60"},
        FilenameCase{"IMG_20240115_103060.jpg", 0, "invalid_second_60"},
        FilenameCase{"IMG_19691231_235959.jpg", 0, "invalid_year_before_epoch"},
        FilenameCase{"IMG_2024XX15_103045.jpg", 0, "invalid_non_digit_in_date"},
        // IMG-20240115-WA0001.jpg: after date, parser reads WA→skip, 00→hour, 01→minute,
        // then no digits for second → fails → returns 0
        FilenameCase{"IMG-20240115-WA0001.jpg", 0, "WhatsApp_legacy_no_time"},
        // "IMG_2024011_103045" — after year=2024, month=01, only 1 digit "1" before "_"
        FilenameCase{"IMG_2024011_103045.jpg", 0, "not_enough_digits_for_day"},
        // Media extension but no timestamp digits at all
        FilenameCase{"img.jpg", 0, "no_match_short"}),
    filenameCaseName);

// ---------------------------------------------------------------------------
// Leap year handling
// ---------------------------------------------------------------------------

class LeapYear: public ::testing::TestWithParam<FilenameCase>
{};

TEST_P(LeapYear, Parses)
{
    EXPECT_EQ(GetParam().expected_ms, computeMediaTsIfMediaFile(GetParam().filename, 0, 0));
}

INSTANTIATE_TEST_SUITE_P(
    Filename,
    LeapYear,
    ::testing::Values(
        FilenameCase{"IMG_20240229_120000.jpg", utcMs(2024, 2, 29, 12, 0, 0), "feb29_leap_valid"},
        FilenameCase{"IMG_20230229_120000.jpg", 0, "feb29_non_leap_invalid"}),
    filenameCaseName);

// ---------------------------------------------------------------------------
// UTC cross-check
// ---------------------------------------------------------------------------

TEST(MediaTsParser, timestamps_are_UTC)
{
    const uint64_t expected = utcMs(2024, 1, 15, 10, 30, 45);
    EXPECT_EQ(expected, computeMediaTsIfMediaFile("IMG_20240115_103045.jpg", 0, 0));
    // 2024-01-15T10:30:45Z = 1705314645000 ms
    EXPECT_EQ(uint64_t{1705314645000}, expected);
}

// ===========================================================================
// computeMediaTsIfMediaFile — extension gate + priority chain
// ===========================================================================

class PriorityChain: public ::testing::TestWithParam<PriorityChainCase>
{};

TEST_P(PriorityChain, Returns)
{
    EXPECT_EQ(GetParam().expected_ms,
              computeMediaTsIfMediaFile(GetParam().filename, GetParam().mtime, GetParam().ctime));
}

INSTANTIATE_TEST_SUITE_P(
    ComputeMediaTs,
    PriorityChain,
    ::testing::Values(
        // Extension gate: non-media extension short-circuits, mtime/ctime ignored
        PriorityChainCase{"document.pdf",
                          1700000000LL,
                          1600000000LL,
                          0,
                          "non_media_file_returns_zero"},
        // Empty filename: extension gate fires (ext="") before mtime/ctime fallback
        PriorityChainCase{"", 1700000000LL, 1600000000LL, 0, "empty_filename_ignores_mtime"},
        // ".jpg" → splitFilename treats as stem=".jpg", ext="" → not recognised as media
        PriorityChainCase{".jpg",
                          1700000000LL,
                          1600000000LL,
                          0,
                          "dot_only_extension_not_recognised"},
        // Priority: filename pattern wins over mtime and ctime
        PriorityChainCase{"IMG_20240115_103045.jpg",
                          1000000,
                          999000,
                          utcMs(2024, 1, 15, 10, 30, 45),
                          "filename_wins_over_all"},
        // Priority: mtime wins over ctime when filename has no pattern
        PriorityChainCase{"photo.jpg",
                          1700000000LL,
                          1600000000LL,
                          1700000000ULL * 1000,
                          "mtime_wins_over_ctime"},
        // Priority: fall back to ctime when mtime is zero
        PriorityChainCase{"photo.jpg", 0, 1600000000LL, 1600000000ULL * 1000, "fallback_to_ctime"},
        PriorityChainCase{"photo.jpg", 0, 0, 0, "zero_when_all_sources_zero"},
        PriorityChainCase{"photo.jpg",
                          1700000000LL,
                          0,
                          1700000000ULL * 1000,
                          "mtime_used_when_no_filename_pattern"},
        // Negative mtime is treated as absent; fall through to ctime
        PriorityChainCase{"photo.jpg",
                          -1,
                          1600000000LL,
                          1600000000ULL * 1000,
                          "negative_mtime_falls_to_ctime"}),
    priorityChainCaseName);
