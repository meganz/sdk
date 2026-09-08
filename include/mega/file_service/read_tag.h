#pragma once

#include <variant>

namespace mega
{
namespace file_service
{

struct NormalReadTag
{}; // NormalReadTag

struct StreamReadTag
{
    // Is it a jump detection candidate reading?
    bool mIsJumpCandidate = false;
}; // StreamReadTag

using ReadTag = std::variant<NormalReadTag, StreamReadTag>;

bool isJumpCandidate(ReadTag readTag);

} // file_service
} // mega
