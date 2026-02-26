#include <mega/common/date_time.h>
#include <mega/utils.h>

#include <iomanip>
#include <sstream>

namespace mega
{
namespace common
{
namespace detail
{

bool DateTime::operator==(const DateTime& rhs) const
{
    return mValue == rhs.mValue;
}

bool DateTime::operator!=(const DateTime& rhs) const
{
    return mValue != rhs.mValue;
}

std::string toString(const DateTime& value)
{
    std::ostringstream ostream;
    struct tm tm;

    m_localtime(value.asValue<m_time_t>(), &tm);

    ostream << std::put_time(&tm, "%Y/%m/%d %H:%M:%S");

    return ostream.str();
}

} // detail
} // common
} // mega
