/**
 * @file CacheKeyCombinations.h
 * @brief Cache-key input dimensions, shared by the CacheKeyBuilder oracle test
 *        (Sqlite_test.cpp) and microbenchmark (SqliteDateSectionsPerf_test.cpp).
 *        Both iterate these arrays exhaustively, so they must stay identical —
 *        hence one definition here.
 */
#pragma once

#include <mega/db/sqlite.h> // OrderByClause, AnchorDirectionDigit
#include <mega/nodemanager.h> // DateSectionGranularity
#include <mega/types.h> // MimeType_t

#include <array>

namespace mega
{
namespace pagetest
{

// All MimeType_t values defined in include/mega/types.h (0..13 inclusive).
inline constexpr std::array<MimeType_t, 14> kAllMimeTypes{{
    MIME_TYPE_UNKNOWN,
    MIME_TYPE_PHOTO,
    MIME_TYPE_AUDIO,
    MIME_TYPE_VIDEO,
    MIME_TYPE_DOCUMENT,
    MIME_TYPE_PDF,
    MIME_TYPE_PRESENTATION,
    MIME_TYPE_ARCHIVE,
    MIME_TYPE_PROGRAM,
    MIME_TYPE_MISC,
    MIME_TYPE_SPREADSHEET,
    MIME_TYPE_ALL_DOCS,
    MIME_TYPE_OTHERS,
    MIME_TYPE_ALL_VISUAL_MEDIA,
}};
// Tripwire: a new MimeType_t bumps MIME_TYPE_MAX, so this fails until the array covers it.
static_assert(kAllMimeTypes.size() == static_cast<size_t>(MIME_TYPE_MAX) + 1,
              "kAllMimeTypes out of sync with MimeType_t — add the new value");

// All currently valid OrderByClause values (with gaps 9..16 omitted).
inline constexpr std::array<int, 12> kAllValidOrders{{
    OrderByClause::DEFAULT_ASC,
    OrderByClause::DEFAULT_DESC,
    OrderByClause::SIZE_ASC,
    OrderByClause::SIZE_DESC,
    OrderByClause::CTIME_ASC,
    OrderByClause::CTIME_DESC,
    OrderByClause::MTIME_ASC,
    OrderByClause::MTIME_DESC,
    OrderByClause::LABEL_ASC,
    OrderByClause::LABEL_DESC,
    OrderByClause::FAV_ASC,
    OrderByClause::FAV_DESC,
}};
// Tripwire for a new order pair appended after FAV_DESC (which moves OrderByClause::LAST).
// Won't catch re-use of a reserved gap (9..16), but those are obsolete slots.
static_assert(kAllValidOrders.back() == OrderByClause::LAST,
              "kAllValidOrders must end at OrderByClause::LAST — add the new order pair");

inline constexpr std::array<AnchorDirectionDigit, 3> kAllAnchorDirs{{
    AnchorDirectionDigit::None,
    AnchorDirectionDigit::Asc,
    AnchorDirectionDigit::Desc,
}};
// Tripwire: a new value bumps AnchorDirectionDigit::Max, so size != Max + 1 until covered here.
static_assert(kAllAnchorDirs.size() == static_cast<size_t>(AnchorDirectionDigit::Max) + 1,
              "kAllAnchorDirs out of sync with AnchorDirectionDigit — add the new value");

inline constexpr std::array<DateSectionGranularity, 3> kAllGranularities{{
    DateSectionGranularity::Day,
    DateSectionGranularity::Month,
    DateSectionGranularity::Year,
}};
// Tripwire: a new value bumps DateSectionGranularity::Max, so size != Max + 1 until covered here.
static_assert(kAllGranularities.size() == static_cast<size_t>(DateSectionGranularity::Max) + 1,
              "kAllGranularities out of sync with DateSectionGranularity — add the new value");

inline constexpr std::array<FavouriteFilter_t, 3> kAllFavouriteFilters{{
    FAVOURITE_FILTER_DISABLED,
    FAVOURITE_FILTER_ONLY_TRUE,
    FAVOURITE_FILTER_ONLY_FALSE,
}};
static_assert(kAllFavouriteFilters.size() == static_cast<size_t>(FAVOURITE_FILTER_MAX) + 1,
              "kAllFavouriteFilters out of sync with FavouriteFilter_t — add the new value");

} // namespace pagetest
} // namespace mega
