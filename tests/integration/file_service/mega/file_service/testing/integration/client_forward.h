#pragma once

#include <memory>

namespace mega
{
namespace file_service
{
namespace testing
{

class Client;

using ClientPtr = std::unique_ptr<Client>;

} // testing
} // file_service
} // mega
