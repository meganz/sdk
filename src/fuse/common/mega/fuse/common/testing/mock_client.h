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
    MockClient(const Path& databasePath,
               const Path& storagePath);

    ~MockClient();
}; // MockClient

} // testing
} // fuse
} // mega

