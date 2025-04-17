/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
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
        mega::SimpleLogger::setOutputClass(this);
    }

    ~MockLogger()
    {
        mega::SimpleLogger::setOutputClass(nullptr);
    }

    void log(const char* time,
             int loglevel,
             const char* source,
             const char* message,
             const char** directMessages,
             size_t* directMessagesSizes,
             unsigned numberMessages) override
    {
        EXPECT_EQ(nullptr, time);
        EXPECT_EQ(nullptr, source);

        if (directMessages)
        {
            for (unsigned i = 0; i < numberMessages; i++)
            {
                mLogLevel.insert(loglevel);
                mMessage.push_back(std::string{directMessages[i], directMessagesSizes[i]});
            }
        }
        else if (message)
        {
            mLogLevel.insert(loglevel);
            mMessage.push_back(message);
        }
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
    return message + " ["+file + ":" + std::to_string(line) + "]";
}
}

TEST(Logging, performanceMode_OneDirectMessage)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const std::string message = "some message";
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line}
            << mega::DirectMessage{message.data()};
        logger.checkLogLevel(level);
        ASSERT_EQ(2, logger.mMessage.size());
        ASSERT_EQ(message, logger.mMessage[0]);
        ASSERT_EQ(" [file.cpp:13]", logger.mMessage[1]);
    }
}

TEST(Logging, performanceMode_MultipleDirectMessages)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const std::string message1 = "some message 1";
        const std::string message2 = "some message 2";
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line}
            << mega::DirectMessage{message1.data()} << mega::DirectMessage{message2.data()};
        logger.checkLogLevel(level);
        ASSERT_EQ(3, logger.mMessage.size());
        ASSERT_EQ(message1, logger.mMessage[0]);
        ASSERT_EQ(message2, logger.mMessage[1]);
        ASSERT_EQ(" [file.cpp:13]", logger.mMessage[2]);
    }
}

TEST(Logging, performanceMode_StringsChained)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const std::string message1 = "some message 1";
        const std::string message2 = "some message 2";
        auto chainedF = [&]()
        {
            mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), 14, true}
                << message2;
            return message1;
        };
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << message1
                                                                                   << chainedF();
        ASSERT_EQ(2, logger.mMessage.size());
        ASSERT_EQ(message1 + message2 + " [file.cpp:14]", logger.mMessage[0]);
        ASSERT_EQ(message1 + " [file.cpp:13]", logger.mMessage[1]);
    }
}

TEST(Logging, performanceMode_DirectMessagesChained)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const std::string message1 = "some message 1";
        const std::string message2 = "some message 2";
        auto chainedF = [&]()
        {
            mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), 14, true}
                << mega::DirectMessage{message2.data()};
            return message1;
        };
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line}
            << mega::DirectMessage{message1.data()} << chainedF().data();
        ASSERT_EQ(4, logger.mMessage.size());
        ASSERT_EQ(message1, logger.mMessage[0]);
        ASSERT_EQ(message2, logger.mMessage[1]);
        ASSERT_EQ(" [file.cpp:14]", logger.mMessage[2]);
        ASSERT_EQ(message1 + " [file.cpp:13]", logger.mMessage[3]);
    }
}

TEST(Logging, performanceMode_forStdString)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
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

TEST(Logging, performanceMode_forCString)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
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

TEST(Logging, performanceMode_forEnum)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
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

TEST(Logging, performanceMode_forPointer)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
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

TEST(Logging, performanceMode_forNullPointer)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
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
void test_forIntegerNumber(const Type number)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << number;
        logger.checkLogLevel(level);
        EXPECT_EQ(1, logger.mMessage.size());
        std::ostringstream expected;
        expected << number;
        EXPECT_EQ(expMsg(file, line, expected.str()), logger.mMessage[0]);
    }
}

template<typename Type>
void test_forFloatingNumber(const Type number)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << number;
        logger.checkLogLevel(level);
        EXPECT_EQ(1, logger.mMessage.size());
        std::ostringstream expected;
        expected << number;
        const auto msg = expMsg(file, line, expected.str());
        EXPECT_NE(logger.mMessage[0].find(msg), std::string::npos);
    }
}

}

TEST(Logging, performanceMode_forInt)
{
    test_forIntegerNumber<int>(0);
    test_forIntegerNumber<int>(42);
    test_forIntegerNumber<int>(-42);
    test_forIntegerNumber<int>(std::numeric_limits<int>::lowest());
    test_forIntegerNumber<int>(std::numeric_limits<int>::max());
}

TEST(Logging, performanceMode_forLong)
{
    test_forIntegerNumber<long>(0);
    test_forIntegerNumber<long>(42);
    test_forIntegerNumber<long>(-42);
    test_forIntegerNumber<long>(std::numeric_limits<long>::lowest());
    test_forIntegerNumber<long>(std::numeric_limits<long>::max());
}

