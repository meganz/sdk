#include "mega/gfx/worker/commands.h"
#include "mega/utils.h"
#include <string>
#include <cassert>

namespace
{
using mega::CacheableWriter;
using mega::CacheableReader;
using mega::GfxDimension;

class GfxSerializationHelper
{
    static constexpr size_t MAX_VECT_SIZE = 100;
public:
    static void serialize(CacheableWriter& writer, const GfxDimension& source)
    {
        writer.serializeu32(source.w());
        writer.serializeu32(source.h());
    }
    static void serialize(CacheableWriter& writer, const std::string& source)
    {
        writer.serializestring_u32(source);
    }
    template<typename T>
    static void serialize(CacheableWriter& writer, const std::vector<T>& target)
    {
        auto vecSize = target.size();
        assert(vecSize < std::numeric_limits<uint32_t>::max());
        writer.serializeu32(static_cast<uint32_t>(vecSize));
        for (const auto& entry : target)
        {
            GfxSerializationHelper::serialize(writer, entry);
        }
    }
    static bool unserialize(CacheableReader& reader, GfxDimension& target)
    {
        uint32_t w = 0;
        if (!reader.unserializeu32(w))
        {
            return false;
        }
        uint32_t h = 0;
        if (!reader.unserializeu32(h))
        {
            return false;
        }
        target.setW(w);
        target.setH(h);
        return true;
    }
    static bool unserialize(CacheableReader& reader, std::string& target)
    {
        return reader.unserializestring_u32(target);
    }
    template<typename T>
    static bool unserialize(CacheableReader& reader, std::vector<T>& target, const size_t maxVectSize = MAX_VECT_SIZE)
    {
        uint32_t vecSize = 0;
        if (!reader.unserializeu32(vecSize))
        {
            return false;
        }
        if (vecSize > maxVectSize)
        {
            return false;
        }
        target.resize(vecSize);
        for (auto& entry : target)
        {
            if (!GfxSerializationHelper::unserialize(reader, entry))
            {
                return false;
            }
        }
        return true;
    }
};

}

namespace mega {
namespace gfx {

std::unique_ptr<ICommand> ICommand::factory(CommandType type)
{
    switch (type)
    {
    case CommandType::NEW_GFX:
        return std::make_unique<CommandNewGfx>();
    case CommandType::NEW_GFX_RESPONSE:
        return std::make_unique<CommandNewGfxResponse>();
    case CommandType::SHUTDOWN:
        return std::make_unique<CommandShutDown>();
    case CommandType::SHUTDOWN_RESPONSE:
        return std::make_unique<CommandShutDownResponse>();
    case CommandType::HELLO:
        return std::make_unique<CommandHello>();
    case CommandType::HELLO_RESPONSE:
        return std::make_unique<CommandHelloResponse>();
    case CommandType::SUPPORT_FORMATS:
        return std::make_unique<CommandSupportFormats>();
    case CommandType::SUPPORT_FORMATS_RESPONSE:
        return std::make_unique<CommandSupportFormatsResponse>();
    default:
        assert(false);
        return nullptr;
    }
}

std::string CommandShutDown::serialize() const
{
    return "";
}

bool CommandShutDown::unserialize(const std::string& /*data*/)
{
    return true;
}

std::string CommandShutDownResponse::serialize() const
{
    return "";
}

bool CommandShutDownResponse::unserialize(const std::string& /*data*/)
{
    return true;
}

std::string CommandNewGfx::serialize() const
{
    std::string toret;
    CacheableWriter writer(toret);
    writer.serializestring_u32(Task.Path);
    GfxSerializationHelper::serialize(writer, Task.Dimensions);
    return toret;
}

bool CommandNewGfx::unserialize(const std::string& data)
{
    CacheableReader reader(data);
    // path
    if (!reader.unserializestring_u32(Task.Path))
    {
        return false;
    }
    // dimensions
    if (!GfxSerializationHelper::unserialize(reader, Task.Dimensions))
    {
        return false;
    }
    // empty dimensions considered an invalid task
    if (Task.Dimensions.size() == 0)
    {
        return false;
    }
    return true;
}

std::string CommandNewGfxResponse::serialize() const
{
    std::string toret;
    CacheableWriter writer(toret);
    writer.serializeu32(ErrorCode);
    writer.serializestring_u32(ErrorText);
    GfxSerializationHelper::serialize(writer, Images);
    return toret;
}

bool CommandNewGfxResponse::unserialize(const std::string& data)
{
    CacheableReader reader(data);
    // ErrorCode
    if (!reader.unserializeu32(ErrorCode))
    {
        return false;
    }
    // ErrorText
    if (!reader.unserializestring_u32(ErrorText))
    {
        return false;
    }
    // images
    if (!GfxSerializationHelper::unserialize(reader, Images))
    {
        return false;
    }
    return true;
}

std::string CommandHello::serialize() const
{
    std::string toret;
    CacheableWriter writer(toret);
    writer.serializestring_u32(Text);
    return toret;
}

bool CommandHello::unserialize(const std::string& data)
{
    CacheableReader reader(data);
    // Text
    if (!reader.unserializestring_u32(Text))
    {
        return false;
    }
    return true;
}

std::string CommandHelloResponse::serialize() const
{
    std::string toret;
    CacheableWriter writer(toret);
    writer.serializestring_u32(Text);
    return toret;
}

bool CommandHelloResponse::unserialize(const std::string& data)
{
    CacheableReader reader(data);
    // Text
    if (!reader.unserializestring_u32(Text))
    {
        return false;
    }
    return true;
}

std::string CommandSupportFormats::serialize() const
{
    return "";
}

bool CommandSupportFormats::unserialize(const std::string& /*data*/)
{
    return true;
}

std::string CommandSupportFormatsResponse::serialize() const
{
    std::string toret;
    CacheableWriter writer(toret);
    writer.serializestring_u32(formats);
    writer.serializestring_u32(videoformats);
    return toret;
}

bool CommandSupportFormatsResponse::unserialize(const std::string& data)
{
    CacheableReader reader(data);
    // formats
    if (!reader.unserializestring_u32(formats))
    {
        return false;
    }
    // videoformats
    if (!reader.unserializestring_u32(videoformats))
    {
        return false;
    }
    return true;
}
}
}