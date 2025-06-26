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
    mGetFileID(database.query()),
    mGetFileRanges(database.query()),
    mGetFileReferences(database.query()),
    mRemoveFile(database.query()),
    mRemoveFileID(database.query()),
    mRemoveFileRanges(database.query()),
    mSetFileHandle(database.query()),
    mSetFileID(database.query()),
    mSetFileModificationTime(database.query()),
    mSetFileReferences(database.query())
{
    mAddFile = "insert into files values ( "
               "  :dirty, "
               "  :handle, "
               "  :id, "
               "  :modified, "
               "  0 "
               ")";

    mAddFileID = "insert into file_ids values (:id, :next)";

    mAddFileRange = "insert into file_ranges values ( "
                    "  :begin, "
                    "  :end, "
                    "  :id "
                    ")";

    mGetFile = "select * "
               "  from files "
               " where (:handle is not null and handle = :handle) "
               "    or (:id is not null and id = :id)";

    mGetFileID = "   select file_id.free "
                 "        , file_ids.next as link "
                 "        , file_id.next "
                 "     from file_id "
                 "left join file_ids "
                 "       on file_ids.id = file_id.free";

    mGetFileRanges = "select begin "
                     "     , end "
                     "  from file_ranges "
                     " where id = :id";

    mGetFileReferences = "select num_references "
                         "  from files "
                         " where id = :id";

    mRemoveFile = "delete from files "
                  " where id = :id";

    mRemoveFileID = "delete from file_ids "
                    " where id = :id";

    mRemoveFileRanges = "delete from file_ranges "
                        " where begin >= :begin "
                        "   and end <= :end "
                        "   and id = :id";

    mSetFileHandle = "update files "
                     "   set handle = :handle "
                     " where id = :id";

    mSetFileID = "update file_id "
                 "   set free = :free "
                 "     , next = :next";

    mSetFileModificationTime = "update files "
                               "   set dirty = 1 "
                               "     , modified = :modified "
                               " where id = :id";

    mSetFileReferences = "update files "
                         "   set num_references = :num_references "
                         " where id = :id";
}

} // file_service
} // mega
