#include <mega/common/database_utilities.h>
#include <mega/common/logging.h>

namespace mega
{
namespace common
{

ScopedForeignKeyDisabler::ScopedForeignKeyDisabler(Database& database):
    mDatabase(database),
    mEnabled()
{
    try
    {
        // Check if foreign keys are enabled in database.
        auto query = mDatabase.query();

        query = "PRAGMA foreign_keys";
        query.execute();

        mEnabled = query.field("foreign_keys").get<bool>();

        // Foreign keys are already disabled.
        if (!mEnabled)
            return;

        // Disable foreign keys in database.
        query = "pragma foreign_keys = OFF";
        query.execute();
    }
    catch (std::runtime_error& exception)
    {
        throw LogErrorF(mDatabase.logger(), "Couldn't disable foreign keys: ", exception.what());
    }
}

ScopedForeignKeyDisabler::~ScopedForeignKeyDisabler()
{
    try
    {
        // Foreign keys weren't enabled in database.
        if (!mEnabled)
            return;

        // Reenable foreign keys in database.
        auto query = mDatabase.query();

        query = "pragma foreign_keys = ON";
        query.execute();
    }
    catch (std::runtime_error& exception)
    {
        LogErrorF(mDatabase.logger(), "Couldn't re-enable foreign keys: %s", exception.what());
    }
}

} // common
} // mega
