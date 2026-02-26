#include <mega/common/date_time.h>

#include <ostream>

namespace mega
{
namespace common
{

std::ostream& operator<<(std::ostream& ostream, const DateTime& value)
{
    return ostream << toString(value);
}

} // common
} // mega
