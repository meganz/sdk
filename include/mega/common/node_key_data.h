#pragma once

#include <mega/common/node_key_data_forward.h>

#include <cstdint>
#include <optional>
#include <string>

namespace mega
{
namespace common
{

struct NodeKeyData
{
    std::optional<std::string> mChatAuth;
    std::string mKeyAndIV;
    std::optional<std::string> mPrivateAuth;
    std::optional<std::string> mPublicAuth;
    bool mIsPublicHandle;
}; // NodeKeyData

} // common
} // mega
