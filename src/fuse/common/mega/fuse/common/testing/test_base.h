#pragma once

#include <mega/fuse/common/testing/test.h>

namespace mega
{
namespace fuse
{
namespace testing
{

class TestBase
  : public Test
{
protected:
    // Perform fixture-specific setup.
    bool DoSetUp(const Parameters& parameters) override;
}; // TestBase

} // testing
} // fuse
} // mega