TEST(Logging, performanceMode_forLongLong)
{
    test_forIntegerNumber<long long>(0);
    test_forIntegerNumber<long long>(42);
    test_forIntegerNumber<long long>(-42);
    test_forIntegerNumber<long long>(std::numeric_limits<long long>::lowest());
    test_forIntegerNumber<long long>(std::numeric_limits<long long>::max());
}

TEST(Logging, performanceMode_forUnsignedInt)
{
    test_forIntegerNumber<unsigned int>(0);
    test_forIntegerNumber<unsigned int>(42);
    test_forIntegerNumber<unsigned int>(std::numeric_limits<unsigned int>::lowest());
    test_forIntegerNumber<unsigned int>(std::numeric_limits<unsigned int>::max());
}

TEST(Logging, performanceMode_forUnsignedLong)
{
    test_forIntegerNumber<unsigned long>(0);
    test_forIntegerNumber<unsigned long>(42);
    test_forIntegerNumber<unsigned long>(std::numeric_limits<unsigned long>::lowest());
    test_forIntegerNumber<unsigned long>(std::numeric_limits<unsigned long>::max());
}

TEST(Logging, performanceMode_forUnsignedLongLong)
{
    test_forIntegerNumber<unsigned long long>(0);
    test_forIntegerNumber<unsigned long long>(42);
    test_forIntegerNumber<unsigned long long>(std::numeric_limits<unsigned long long>::lowest());
    test_forIntegerNumber<unsigned long long>(std::numeric_limits<unsigned long long>::max());
}

TEST(Logging, performanceMode_forFloat)
{
    test_forFloatingNumber<float>(0.f);
    test_forFloatingNumber<float>(42.123f);
    test_forFloatingNumber<float>(-42.123f);
    test_forFloatingNumber<float>(std::numeric_limits<float>::lowest());
    test_forFloatingNumber<float>(std::numeric_limits<float>::min());
    test_forFloatingNumber<float>(std::numeric_limits<float>::max());
}

TEST(Logging, performanceMode_forDouble)
{
    test_forFloatingNumber<double>(0.);
    test_forFloatingNumber<double>(42.123);
    test_forFloatingNumber<double>(-42.123);
    test_forFloatingNumber<double>(std::numeric_limits<double>::lowest());
    test_forFloatingNumber<double>(std::numeric_limits<double>::min());
    test_forFloatingNumber<double>(std::numeric_limits<double>::max());
}

TEST(Logging, performanceMode_withMessageLargeThanLogBuffer)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const std::string firstMessage(mega::LOGGER_CHUNKS_SIZE - file.size() - 5,
                                       'X'); // 5 = ':13 ' plus null terminator
        const std::string secondMessage = "yay";
        const std::string message = firstMessage + secondMessage;
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << message;
        logger.checkLogLevel(level);
        ASSERT_EQ(2, logger.mMessage.size());
        ASSERT_EQ(expMsg(file, line, message).substr(0, mega::LOGGER_CHUNKS_SIZE - 1),
                  logger.mMessage[0]);
        ASSERT_EQ(expMsg(file, line, message).substr(mega::LOGGER_CHUNKS_SIZE - 1),
                  logger.mMessage[1]);
    }
}

TEST(Logging, performanceMode_withHugeMessage)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        const std::string file = "file.cpp";
        const int line = 13;
        const std::string message(5000, 'X');
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << message;
        logger.checkLogLevel(level);

        const int totallength = 5000 + strlen(" [file.cpp:13]") + 1;
        const size_t fullMsgCount = totallength / (mega::LOGGER_CHUNKS_SIZE - 1);
        ASSERT_EQ(fullMsgCount + 1, logger.mMessage.size());
        ASSERT_EQ(totallength % (mega::LOGGER_CHUNKS_SIZE - 1) - 1, logger.mMessage.back().size());
    }
}

TEST(Logging, performanceMode_withHugeMessage_butNoLogger)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        mega::SimpleLogger::setOutputClass(nullptr);
        const std::string file = "file.cpp";
        const int line = 13;
        const std::string message(5000, 'X');
        mega::SimpleLogger{static_cast<mega::LogLevel>(level), file.c_str(), line} << message;
        // ensure no crash or other funny business
    }
}

#else

namespace {

class MockLogger : public mega::Logger
{
public:

    MockLogger()
    {
        mega::SimpleLogger::setOutputClass(this);
    }

    ~MockLogger()
    {
        mega::SimpleLogger::setOutputClass(nullptr);
    }

    void log(const char *time, int loglevel, const char *source, const char *message) override
    {
        EXPECT_NE(nullptr, time);
        EXPECT_NE(nullptr, source);
        EXPECT_NE(nullptr, message);
        mLogLevel.insert(loglevel);
        mMessage.push_back(message);
    }

    void checkLogLevel(const int expLogLevel) const
    {
        EXPECT_EQ(1u, mLogLevel.size());
        EXPECT_EQ(expLogLevel, *mLogLevel.begin());
    }

    std::vector<std::string> mMessage;

private:
    std::set<int> mLogLevel;
};

}

#endif

