#pragma once

#include <memory>

namespace mega
{
namespace fuse
{
namespace testing
{

class Client;

using ClientPtr = std::unique_ptr<Client>;

} // testing
} // fuse
} // mega

