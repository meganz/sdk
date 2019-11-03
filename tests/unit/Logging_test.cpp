/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
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
#include <gtest/gtest.h>

#include <mega/logging.h>

#ifdef ENABLE_LOG_PERFORMANCE
namespace {

class MockLogger : public mega::Logger
{
public:

    MockLogger()
    {
        mega::SimpleLogger::logger = this;
    }

    ~MockLogger()
    {
        mega::SimpleLogger::logger = nullptr;
    }

    void log(const char *time, int loglevel, const char *source, const char *message) override
    {
        EXPECT_EQ(nullptr, time);
        EXPECT_EQ(nullptr, source);
        EXPECT_NE(nullptr, message);
        mLogLevel.insert(loglevel);
        mMessage.push_back(message);
    }

    void checkLogLevel(const int expLogLevel) const
    {
        EXPECT_EQ(1, mLogLevel.size());
        EXPECT_EQ(expLogLevel, *mLogLevel.begin());
    }

    std::vector<std::string> mMessage;

private:
    std::set<int> mLogLevel;
};

std::string expMsg(const std::string& file, const int line, const std::string& message)
{
    return file + ":" + std::to_string(line) + " " + message;
}

}

TEST(Logging, forStdString)
{
    for (int level = 0; level < mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const std::string message = "some message";
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << message;
        logger.checkLogLevel(level);
        ASSERT_EQ(1, logger.mMessage.size());
        ASSERT_EQ(expMsg(file, line, message), logger.mMessage[0]);
    }
}

TEST(Logging, forCString)
{
    for (int level = 0; level < mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const std::string message = "some message";
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << message.c_str();
        logger.checkLogLevel(level);
        ASSERT_EQ(1, logger.mMessage.size());
        ASSERT_EQ(expMsg(file, line, message), logger.mMessage[0]);
    }
}

TEST(Logging, forEnum)
{
    for (int level = 0; level < mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const auto obj = mega::LogLevel::logDebug;
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << obj;
        logger.checkLogLevel(level);
        ASSERT_EQ(1, logger.mMessage.size());
        ASSERT_EQ(expMsg(file, line, "4"), logger.mMessage[0]);
    }
}

TEST(Logging, forPointer)
{
    for (int level = 0; level < mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const double obj = 42;
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << &obj;
        logger.checkLogLevel(level);
        ASSERT_EQ(1, logger.mMessage.size());
        ASSERT_GE(logger.mMessage[0].size(), file.size() + 5); // 5 = ':13 ' plus null terminator
    }
}

TEST(Logging, forNullPointer)
{
    for (int level = 0; level < mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const double* obj = nullptr;
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << obj;
        logger.checkLogLevel(level);
        ASSERT_EQ(1, logger.mMessage.size());
        ASSERT_EQ(expMsg(file, line, "(NULL)"), logger.mMessage[0]);
    }
}

namespace {

template<typename Type>
void test_forIntegerNumber()
{
    for (int level = 0; level < mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const Type obj = 42;
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << obj;
        logger.checkLogLevel(level);
        EXPECT_EQ(1, logger.mMessage.size());
        EXPECT_EQ(expMsg(file, line, "42"), logger.mMessage[0]);
    }
}

template<typename Type>
void test_forFloatingNumber()
{
    for (int level = 0; level < mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const auto obj = static_cast<Type>(42.123);
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << obj;
        logger.checkLogLevel(level);
        EXPECT_EQ(1, logger.mMessage.size());
        const auto msg = expMsg(file, line, "42.123");
        EXPECT_NE(logger.mMessage[0].find(msg), std::string::npos);
    }
}

}

TEST(Logging, forInt)
{
    test_forIntegerNumber<int>();
}

TEST(Logging, forLong)
{
    test_forIntegerNumber<long>();
}

TEST(Logging, forLongLong)
{
    test_forIntegerNumber<long long>();
}

TEST(Logging, forUnsignedInt)
{
    test_forIntegerNumber<unsigned int>();
}

TEST(Logging, forUnsignedLong)
{
    test_forIntegerNumber<unsigned long>();
}

TEST(Logging, forUnsignedLongLong)
{
    test_forIntegerNumber<unsigned long long>();
}

TEST(Logging, forFloat)
{
    test_forFloatingNumber<float>();
}

TEST(Logging, forDouble)
{
    test_forFloatingNumber<double>();
}

TEST(Logging, withMessageLargeThanLogBuffer)
{
    for (int level = 0; level < mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const std::string firstMessage(256 - file.size() - 5, 'X'); // 5 = ':13 ' plus null terminator
        const std::string secondMessage = "yay";
        const std::string message = firstMessage + secondMessage;
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << message;
        logger.checkLogLevel(level);
        ASSERT_EQ(2, logger.mMessage.size());
        ASSERT_EQ(expMsg(file, line, firstMessage), logger.mMessage[0]);
        ASSERT_EQ(secondMessage, logger.mMessage[1]);
    }
}

TEST(Logging, withHugeMessage)
{
    for (int level = 0; level < mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const std::string message(5000, 'X');
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << message;
        logger.checkLogLevel(level);
        const size_t fullMsgCount = 5013 / 255;
        ASSERT_EQ(fullMsgCount + 1, logger.mMessage.size());
        ASSERT_EQ(5013 % 255 - 1, logger.mMessage.back().size());
    }
}

TEST(Logging, withHugeMessage_butNoLogger)
{
    for (int level = 0; level < mega::LogLevel::logMax; ++level)
    {
        mega::SimpleLogger::logger = nullptr;
        const std::string file = "file.cpp";
        const int line = 13;
        const std::string message(5000, 'X');
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << message;
        // ensure no crash or other funny business
    }
}

TEST(Logging, toStr)
{
    ASSERT_EQ(0, std::strcmp("verbose", mega::SimpleLogger::toStr(mega::LogLevel::logMax)));
    ASSERT_EQ(0, std::strcmp("debug", mega::SimpleLogger::toStr(mega::LogLevel::logDebug)));
    ASSERT_EQ(0, std::strcmp("info", mega::SimpleLogger::toStr(mega::LogLevel::logInfo)));
    ASSERT_EQ(0, std::strcmp("warn", mega::SimpleLogger::toStr(mega::LogLevel::logWarning)));
    ASSERT_EQ(0, std::strcmp("err", mega::SimpleLogger::toStr(mega::LogLevel::logError)));
    ASSERT_EQ(0, std::strcmp("FATAL", mega::SimpleLogger::toStr(mega::LogLevel::logFatal)));
}
#endif
