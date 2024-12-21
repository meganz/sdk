#pragma once

#include <mega/fuse/common/testing/client.h>

namespace mega
{
namespace fuse
{
namespace testing
{

class MockClient
  : public Client
{
public:
    MockClient(const std::string& clientName, const Path& databasePath, const Path& storagePath);

    ~MockClient();
}; // MockClient

} // testing
} // fuse
} // mega