TEST(Logging, toStr)
{
    ASSERT_EQ(0, std::strcmp("verbose", mega::SimpleLogger::toStr(mega::LogLevel::logMax)));
    ASSERT_EQ(0, std::strcmp("debug", mega::SimpleLogger::toStr(mega::LogLevel::logDebug)));
    ASSERT_EQ(0, std::strcmp("info", mega::SimpleLogger::toStr(mega::LogLevel::logInfo)));
    ASSERT_EQ(0, std::strcmp("warn", mega::SimpleLogger::toStr(mega::LogLevel::logWarning)));
    ASSERT_EQ(0, std::strcmp("err", mega::SimpleLogger::toStr(mega::LogLevel::logError)));
    ASSERT_EQ(0, std::strcmp("FATAL", mega::SimpleLogger::toStr(mega::LogLevel::logFatal)));
}

#ifdef NDEBUG
TEST(Logging, toStr_withBadLogLevel)
{
    ASSERT_EQ(0, std::strcmp("", mega::SimpleLogger::toStr(static_cast<mega::LogLevel>(42))));
}
#endif

TEST(Logging, macroVerbose)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        mega::SimpleLogger::setLogLevel(static_cast<mega::LogLevel>(level));
        const std::string msg = "foobar";
        LOG_verbose << msg;
        const auto currentLevel = mega::LogLevel::logMax;
        if (level >= currentLevel)
        {
            logger.checkLogLevel(currentLevel);
            ASSERT_EQ(1u, logger.mMessage.size());
            EXPECT_NE(logger.mMessage[0].find(msg), std::string::npos);
        }
        else
        {
            ASSERT_EQ(0u, logger.mMessage.size());
        }
    }
}

TEST(Logging, macroDebug)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        mega::SimpleLogger::setLogLevel(static_cast<mega::LogLevel>(level));
        const std::string msg = "foobar";
        LOG_debug << msg;
        const auto currentLevel = mega::LogLevel::logDebug;
        if (level >= currentLevel)
        {
            logger.checkLogLevel(currentLevel);
            ASSERT_EQ(1u, logger.mMessage.size());
            EXPECT_NE(logger.mMessage[0].find(msg), std::string::npos);
        }
        else
        {
            ASSERT_EQ(0u, logger.mMessage.size());
        }
    }
}

TEST(Logging, macroInfo)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        mega::SimpleLogger::setLogLevel(static_cast<mega::LogLevel>(level));
        const std::string msg = "foobar";
        LOG_info << msg;
        const auto currentLevel = mega::LogLevel::logInfo;
        if (level >= currentLevel)
        {
            logger.checkLogLevel(currentLevel);
            ASSERT_EQ(1u, logger.mMessage.size());
            EXPECT_NE(logger.mMessage[0].find(msg), std::string::npos);
        }
        else
        {
            ASSERT_EQ(0u, logger.mMessage.size());
        }
    }
}

TEST(Logging, macroWarn)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        mega::SimpleLogger::setLogLevel(static_cast<mega::LogLevel>(level));
        const std::string msg = "foobar";
        LOG_warn << msg;
        const auto currentLevel = mega::LogLevel::logWarning;
        if (level >= currentLevel)
        {
            logger.checkLogLevel(currentLevel);
            ASSERT_EQ(1u, logger.mMessage.size());
            EXPECT_NE(logger.mMessage[0].find(msg), std::string::npos);
        }
        else
        {
            ASSERT_EQ(0u, logger.mMessage.size());
        }
    }
}

TEST(Logging, macroErr)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        mega::SimpleLogger::setLogLevel(static_cast<mega::LogLevel>(level));
        const std::string msg = "foobar";
        LOG_err << msg;
        const auto currentLevel = mega::LogLevel::logError;
        if (level >= currentLevel)
        {
            logger.checkLogLevel(currentLevel);
            ASSERT_EQ(1u, logger.mMessage.size());
            EXPECT_NE(logger.mMessage[0].find(msg), std::string::npos);
        }
        else
        {
            ASSERT_EQ(0u, logger.mMessage.size());
        }
    }
}

TEST(Logging, macroFatal)
{
    for (int level = 0; level <= mega::LogLevel::logMax; ++level)
    {
        MockLogger logger;
        mega::SimpleLogger::setLogLevel(static_cast<mega::LogLevel>(level));
        const std::string msg = "foobar";
        LOG_fatal << msg;
        logger.checkLogLevel(mega::LogLevel::logFatal);
        ASSERT_EQ(1u, logger.mMessage.size());
        EXPECT_NE(logger.mMessage[0].find(msg), std::string::npos);
    }
}

TEST(Logging, Extract_file_name_from_full_path)
{
    ASSERT_EQ(0, strcmp(::mega::log_file_leafname(__FILE__), "Logging_test.cpp" ));
    ASSERT_EQ(0, strcmp(::mega::log_file_leafname("logging.h"), "logging.h") );
    ASSERT_EQ(0, strcmp(::mega::log_file_leafname("include/mega/logging.h"), "logging.h"));
    ASSERT_EQ(0, strcmp(::mega::log_file_leafname("include\\mega\\logging.h"), "logging.h" ));
}
