#pragma once

#include <memory>

namespace mega
{
namespace common
{
namespace testing
{

class Client;

using ClientPtr = std::unique_ptr<Client>;

} // testing
} // common
} // mega
