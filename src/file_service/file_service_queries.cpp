#include <mega/common/database.h>
#include <mega/file_service/file_service_queries.h>

namespace mega
{
namespace file_service
{

using namespace common;

FileServiceQueries::FileServiceQueries(Database& database):
    mAddFile(database.query()),
    mAddFileID(database.query()),
    mAddFileRange(database.query()),
    mGetFile(database.query()),
    mGetFileByNameAndParentHandle(database.query()),
    mGetFileIDs(database.query()),
    mGetFileIDsByParentHandle(database.query()),
    mGetFileRanges(database.query()),
    mGetFreeFileID(database.query()),
    mGetNextFileID(database.query()),
    mGetReclaimableFiles(database.query()),
    mGetStorageUsed(database.query()),
    mRemoveFile(database.query()),
    mRemoveFileID(database.query()),
    mRemoveFileIDs(database.query()),
    mRemoveFileRanges(database.query()),
    mRemoveFiles(database.query()),
    mSetFileAccessTime(database.query()),
    mSetFileHandle(database.query()),
    mSetFileLocation(database.query()),
    mSetFileModificationTime(database.query()),
    mSetFileRemoved(database.query()),
    mSetFileSize(database.query()),
    mSetNextFileID(database.query())
{
    mAddFile = "insert into files values ( "
               "  :accessed, "
               "  :allocated_size, "
               "  :dirty, "
               "  :handle, "
               "  :id, "
               "  :modified, "
               "  :name, "
               "  :parent_handle, "
               "  :removed, "
               "  :reported_size, "
               "  :size "
               ")";

    mAddFileID = "insert into file_ids values (:id)";

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

    mGetFileByNameAndParentHandle = "select * "
                                    "  from files "
                                    " where name = :name and parent_handle = :parent_handle";

    mGetFileIDs = "select id "
                  "  from files "
                  " where (:removed is null or removed = :removed)";

    mGetFileIDsByParentHandle = "select id "
                                "  from files "
                                " where parent_handle = :parent_handle";

    mGetFileRanges = "select begin "
                     "     , end "
                     "  from file_ranges "
                     " where id = :id";

    mGetFreeFileID = "select id "
                     "  from file_ids "
                     " limit 1";

    mGetNextFileID = "select next from file_id";

    // Files marked for removal will be purged when closed.
    mGetReclaimableFiles = "select allocated_size "
                           "     , id "
                           "  from files "
                           " where allocated_size <> 0 "
                           "   and accessed <= :accessed "
                           "   and removed = 0 "
                           " order by accessed desc";

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
                   " where (:removed is null or removed = :removed)";

    mSetFileAccessTime = "update files "
                         "   set accessed = :accessed "
                         " where id = :id";

    mSetFileHandle = "update files "
                     "   set handle = :handle "
                     " where id = :id";

    mSetFileLocation = "update files "
                       "   set name = :name "
                       "     , parent_handle = :parent_handle "
                       " where id = :id";

    mSetFileModificationTime = "update files "
                               "   set accessed = :accessed "
                               "     , dirty = 1 "
                               "     , modified = :modified "
                               " where id = :id";

    mSetFileRemoved = "update files "
                      "   set name = null "
                      "     , parent_handle = null "
                      "     , removed = 1 "
                      " where id = :id";

    mSetFileSize = "update files "
                   "   set allocated_size = :allocated_size "
                   "     , reported_size = :reported_size "
                   "     , size = :size "
                   " where id = :id";

    mSetNextFileID = "update file_id "
                     "   set next = :next";
}

} // file_service
} // mega
