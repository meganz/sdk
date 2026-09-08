#include <mega/common/assertions.h>
#include <mega/logging.h>

#include <cstdlib>
#include <iostream>

namespace mega
{
namespace common
{
namespace detail
{

static const char* leafname(std::string_view file);

[[noreturn]]
void assertFailure(std::string_view assertion,
                   std::string_view file,
                   std::string_view function,
                   const int line_no)
{
    SimpleLogger(logFatal, leafname(file), line_no)
        << function << ": Assertion failed: `" << assertion << "'";

    std::cerr << file << ":" << line_no << ": " << function << ": Assertion failed: `" << assertion
              << "'" << std::endl;

    std::abort();
}

static const char* leafname(std::string_view file)
{
    auto offset = file.find_last_of("\\/");

    if (offset != std::string_view::npos)
        return file.data() + offset + 1;

    return file.data();
}

} // detail
} // common
} // mega
