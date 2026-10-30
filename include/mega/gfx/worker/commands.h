/**
 * (c) 2013 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#pragma once

#include "mega/gfx/worker/tasks.h"
#include "mega/log_level.h"

#include <memory>

namespace mega {
namespace gfx {

enum class CommandType
{
    BEGIN                       = 1,
    NEW_GFX                     = 1,
    NEW_GFX_RESPONSE            = 2,
    ABORT                       = 3,
    SHUTDOWN                    = 4,
    SHUTDOWN_RESPONSE           = 5,
    HELLO                       = 6,
    HELLO_RESPONSE              = 7,
    SUPPORT_FORMATS             = 8,
    SUPPORT_FORMATS_RESPONSE    = 9,
    MEDIA_INFO = 10,
    MEDIA_INFO_RESPONSE = 11,
    END = 12 // 1 more than the last valid one
};

class ICommand
{
public:
    virtual ~ICommand() = default;

    virtual CommandType type() const = 0;

    virtual std::string typeStr() const = 0;

    virtual std::string serialize() const = 0;

    virtual bool unserialize(const std::string& data) = 0;

    static std::unique_ptr<ICommand> factory(CommandType type);

    virtual LogLevel logLevel() const
    {
        return logInfo;
    }
};

struct CommandShutDown : public ICommand
{
    CommandType type() const override { return CommandType::SHUTDOWN; }

    std::string typeStr() const override { return "SHUTDOWN"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

struct CommandShutDownResponse : public ICommand
{
    CommandType type() const override { return CommandType::SHUTDOWN_RESPONSE; }

    std::string typeStr() const override { return "SHUTDOWN_RESPONSE"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

struct CommandNewGfx: public ICommand
{
    GfxTask Task;

    CommandType type() const override
    {
        return CommandType::NEW_GFX;
    }

    std::string typeStr() const override
    {
        return "NEW_GFX";
    };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

struct CommandNewGfxResponse : public ICommand
{
    uint32_t    ErrorCode;
    std::string ErrorText;
    std::vector<std::string> Images;

    CommandType type() const override { return CommandType::NEW_GFX_RESPONSE; }

    std::string typeStr() const override { return "NEW_GFX_RESPONSE"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

struct CommandHello : public ICommand
{
    std::string Text;

    CommandType type() const override { return CommandType::HELLO; }

    std::string typeStr() const override { return "HELLO"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;

    LogLevel logLevel() const override
    {
        return logVerbose;
    }
};

struct CommandHelloResponse : public ICommand
{
    std::string Text;

    CommandType type() const override { return CommandType::HELLO_RESPONSE; }

    std::string typeStr() const override { return "HELLO_RESPONSE"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

struct CommandSupportFormats : public ICommand
{
    CommandType type() const override { return CommandType::SUPPORT_FORMATS; }

    std::string typeStr() const override { return "SUPPORT_FORMATS"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;

    LogLevel logLevel() const override
    {
        return logVerbose;
    }
};

struct CommandSupportFormatsResponse : public ICommand
{
    std::string formats;
    std::string videoformats;

    CommandType type() const override { return CommandType::SUPPORT_FORMATS_RESPONSE; }

    std::string typeStr() const override { return "SUPPORT_FORMATS_RESPONSE"; };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

class GfxSerializationHelper
{
    static constexpr size_t MAX_VECT_SIZE = 100;

public:
    static void serialize(CacheableWriter& writer, const GfxDimension& source);
    static void serialize(CacheableWriter& writer, const std::string& source);

    template<typename T>
    static void serialize(CacheableWriter& writer, const std::vector<T>& target)
    {
        auto vecSize = target.size();
        assert(vecSize < std::numeric_limits<uint32_t>::max());
        writer.serializeu32(static_cast<uint32_t>(vecSize));
        for (const auto& entry: target)
        {
            GfxSerializationHelper::serialize(writer, entry);
        }
    }

    static bool unserialize(CacheableReader& reader, GfxDimension& target);
    static bool unserialize(CacheableReader& reader, std::string& target);

    template<typename T>
    static bool unserialize(CacheableReader& reader,
                            std::vector<T>& target,
                            const size_t maxVectSize = MAX_VECT_SIZE)
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
        for (auto& entry: target)
        {
            if (!GfxSerializationHelper::unserialize(reader, entry))
            {
                return false;
            }
        }
        return true;
    }
};

struct CommandMediaInfo: public ICommand
{
    std::string Path;

    CommandType type() const override
    {
        return CommandType::MEDIA_INFO;
    }

    std::string typeStr() const override
    {
        return "MEDIA_INFO";
    };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

struct CommandMediaInfoResponse: public ICommand
{
    uint32_t ErrorCode{};

    MediaProperties MediaInfo;

    CommandType type() const override
    {
        return CommandType::MEDIA_INFO_RESPONSE;
    }

    std::string typeStr() const override
    {
        return "MEDIA_INFO_RESPONSE";
    };

    std::string serialize() const override;

    bool unserialize(const std::string& data) override;
};

} //namespace gfx
} //namespace mega
