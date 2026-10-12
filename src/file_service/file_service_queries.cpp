#include <mega/common/database.h>
#include <mega/file_service/file_service_queries.h>

namespace mega
{
namespace file_service
{

using namespace common;

FileServiceQueries::FileServiceQueries(Database& database):
    mAddFile(database.query()),
    mAddFileDuration(database.query()),
    mAddFileID(database.query()),
    mAddFileKeyData(database.query()),
    mAddFileRange(database.query()),
    mGetFile(database.query()),
    mGetFileDuration(database.query()),
    mGetFileIDByHandle(database.query()),
    mGetFileIDs(database.query()),
    mGetFileKeyData(database.query()),
    mGetFileRanges(database.query()),
    mGetFreeFileID(database.query()),
    mGetNextFileID(database.query()),
    mGetModifiedFiles(database.query()),
    mGetReclaimableFiles(database.query()),
    mGetStorageInfo(database.query()),
    mGetStorageUsed(database.query()),
    mRemoveFile(database.query()),
    mRemoveFileID(database.query()),
    mRemoveFileIDs(database.query()),
    mRemoveFileRanges(database.query()),
    mRemoveFiles(database.query()),
    mSetFileAccessTime(database.query()),
    mSetFileHandle(database.query()),
    mSetFileModificationTime(database.query()),
    mSetFileRemoved(database.query()),
    mSetFileSize(database.query()),
    mSetFileSizeAndModificationTime(database.query()),
    mSetNextFileID(database.query())
{
    mAddFile = "insert into files values ( "
               "  :accessed, "
               "  :allocated_size, "
               "  :dirty, "
               "  :handle, "
               "  :id, "
               "  :modified, "
               "  :removed, "
               "  :reported_size, "
               "  :size "
               ")";

    mAddFileDuration = "insert into file_durations values ( "
                       "  :duration, "
                       "  :id "
                       ")";

    mAddFileID = "insert into file_ids values (:id)";

    mAddFileKeyData = "insert into file_key_data values ( "
                      "  :chat_auth, "
                      "  :id, "
                      "  :is_public, "
                      "  :key_and_iv, "
                      "  :private_auth, "
                      "  :public_auth "
                      ")";

    mAddFileRange = "insert into file_ranges values ( "
                    "  :begin, "
                    "  :end, "
                    "  :id "
                    ")";

    mGetFile = "select * "
               "  from files "
               " where ((:handle is not null and handle = :handle) "
               "        or (:id is not null and id = :id)) "
               "   and (:removed is null or removed = :removed)";

    mGetFileDuration = "select duration "
                       "  from file_durations "
                       " where id = :id";

    mGetFileIDByHandle = "select id "
                         "  from files "
                         " where handle = :handle";

    mGetFileIDs = "select id "
                  "  from files "
                  " where (:dirty is null or dirty = :dirty) "
                  "   and (:removed is null or removed = :removed)";

    mGetFileKeyData = "select * from file_key_data where id = :id";

    mGetFileRanges = "select begin "
                     "     , end "
                     "  from file_ranges "
                     " where ((:position is null) "
                     "        or (begin < :position and end >= :position)) "
                     "   and id = :id";

    mGetFreeFileID = "select id "
                     "  from file_ids "
                     " limit 1";

    mGetNextFileID = "select next from file_id";

    mGetModifiedFiles = "select id "
                        "     , size "
                        "  from files "
                        " where dirty = 1";

    // Files marked for removal will be purged when closed.
    mGetReclaimableFiles = "select allocated_size "
                           "     , id "
                           "  from files "
                           " where allocated_size <> 0 "
                           "   and accessed <= :accessed "
                           "   and removed = 0 "
                           " order by accessed asc";

    // ifnull(...) is necessary as there may be no files to sum.
    mGetStorageInfo = "with TargetStats as ( "
                      "    select sum(allocated_size) as total_allocated_now "
                      "         , sum(reported_size) as total_reported_now "
                      "         , sum(size) as total_size_now "
                      "      from files "
                      "), "
                      "ReclaimableRows as ( "
                      "    select f.allocated_size "
                      "         , sum(f.allocated_size) over (order by f.accessed asc, f.id asc) "
                      "as running_reclaimed "
                      "         , ts.total_allocated_now "
                      "      from files f "
                      "      cross join TargetStats ts "
                      "     where f.removed = 0 "
                      "       and f.allocated_size <> 0 "
                      "       and f.accessed <= :accessed "
                      ") "
                      "select "
                      "    ifnull(( "
                      "        select sum(allocated_size) "
                      "        from ReclaimableRows "
                      // Include this file if, before subtracting it, was still above the target.
                      // Reclaim enough space to no more than the target size
                      "        where (total_allocated_now - running_reclaimed + allocated_size) > "
                      ":target_remaining_size "
                      "    ), 0) as size_to_reclaim, "
                      "    ifnull(total_allocated_now, 0) as total_allocated_size, "
                      "    ifnull(total_reported_now, 0) as total_reported_size, "
                      "    ifnull(total_size_now, 0) as total_size "
                      "from TargetStats ";

    // ifnull(...) is necessary as there may be no files to sum.
    mGetStorageUsed = "select ifnull(sum(allocated_size), 0) as total_allocated_size "
                      "     , ifnull(sum(reported_size), 0) as total_reported_size "
                      "     , ifnull(sum(size), 0) as total_size "
                      "  from files";

    mRemoveFile = "delete from files "
                  " where id = :id";

    mRemoveFileID = "delete from file_ids "
                    " where id = :id";

    mRemoveFileIDs = "delete from file_ids";

    mRemoveFileRanges = "delete from file_ranges "
                        " where begin >= :begin "
                        "   and end <= :end "
                        "   and id = :id";

    mRemoveFiles = "delete from files "
                   " where (:dirty is null or dirty = :dirty) "
                   "   and (:removed is null or removed = :removed)";

    mSetFileAccessTime = "update files "
                         "   set accessed = :accessed "
                         " where id = :id";

    mSetFileHandle = "update files "
                     "   set dirty = 0 "
                     "     , handle = :handle "
                     " where id = :id";

    mSetFileModificationTime = "update files "
                               "   set accessed = :accessed "
                               "     , dirty = 1 "
                               "     , modified = :modified "
                               " where id = :id";

    mSetFileRemoved = "update files "
                      "   set handle = null "
                      "     , removed = 1 "
                      " where id = :id";

    mSetFileSize = "update files "
                   "   set allocated_size = :allocated_size "
                   "     , reported_size = :reported_size "
                   "     , size = :size "
                   " where id = :id";

    mSetFileSizeAndModificationTime = "update files "
                                      "   set accessed = :accessed "
                                      "     , allocated_size = :allocated_size "
                                      "     , dirty = 1 "
                                      "     , modified = :modified "
                                      "     , reported_size = :reported_size "
                                      "     , size = :size "
                                      " where id = :id";

    mSetNextFileID = "update file_id "
                     "   set next = :next";
}

} // file_service
} // mega
