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
    mGetFileRanges(database.query()),
    mGetFileReferences(database.query()),
    mGetFreeFileID(database.query()),
    mGetNextFileID(database.query()),
    mRemoveFile(database.query()),
    mRemoveFileID(database.query()),
    mRemoveFileRanges(database.query()),
    mSetFileAccessTime(database.query()),
    mSetFileHandle(database.query()),
    mSetFileModificationTime(database.query()),
    mSetFileReferences(database.query()),
    mSetNextFileID(database.query())
{
    mAddFile = "insert into files values ( "
               "  :accessed, "
               "  :dirty, "
               "  :handle, "
               "  :id, "
               "  :modified, "
               "  0 "
               ")";

    mAddFileID = "insert into file_ids values (:id)";

    mAddFileRange = "insert into file_ranges values ( "
                    "  :begin, "
                    "  :end, "
                    "  :id "
                    ")";

    mGetFile = "select * "
               "  from files "
               " where (:handle is not null and handle = :handle) "
               "    or (:id is not null and id = :id)";

    mGetFileRanges = "select begin "
                     "     , end "
                     "  from file_ranges "
                     " where id = :id";

    mGetFileReferences = "select num_references "
                         "  from files "
                         " where id = :id";

    mGetFreeFileID = "select id "
                     "  from file_ids "
                     " limit 1";

    mGetNextFileID = "select next from file_id";

    mRemoveFile = "delete from files "
                  " where id = :id";

    mRemoveFileID = "delete from file_ids "
                    " where id = :id";

    mRemoveFileRanges = "delete from file_ranges "
                        " where begin >= :begin "
                        "   and end <= :end "
                        "   and id = :id";

    mSetFileAccessTime = "update files "
                         "   set accessed = :accessed "
                         " where id = :id";

    mSetFileHandle = "update files "
                     "   set handle = :handle "
                     " where id = :id";

    mSetFileModificationTime = "update files "
                               "   set accessed = :accessed "
                               "     , dirty = 1 "
                               "     , modified = :modified "
                               " where id = :id";

    mSetFileReferences = "update files "
                         "   set num_references = :num_references "
                         " where id = :id";

    mSetNextFileID = "update file_id "
                     "   set next = :next";
}

} // file_service
} // mega
