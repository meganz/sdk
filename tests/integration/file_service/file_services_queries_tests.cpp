#include "mega/common/scoped_query.h"

#include <gtest/gtest.h>
#include <mega/common/database.h>
#include <mega/common/subsystem_logger.h>
#include <mega/common/transaction.h>
#include <mega/file_service/database_builder.h>
#include <mega/file_service/file_service_queries.h>
#include <mega/file_service/storage_info.h>
#include <mega/localpath.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace mega
{
namespace file_service
{
namespace testing
{

using common::Database;
using common::SubsystemLogger;

class FileServiceQueriesGetStorageInfo: public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto* testInfo = ::testing::UnitTest::GetInstance()->current_test_info();
        mDbPath = std::string(testInfo->name()) + ".db";
        mDatabase.emplace(mLogger, mega::LocalPath::fromRelativePath(mDbPath));
        DatabaseBuilder(*mDatabase).build();
        mQueries.emplace(*mDatabase);
    }

    void TearDown() override
    {
        mQueries.reset();
        mDatabase.reset();
        std::filesystem::remove(mDbPath);
    }

    // Insert a row into `files` using mQueries->mAddFile.
    void addFile(std::int64_t id,
                 std::int64_t accessed,
                 std::uint64_t allocatedSize,
                 std::uint64_t reportedSize,
                 std::uint64_t size,
                 bool removed = false)
    {
        mQueries->mAddFile.reset();
        mQueries->mAddFile.param(":accessed").set(accessed);
        mQueries->mAddFile.param(":allocated_size").set(allocatedSize);
        mQueries->mAddFile.param(":dirty").set(0);
        mQueries->mAddFile.param(":handle").set(nullptr);
        mQueries->mAddFile.param(":id").set(id);
        mQueries->mAddFile.param(":modified").set(0);
        mQueries->mAddFile.param(":removed").set(removed);
        mQueries->mAddFile.param(":reported_size").set(reportedSize);
        mQueries->mAddFile.param(":size").set(size);
        mQueries->mAddFile.execute();
    }

    // Execute mGetStorageInfo with the given parameters and return populated StorageInfo.
    StorageInfo getStorageInfo(std::int64_t accessed, std::uint64_t targetRemainingSize)
    {
        auto transaction = mDatabase->transaction();
        auto query = transaction.query(mQueries->mGetStorageInfo);

        query.param(":accessed").set(accessed);
        query.param(":target_remaining_size").set(targetRemainingSize);
        query.execute();

        StorageInfo result;
        result.mReclaimableSize = query.field("size_to_reclaim").get<std::uint64_t>();
        result.mAllocatedSize = query.field("total_allocated_size").get<std::uint64_t>();
        result.mReportedSize = query.field("total_reported_size").get<std::uint64_t>();
        result.mTotalSize = query.field("total_size").get<std::uint64_t>();

        return result;
    }

    SubsystemLogger mLogger{"FileServiceQueriesGetStorageInfo"};
    std::optional<Database> mDatabase;
    std::optional<FileServiceQueries> mQueries;
    std::string mDbPath;
};

// An empty database should report all sizes as zero.
TEST_F(FileServiceQueriesGetStorageInfo, empty_database_returns_zeros)
{
    auto result = getStorageInfo(0, 0);
    EXPECT_EQ(result, (StorageInfo{0, 0, 0, 0}));
}

// Files whose accessed time is after the cutoff are not eligible for reclamation.
TEST_F(FileServiceQueriesGetStorageInfo, no_files_reclaimable_when_cutoff_before_accessed)
{
    addFile(1, 100, 500, 400, 1000);
    addFile(2, 100, 500, 400, 1000);

    // cutoff=50 is before both files' accessed=100, so neither is reclaimable.
    auto result = getStorageInfo(50, 0);
    EXPECT_EQ(result, (StorageInfo{0, 1000, 800, 2000}));
}

// A single eligible file is fully reclaimed when the target remaining size is zero.
TEST_F(FileServiceQueriesGetStorageInfo, single_file_fully_reclaimed_when_target_is_zero)
{
    addFile(1, 50, 500, 400, 1000);

    auto result = getStorageInfo(100, 0);
    EXPECT_EQ(result, (StorageInfo{500, 500, 400, 1000}));
}

// A file whose allocated size exactly equals the target is NOT reclaimed because the
// condition requires strictly greater than: (total - running + alloc) > target.
TEST_F(FileServiceQueriesGetStorageInfo, file_not_reclaimed_when_allocated_exactly_equals_target)
{
    addFile(1, 50, 500, 400, 1000);

    // target=500 means condition is 500 > 500, which is false.
    auto result = getStorageInfo(100, 500);
    EXPECT_EQ(result, (StorageInfo{0, 500, 400, 1000}));
}

// Files are reclaimed oldest-first until the total drops to the target.
// Files 1 (alloc=100) and 2 (alloc=200) are reclaimed; file 3 (alloc=300) is not.
TEST_F(FileServiceQueriesGetStorageInfo, partial_reclaim_reaches_exact_target)
{
    addFile(1, 10, 100, 100, 100);
    addFile(2, 20, 200, 200, 200);
    addFile(3, 30, 300, 300, 300);

    // total=600, target=300 → reclaim files 1+2 (300 bytes), leave file 3.
    auto result = getStorageInfo(100, 300);
    EXPECT_EQ(result, (StorageInfo{300, 600, 600, 600}));
}

// All eligible files are reclaimed when the target remaining size is zero.
TEST_F(FileServiceQueriesGetStorageInfo, all_files_reclaimed_when_target_is_zero)
{
    addFile(1, 10, 200, 200, 200);
    addFile(2, 20, 300, 300, 300);

    auto result = getStorageInfo(100, 0);
    EXPECT_EQ(result, (StorageInfo{500, 500, 500, 500}));
}

// Removed files are excluded from the reclaim candidate set but still counted in totals.
TEST_F(FileServiceQueriesGetStorageInfo, removed_files_excluded_from_reclaim_but_included_in_totals)
{
    addFile(1, 50, 300, 300, 300, /*removed=*/true);
    addFile(2, 60, 200, 200, 200, /*removed=*/false);

    // File 1 is removed → not reclaimable (200 bytes reclaimed from file 2 only).
    // Both files count towards totals.
    auto result = getStorageInfo(100, 0);
    EXPECT_EQ(result, (StorageInfo{200, 500, 500, 500}));
}

// Files with allocated_size == 0 are excluded from reclaim candidates.
TEST_F(FileServiceQueriesGetStorageInfo, files_with_zero_allocated_size_excluded_from_reclaim)
{
    addFile(1, 50, 0, 0, 1000); // alloc=0 → excluded from reclaim
    addFile(2, 60, 200, 200, 1000); // alloc=200 → eligible

    // File 1 not reclaimable (alloc=0); file 2 reclaimable.
    // total_allocated = 0+200 = 200; total_size = 1000+1000 = 2000.
    auto result = getStorageInfo(100, 0);
    EXPECT_EQ(result, (StorageInfo{200, 200, 200, 2000}));
}

// Two files with the same accessed time are both reclaimable when the target is zero.
// id=1 (alloc=100) is processed first, id=2 (alloc=200) second; both satisfy the condition.
TEST_F(FileServiceQueriesGetStorageInfo, same_accessed_time_both_files_reclaimable)
{
    addFile(1, 50, 100, 100, 100);
    addFile(2, 50, 200, 200, 200);

    // total=300, target=0 → both files reclaimed (300 bytes total).
    auto result = getStorageInfo(100, 0);
    EXPECT_EQ(result, (StorageInfo{300, 300, 300, 300}));
}

// When two files share the same accessed time, id breaks the tie (ascending).
// id=1 (alloc=200) is processed before id=2 (alloc=300).
// After reclaiming id=1 the running total reaches 500−200=300 which equals the target,
// so id=2 is not reclaimed (condition 300 > 300 is false).
TEST_F(FileServiceQueriesGetStorageInfo, same_accessed_time_tie_broken_by_id)
{
    addFile(1, 50, 200, 200, 200);
    addFile(2, 50, 300, 300, 300);

    // total=500, target=300 → only id=1 reclaimed (200 bytes); id=2 stays.
    auto result = getStorageInfo(100, 300);
    EXPECT_EQ(result, (StorageInfo{200, 500, 500, 500}));
}

// Files are reclaimed in ascending access-time order (oldest first).
// With target=300 and total=600, files 2 (accessed=10) and 3 (accessed=20) are reclaimed,
// but file 1 (accessed=30) is not.
TEST_F(FileServiceQueriesGetStorageInfo, oldest_files_reclaimed_first)
{
    addFile(1, 30, 300, 300, 300);
    addFile(2, 10, 100, 100, 100);
    addFile(3, 20, 200, 200, 200);

    auto result = getStorageInfo(100, 300);
    EXPECT_EQ(result, (StorageInfo{300, 600, 600, 600}));
}

} // testing
} // file_service
} // mega
