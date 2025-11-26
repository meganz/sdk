/**
 * (c) 2025 by Mega Limited, New Zealand
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

#include "mega/command.h"
#include "mega/json.h"
#include "mega/types.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace mega;

// Keep in mind, this unit test is added after the JSONSplitter has served well for a long time.
// So this test is for the supported scenarios, and new features, not for the edge cases.
// Because JSONSplitter is intended for specific scenarios like streaming parsing the well formed
// JSONs from API Server. But it's harmless to record those unexpected cases.
// 1. Numbers: {"int": 123, "float": 3.14, "negative": -123}
// 2. Booleans and Null values: {"bool": true, "null": null}
// 3. Arrays: ["a", "b", "c"]
// 4. Spaces:
//        before the end of first chunk: R"({"key1": "value1", )" and R"("key2": "value2"})"
//        before the number:{"err": -1}
//        before the string: {"key": "value"}

class JSONSplitterTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        splitter.clear();
        filters.clear();
    }

    void TearDown() override
    {
        // Cleanup
    }

    JSONSplitter splitter;
    std::map<std::string, std::function<JSONSplitter::CallbackResult(JSON*)>> filters;
    std::string callbackData;
    bool callbackResult = true;

    // Helper function to create a simple callback that records data
    std::function<JSONSplitter::CallbackResult(JSON*)> createCallbackWithString(std::string& output)
    {
        return [&output](JSON* json) -> JSONSplitter::CallbackResult
        {
            if (json && json->pos)
            {
                return json->storeobject(&output) ? JSONSplitter::CallbackResult::SPLITTER_SUCCESS :
                                                    JSONSplitter::CallbackResult::SPLITTER_ERROR;
            }
            return JSONSplitter::CallbackResult::SPLITTER_ERROR;
        };
    }

    // Helper function to create a simple callback that records data in vector
    std::function<JSONSplitter::CallbackResult(JSON*)>
        createCallbackWithVector(std::vector<std::string>& output)

    {
        return [&output](JSON* json) -> JSONSplitter::CallbackResult
        {
            if (json && json->pos)
            {
                std::string temp;
                if (json->storeobject(&temp))
                {
                    output.emplace_back(temp);
                    return JSONSplitter::CallbackResult::SPLITTER_SUCCESS;
                }
            }
            return JSONSplitter::CallbackResult::SPLITTER_ERROR;
        };
    }
};

class TestCommand: public Command
{
public:
    bool procresult(Result, JSON&) override
    {
        return true;
    }
};

TEST_F(JSONSplitterTest, ConstructorAndInitialState)
{
    JSONSplitter newSplitter;

    EXPECT_TRUE(newSplitter.isStarting());
    EXPECT_FALSE(newSplitter.hasFinished());
    EXPECT_FALSE(newSplitter.hasFailed());
}

TEST_F(JSONSplitterTest, ClearResetsState)
{
    // Simple test data
    std::string testJson = R"({"test": "value"})";
    splitter.processChunk(nullptr, testJson.c_str());

    // Clear and verify state is reset
    splitter.clear();
    EXPECT_TRUE(splitter.isStarting());
    EXPECT_FALSE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
}

TEST_F(JSONSplitterTest, ProcessErrorResponse)
{
    std::string testJson = R"({"err":-1})";
    Error err;

    filters["#"] = [&err](JSON* json) -> JSONSplitter::CallbackResult
    {
        if (json && json->pos)
        {
            TestCommand cmd;
            return cmd.checkError(err, *json) ? JSONSplitter::CallbackResult::SPLITTER_SUCCESS :
                                                JSONSplitter::CallbackResult::SPLITTER_ERROR;
        }

        return JSONSplitter::CallbackResult::SPLITTER_ERROR;
    };

    auto consumed = splitter.processChunk(&filters, testJson.c_str());

    EXPECT_EQ(consumed, static_cast<m_off_t>(testJson.length()));
    EXPECT_TRUE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_EQ(ErrorCodes::API_EINTERNAL, err);
}

TEST_F(JSONSplitterTest, ProcessSimpleObjectNumber)
{
    std::string testJson = "-1,"; // Can not be a number without any suffix?
    m_off_t capturedData;

    filters["#"] = [&capturedData](JSON* json) -> JSONSplitter::CallbackResult
    {
        if (json && json->pos)
        {
            capturedData = json->getint();
        }

        return JSONSplitter::CallbackResult::SPLITTER_SUCCESS;
    };

    auto consumed = splitter.processChunk(&filters, testJson.c_str());
    EXPECT_EQ(consumed, static_cast<m_off_t>(testJson.length() - 1));
    EXPECT_TRUE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_EQ(-1, capturedData);
}

TEST_F(JSONSplitterTest, ProcessSimpleObjectString)
{
    std::string testJson = R"({"key": "value"})";
    std::string capturedData;

    filters["{\"key"] = createCallbackWithString(capturedData);
    auto consumed = splitter.processChunk(&filters, testJson.c_str());

    EXPECT_EQ(consumed, static_cast<m_off_t>(testJson.length()));
    EXPECT_TRUE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_EQ("value", capturedData);
}

TEST_F(JSONSplitterTest, ProcessNestedObject)
{
    std::string testJson = R"({"outer": {"inner": "value"}})";
    std::string capturedData;

    // Why {{outer\"inner" :
    // first { is from the wrapper of the JSON object
    // second { is part of the "outer", it means the value for field "outer" is a JSON, so push
    // {outer together into the stack the \" before "inner" is also a separator of the path, not
    // part of the name "inner", it means the value for filed "inner" is a string
    filters["{{outer\"inner"] = createCallbackWithString(capturedData);
    auto consumed = splitter.processChunk(&filters, testJson.c_str());

    EXPECT_EQ(consumed, static_cast<m_off_t>(testJson.length()));
    EXPECT_TRUE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_EQ(capturedData, "value");
}

TEST_F(JSONSplitterTest, ProcessStringWithEscapes)
{
    std::string testJson = R"({"escaped": "quote:\" new line:\n tab:\t"})";
    std::string capturedData;

    filters["{\"escaped"] = createCallbackWithString(capturedData);

    auto consumed = splitter.processChunk(&filters, testJson.c_str());
    EXPECT_EQ(consumed, static_cast<m_off_t>(testJson.length()));
    EXPECT_TRUE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_EQ(capturedData, R"(quote:\" new line:\n tab:\t)");
}

TEST_F(JSONSplitterTest, ProcessChunkedData)
{
    // Split JSON across mutiple chunks
    std::string chunk1 = R"({"key1": "value1",)";
    std::string chunk2 = R"("key2": "value2"})";

    std::vector<std::string> capturedData;
    filters["{\"key1"] = createCallbackWithVector(capturedData);
    filters["{\"key2"] = createCallbackWithVector(capturedData);

    auto consumed1 = splitter.processChunk(&filters, chunk1.c_str());
    EXPECT_EQ(consumed1, static_cast<m_off_t>(chunk1.length()));
    EXPECT_FALSE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());

    // Must purge consumed bytes
    // if passing chunk1 + chunk2 here, splitter won't work as expected
    auto consumed2 = splitter.processChunk(&filters, chunk2.c_str());
    EXPECT_EQ(consumed2, static_cast<m_off_t>(chunk2.length()));
    EXPECT_TRUE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_THAT(capturedData, testing::ElementsAre("value1", "value2"));
}

TEST_F(JSONSplitterTest, ProcessArrayWithStarters)
{
    std::string testJson = R"({"a":[{"a": "d", "i": "abc"}, {"a": "x", "sn": "xyz"}]})";
    std::vector<std::string> capturedData;

    filters["{[a{\"a"] = createCallbackWithVector(capturedData);
    auto consumed = splitter.processChunk(&filters, testJson.c_str());

    EXPECT_EQ(consumed, static_cast<m_off_t>(testJson.length()));
    EXPECT_TRUE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_THAT(capturedData, testing::ElementsAre("d", "x"));
}

TEST_F(JSONSplitterTest, ProcessChunkWithPauseFromStart)
{
    std::string testJson = R"({"a":[{"a": "d", "i": "abc"}, {"a": "x", "sn": "xyz"}]})";
    bool first = true;
    std::vector<std::string> capturedData;

    filters["{[a{\"a"] = [&first, &capturedData](JSON* json) -> JSONSplitter::CallbackResult
    {
        if (first)
        {
            first = false;
            return JSONSplitter::CallbackResult::SPLITTER_PAUSE;
        }

        else
        {
            std::string output;
            json->storeobject(&output);
            capturedData.emplace_back(output);
        }
        return JSONSplitter::CallbackResult::SPLITTER_SUCCESS;
    };

    auto consumed = splitter.processChunk(&filters, testJson.c_str());
    EXPECT_EQ(consumed, 0);
    EXPECT_FALSE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_EQ(0, capturedData.size());

    // No need to purge because the consumed length is 0

    consumed = splitter.processChunk(&filters, testJson.c_str());
    EXPECT_EQ(consumed, static_cast<m_off_t>(testJson.length()));
    EXPECT_TRUE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_THAT(capturedData, testing::ElementsAre("d", "x"));
}

TEST_F(JSONSplitterTest, ProcessChunkWithPauseFromMiddle)
{
    std::string testJson = R"({"a":[{"a": "d", "i": "abc"}, {"b": "x", "sn": "xyz"}]})";
    bool first = true;
    std::vector<std::string> capturedData;

    filters["{[a{\"a"] = [&capturedData](JSON* json) -> JSONSplitter::CallbackResult
    {
        std::string output;
        json->storeobject(&output);
        capturedData.emplace_back(output);
        return JSONSplitter::CallbackResult::SPLITTER_SUCCESS;
    };

    filters["{[a{\"b"] = [&first, &capturedData](JSON* json) -> JSONSplitter::CallbackResult
    {
        if (first)
        {
            first = false;
            return JSONSplitter::CallbackResult::SPLITTER_PAUSE;
        }

        else
        {
            std::string output;
            json->storeobject(&output);
            capturedData.emplace_back(output);
        }
        return JSONSplitter::CallbackResult::SPLITTER_SUCCESS;
    };

    auto consumed = splitter.processChunk(&filters, testJson.c_str());
    EXPECT_EQ(consumed, 16); // {"a":[{"a": "d",
    EXPECT_FALSE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_THAT(capturedData, testing::ElementsAre("d"));

    // Must purge consumed bytes before next call
    testJson.erase(0, static_cast<size_t>(consumed));

    consumed = splitter.processChunk(&filters, testJson.c_str());
    EXPECT_EQ(consumed, static_cast<m_off_t>(testJson.length()));
    EXPECT_TRUE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_THAT(capturedData, testing::ElementsAre("d", "x"));
}

TEST_F(JSONSplitterTest, ProcessChunkWithPauseAtObjectClosure)
{
    // Note: Do not add space in the JSON string, e.g. after the ":"
    std::string testJson = R"({"a":[{"a":"d","i":"abc"},{"a":"x","sn":"xyz"}]})";
    bool firstObject = true;
    std::vector<std::string> capturedObjects;

    // Filter for the array of objects - pause at first object closure
    filters["{[a{"] = [&firstObject, &capturedObjects](JSON* json) -> JSONSplitter::CallbackResult
    {
        if (firstObject)
        {
            firstObject = false;
            return JSONSplitter::CallbackResult::SPLITTER_PAUSE;
        }

        else
        {
            std::string output;
            json->storeobject(&output);
            capturedObjects.emplace_back(output);
        }
        return JSONSplitter::CallbackResult::SPLITTER_SUCCESS;
    };

    auto consumed = splitter.processChunk(&filters, testJson.c_str());
    EXPECT_EQ(consumed, 6); // Consumed up to before the first object: {"a":[
    EXPECT_FALSE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_EQ(0, capturedObjects.size());

    // Must purge consumed bytes before next call
    testJson.erase(0, static_cast<size_t>(consumed));

    // Second call should process the first object and pause at second object closure
    consumed = splitter.processChunk(&filters, testJson.c_str());
    EXPECT_EQ(consumed, static_cast<m_off_t>(testJson.length()));
    EXPECT_TRUE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_THAT(capturedObjects,
                testing::ElementsAre(R"({"a":"d","i":"abc"})", R"({"a":"x","sn":"xyz"})"));
}

TEST_F(JSONSplitterTest, ProcessChunkWithMultiplePauseAtStringValue)
{
    std::string testJson = R"({"key1":"value1", "key2":"value2", "key3":"value3"})";
    int callCount = 0;
    std::vector<std::string> capturedValues;

    // Filter for string values - pause at first value
    filters["{\"key1"] = [&callCount, &capturedValues](JSON* json) -> JSONSplitter::CallbackResult
    {
        callCount++;
        if (callCount <= 2)
        {
            return JSONSplitter::CallbackResult::SPLITTER_PAUSE;
        }

        std::string output;
        json->storeobject(&output);
        capturedValues.emplace_back(output);
        return JSONSplitter::CallbackResult::SPLITTER_SUCCESS;
    };

    filters["{\"key2"] = [&capturedValues](JSON* json) -> JSONSplitter::CallbackResult
    {
        std::string output;
        json->storeobject(&output);
        capturedValues.emplace_back(output);
        return JSONSplitter::CallbackResult::SPLITTER_SUCCESS;
    };

    filters["{\"key3"] = [&capturedValues](JSON* json) -> JSONSplitter::CallbackResult
    {
        std::string output;
        json->storeobject(&output);
        capturedValues.emplace_back(output);
        return JSONSplitter::CallbackResult::SPLITTER_SUCCESS;
    };

    // First call should pause at key1's string value
    auto consumed = splitter.processChunk(&filters, testJson.c_str());
    EXPECT_EQ(consumed, 0);
    EXPECT_FALSE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_EQ(0, capturedValues.size());

    // Second call should pause at key1's string value
    consumed = splitter.processChunk(&filters, testJson.c_str());
    EXPECT_EQ(consumed, 0);
    EXPECT_FALSE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_EQ(0, capturedValues.size());

    // Second call should process all remaining values
    consumed = splitter.processChunk(&filters, testJson.c_str());
    EXPECT_EQ(consumed, static_cast<m_off_t>(testJson.length()));
    EXPECT_TRUE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_THAT(capturedValues, testing::ElementsAre("value1", "value2", "value3"));
}

TEST_F(JSONSplitterTest, ProcessChunkWithPauseAtErrorNumber)
{
    std::string testJson = R"({"err":-1})";
    int callCount = 0;
    Error err = ErrorCodes::API_OK;

    // Filter for error response (just a number)
    filters["#"] = [&callCount, &err](JSON* json) -> JSONSplitter::CallbackResult
    {
        callCount++;
        if (callCount == 1)
        {
            return JSONSplitter::CallbackResult::SPLITTER_PAUSE;
        }

        if (json && json->pos)
        {
            TestCommand cmd;
            return cmd.checkError(err, *json) ? JSONSplitter::CallbackResult::SPLITTER_SUCCESS :
                                                JSONSplitter::CallbackResult::SPLITTER_ERROR;
        }
        return JSONSplitter::CallbackResult::SPLITTER_SUCCESS;
    };

    // First call should pause at the error number
    auto consumed = splitter.processChunk(&filters, testJson.c_str());
    EXPECT_EQ(consumed, 0); // No data consumed before the number
    EXPECT_FALSE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_EQ(ErrorCodes::API_OK, err);

    // Second call should process the error number
    consumed = splitter.processChunk(&filters, testJson.c_str());
    EXPECT_EQ(consumed, static_cast<m_off_t>(testJson.length()));
    EXPECT_TRUE(splitter.hasFinished());
    EXPECT_FALSE(splitter.hasFailed());
    EXPECT_EQ(ErrorCodes::API_EINTERNAL, err);
}
