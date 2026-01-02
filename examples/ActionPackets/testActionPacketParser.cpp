#include <iostream>
#include <cstring>
#include <chrono>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>

#include "ActionPacketParser.h"

// Test Result Statistics
struct TestStats {
    int passed = 0;
    int failed = 0;
    int total = 0;
};

TestStats stats;

// Test Utility Functions
void runTest(const std::string& testName, std::function<void()> testFunction) {
    std::cout << "\n=== Test: " << testName << " ===" << std::endl;
    try {
        testFunction();
        std::cout << "Test Passed" << std::endl;
        stats.passed++;
    } catch (const std::exception& e) {
        std::cout << "Test Failed: " << e.what() << std::endl;
        stats.failed++;
    } catch (...) {
        std::cout << "Test Failed: Unknown Error" << std::endl;
        stats.failed++;
    }
    stats.total++;
}

// Original testActionPacketParser.cpp Test
void testOriginalActionPacketParser() {
    std::cout << "\n--- Original ActionPacketParser Test ---" << std::endl;
    
    // Simulate network byte stream (multiple actionpackets concatenated, containing large field t)
    const char* mockNetworkData = 
        "{\"f\":[{\"nodeId\":\"123\",\"name\":\"file1\"},{\"nodeId\":\"456\",\"name\""  
        ":\"file2\"}]}\n{\"f\":[{\"nodeId\":\"456\",\"name\":\"file3\"}" 
        "]}\n{\"f\":[{\"nodeId\":\"789\",\"name\":\"file4\"}]}\n" 
        "{\"id\":4,\"f\"";

    // Packet execution callback function (simulate business logic)
    int packetCount = 0;
    ActionPacketParser parser('\n');
    parser.setLargeFieldPath("t");
    parser.setPacketExecCallback([&packetCount](const std::unordered_map<std::string, std::string>& packetData) {
        packetCount++;
        std::cout << "===== Execute Complete ActionPacket =====" << std::endl;
        for (const auto& [key, value] : packetData) {
            std::cout << "Key: " << key << ", Value: " << value << std::endl;
        }
        std::cout << "================================" << std::endl;
    });

    // Simulate network byte stream input (batch input, simulate streaming reception)
    size_t dataLen = strlen(mockNetworkData);
    size_t batchSize = 20; // Batch input, simulate network segment transmission
    for (size_t i = 0; i < dataLen; i += batchSize) {
        size_t currentBatchLen = std::min(batchSize, dataLen - i);
        parser.feed(mockNetworkData + i, currentBatchLen);
    }
    
    if (packetCount != 3) {
        throw std::runtime_error("Original Test Failed: Expected 3 packets, parsed " + std::to_string(packetCount));
    }
}

// Extended Tests from testActionPacketParserExtended.cpp

// Basic Test: Original Test Scenario
void testBasicScenario() {
    std::cout << "\n--- Basic Test Scenario ---" << std::endl;
    
    // Mock network data stream (multiple actionpackets concatenated, containing large field "f")
    const char* mockNetworkData = 
        "{\"id\":1,\"f\":[{\"h\":\"123\",\"p\":\"456\",\"t\":1,\"a\":\"file1.txt\",\"k\":\"key1\"},{\"h\":\"456\",\"p\":\"789\",\"t\":2,\"a\":\"folder1\",\"k\":\"key2\"}]}\n{\"id\":2,\"f\":[{\"h\":\"789\",\"p\":\"456\",\"t\":1,\"a\":\"file3.txt\",\"k\":\"key3\"}"
        "]}\n{\"id\":3,\"f\":[{\"h\":\"abc\",\"p\":\"def\",\"t\":1,\"a\":\"file4.txt\",\"k\":\"key4\"}]}\n";
    
    int packetCount = 0;
    
    // Create parser: delimiter '\n'
    ActionPacketParser parser('\n');
    
    // callback function
    parser.setPacketExecCallback([&packetCount](const std::unordered_map<std::string, std::string>& packetData) {
        packetCount++;
        std::cout << "Packet " << packetCount << " Parsed:" << std::endl;
        for (const auto& [key, value] : packetData) {
            std::cout << "  " << key << ": " << value << std::endl;
        }
    });
    
    // 模拟网络字节流输入（分批输入，模拟流式接收）
    size_t dataLen = strlen(mockNetworkData);
    size_t batchSize = 20; // 分批输入，模拟网络逐段传输
    for (size_t i = 0; i < dataLen; i += batchSize) {
        size_t currentBatchLen = std::min(batchSize, dataLen - i);
        parser.feed(mockNetworkData + i, currentBatchLen);
    }
    
    if (packetCount != 3) {
        throw std::runtime_error("Basic Test Failed: Expected 3 packets, parsed " + std::to_string(packetCount));
    }
}

// Boundary Condition Test 1: Single Character Input
void testSingleCharInput() {
    std::cout << "\n--- Boundary Condition Test: Single Character Input ---" << std::endl;
    
    const char* mockNetworkData = "{\"id\":1,\"f\":[{\"h\":\"123\",\"p\":\"456\",\"t\":1}]}\n";
    
    int packetCount = 0;
    
    ActionPacketParser parser('\n');
    parser.setPacketExecCallback([&packetCount](const std::unordered_map<std::string, std::string>& packetData) {
        packetCount++;
    });
    
    // Character by character input
    size_t dataLen = strlen(mockNetworkData);
    for (size_t i = 0; i < dataLen; i++) {
        parser.feed(mockNetworkData + i, 1);
    }
    
    if (packetCount != 1) {
        throw std::runtime_error("Single Character Input Test Failed: Expected 1 packet, parsed " + std::to_string(packetCount));
    }
}

// Boundary Condition Test 2: Empty Data Input
void testEmptyDataInput() {
    std::cout << "\n--- Boundary Condition Test: Empty Data Input ---" << std::endl;
    
    ActionPacketParser parser('\n');
    
    // Test null pointer and zero-length input
    parser.feed(nullptr, 0);
    parser.feed("", 0);
    
    // Test passes if no exception is thrown
    std::cout << "Empty Data Input Test Passed" << std::endl;
}

// Boundary Condition Test 3: Partial Data Input
void testPartialDataInput() {
    std::cout << "\n--- Boundary Condition Test: Partial Data Input ---" << std::endl;
    
    const char* mockNetworkData = "{\"id\":1,\"f\":[{\"h\":\"123\",\"p\":\"456\"}]}\n{\"id\":2,\"f\":[{\"h\":\"456\",\"p\":\"789\"}]}\n";
    
    int packetCount = 0;
    
    ActionPacketParser parser('\n');
    parser.setPacketExecCallback([&packetCount](const std::unordered_map<std::string, std::string>& packetData) {
        packetCount++;
    });
    
    // Input only partial data (ensure no complete packet)
    size_t dataLen = strlen(mockNetworkData);
    size_t partialLen = 10; // Input only first 10 characters, insufficient for complete packet
    parser.feed(mockNetworkData, partialLen);
    
    // Should not parse any complete packet
    if (packetCount != 0) {
        throw std::runtime_error("Partial Data Input Test Failed: Expected 0 packets, parsed " + std::to_string(packetCount));
    }
    
    // Input remaining data
    parser.feed(mockNetworkData + partialLen, dataLen - partialLen);
    
    if (packetCount != 2) {
        throw std::runtime_error("Partial Data Input Test Failed: Expected 2 packets, parsed " + std::to_string(packetCount));
    }
}

// Error Handling Test 1: Invalid JSON Format
void testInvalidJson() {
    std::cout << "\n--- Error Handling Test: Invalid JSON Format ---" << std::endl;
    
    const char* invalidJsonData = "{\"id\":1,\"f\":[{\"h\":\"123\"},}{\"id\":2,\"f\":[{\"h\":\"456\"}]}\n";
    
    int packetCount = 0;
    
    ActionPacketParser parser('\n');
    parser.setPacketExecCallback([&packetCount](const std::unordered_map<std::string, std::string>& packetData) {
        packetCount++;
    });
    
    // Input invalid JSON data
    size_t dataLen = strlen(invalidJsonData);
    parser.feed(invalidJsonData, dataLen);
    
    // Should be able to handle partially valid data
    std::cout << "Invalid JSON Test: Parsed " << packetCount << " valid packets" << std::endl;
}

// Error Handling Test 2: Missing Large Field
void testMissingLargeField() {
    std::cout << "\n--- Error Handling Test: Missing Large Field ---" << std::endl;
    
    const char* dataWithoutLargeField = 
        "{\"id\":1,\"name\":\"test\"}\n{\"id\":2,\"message\":\"hello\"}\n";
    
    int packetCount = 0;
    
    ActionPacketParser parser('\n');
    parser.setPacketExecCallback([&packetCount](const std::unordered_map<std::string, std::string>& packetData) {
        packetCount++;
    });
    
    size_t dataLen = strlen(dataWithoutLargeField);
    parser.feed(dataWithoutLargeField, dataLen);
    
    if (packetCount != 2) {
        throw std::runtime_error("Missing Large Field Test Failed: Expected 2 packets, parsed " + std::to_string(packetCount));
    }
}

// Function Extension Test 1: Different Delimiter
void testDifferentDelimiter() {
    std::cout << "\n--- Function Extension Test: Different Delimiter ---" << std::endl;
    
    // Use '|' as delimiter
    const char* dataWithPipeDelimiter = 
        "{\"f\":[{\"h\":\"123\",\"p\":\"456\"}]}|{\"f\":[{\"h\":\"456\",\"p\":\"789\"}]}|";
    
    int packetCount = 0;
    
    ActionPacketParser parser('|');
    parser.setPacketExecCallback([&packetCount](const std::unordered_map<std::string, std::string>& packetData) {
        packetCount++;
    });
    
    size_t dataLen = strlen(dataWithPipeDelimiter);
    parser.feed(dataWithPipeDelimiter, dataLen);
    
    if (packetCount != 2) {
        throw std::runtime_error("Different Delimiter Test Failed: Expected 2 packets, parsed " + std::to_string(packetCount));
    }
}

// Function Extension Test 2: Different Large Field Name
void testDifferentLargeField() {
    std::cout << "\n--- Function Extension Test: Different Large Field Name ---" << std::endl;
    
    // Use 'items' as large field name
    const char* dataWithDifferentLargeField = 
        "{\"id\":1,\"items\":[{\"h\":\"123\",\"p\":\"456\",\"t\":1,\"a\":\"item1.txt\",\"k\":\"key1\"}]}\n";
    
    int packetCount = 0;
    bool largeFieldProcessed = false;
    
    ActionPacketParser parser('\n');
    parser.setLargeFieldPath("items");
    parser.setPacketExecCallback([&packetCount, &largeFieldProcessed](const std::unordered_map<std::string, std::string>& packetData) {
        packetCount++;
        for (const auto &item: packetData) {
            std::cout << item.first << ": " << item.second << std::endl;
        }
        if (packetData.find("items") != packetData.end()) {
            largeFieldProcessed = true;
        }
    });
    
    size_t dataLen = strlen(dataWithDifferentLargeField);
    parser.feed(dataWithDifferentLargeField, dataLen);
    
    if (packetCount != 1) {
        throw std::runtime_error("Different Large Field Name Test Failed: Expected 1 packet, parsed " + std::to_string(packetCount));
    }
    
    if (!largeFieldProcessed) {
        throw std::runtime_error("Different Large Field Name Test Failed: Large field not properly processed");
    }
}

// Function Extension Test 3: Multiple Large Fields
void testMultipleLargeFields() {
    std::cout << "\n--- Function Extension Test: Multiple Large Fields ---" << std::endl;
    
    // Note: Current implementation only supports one large field, but we can test multiple large field names
    const char* dataWithMultipleLargeFields = 
        "{\"id\":1,\"f\":[{\"h\":\"123\",\"p\":\"456\"}],\"u\":[{\"userId\":\"456\"}]}\n";
    
    int packetCount = 0;
    
    ActionPacketParser parser('\n');
    parser.setLargeFieldPath("f");
    parser.setPacketExecCallback([&packetCount](const std::unordered_map<std::string, std::string>& packetData) {
        packetCount++;
    });
    
    size_t dataLen = strlen(dataWithMultipleLargeFields);
    parser.feed(dataWithMultipleLargeFields, dataLen);
    
    if (packetCount != 1) {
        throw std::runtime_error("Multiple Large Fields Test Failed: Expected 1 packet, parsed " + std::to_string(packetCount));
    }
}

// Function Extension Test 4: JSON with Escape Characters
void testJsonWithEscapeCharacters() {
    std::cout << "\n--- Function Extension Test: JSON with Escape Characters ---" << std::endl;
    
    // JSON with escape characters
    const char* dataWithEscapes = 
        "{\"id\":1,\"f\":[{\"h\":\"123\",\"p\":\"456\",\"t\":1,\"a\":\"file\\\"name\\\"1.txt\",\"k\":\"key1\"}]}\n";
    
    int packetCount = 0;
    
    ActionPacketParser parser('\n');
    parser.setPacketExecCallback([&packetCount](const std::unordered_map<std::string, std::string>& packetData) {
        packetCount++;
    });
    
    size_t dataLen = strlen(dataWithEscapes);
    parser.feed(dataWithEscapes, dataLen);
    
    if (packetCount != 1) {
        throw std::runtime_error("JSON with Escape Characters Test Failed: Expected 1 packet, parsed " + std::to_string(packetCount));
    }
}

// Performance Test: Large Data Processing
void testPerformance() {
    std::cout << "\n--- Performance Test: Large Data Processing ---" << std::endl;
    
    // Generate a large amount of test data with 'f' response structure
    std::string largeData;
    for (int i = 0; i < 1000; i++) {
        largeData += "{\"id\":" + std::to_string(i) + ",\"f\":[{\"h\":\"" + std::to_string(i) + "\",\"p\":\"" + std::to_string(i+1) + "\",\"t\":1,\"a\":\"file" + std::to_string(i) + ".txt\",\"k\":\"key" + std::to_string(i) + "\"}]}\n";
    }
    
    int packetCount = 0;
    
    ActionPacketParser parser('\n');
    parser.setLargeFieldPath("f");
    parser.setPacketExecCallback([&packetCount](const std::unordered_map<std::string, std::string>& packetData) {
        packetCount++;
    });
    
    // Measure processing time
    auto start = std::chrono::high_resolution_clock::now();
    
    size_t dataLen = largeData.size();
    size_t batchSize = 1000;
    for (size_t i = 0; i < dataLen; i += batchSize) {
        size_t currentBatchLen = std::min(batchSize, dataLen - i);
        parser.feed(largeData.data() + i, currentBatchLen);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "Processing 1000 packets took: " << duration << "ms" << std::endl;
    std::cout << "Average time per packet: " << (duration / 1000.0) << "ms" << std::endl;
    
    if (packetCount != 1000) {
        throw std::runtime_error("Performance Test Failed: Expected 1000 packets, parsed " + std::to_string(packetCount));
    }
}

// Performance Test: Large f Field Data
void testLargeFFieldPerformance() {
    std::cout << "\n--- Performance Test: Large f Field Data ---" << std::endl;
    
    // Generate test data with a large 'f' field containing many nodes
    std::string largeTData = "{\"id\":1,\"f\":[";
    for (int i = 0; i < 1000; i++) {
        if (i > 0) largeTData += ",";
        largeTData += "{\"h\":\"" + std::to_string(i) + "\",\"p\":\"" + std::to_string(i+1) + "\",\"t\":1,\"a\":\"file" + std::to_string(i) + ".txt\",\"k\":\"key" + std::to_string(i) + "\"}";
    }
    largeTData += "]}\n";
    
    int packetCount = 0;
    
    ActionPacketParser parser('\n');
    parser.setLargeFieldPath("f");
    parser.setPacketExecCallback([&packetCount](const std::unordered_map<std::string, std::string>& packetData) {
        packetCount++;
    });
    
    // Measure processing time
    auto start = std::chrono::high_resolution_clock::now();
    
    size_t dataLen = largeTData.size();
    parser.feed(largeTData.data(), dataLen);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "Processing f field with 1000 elements took: " << duration << "ms" << std::endl;
    
    if (packetCount != 1) {
        throw std::runtime_error("Large f Field Test Failed: Expected 1 packet, parsed " + std::to_string(packetCount));
    }
}

// Error Handling Test 3: Deeply Nested JSON
void testDeepNestedJson() {
    std::cout << "\n--- Error Handling Test: Deeply Nested JSON ---" << std::endl;
    
    // Generate deeply nested JSON with 'f' response structure
    std::string deepNestedJson = "{\"f\":[";
    for (int i = 0; i < 100; i++) {
        deepNestedJson += "{\"h\":\"" + std::to_string(i) + ",\"p\":\"" + std::to_string(i+1) + ",\"t\":1,\"a\":\"file" + std::to_string(i) + ".txt\"}";
    }
    deepNestedJson += "]}\n";
    
    int packetCount = 0;
    
    ActionPacketParser parser('\n');
    parser.setLargeFieldPath("f");
    parser.setPacketExecCallback([&packetCount](const std::unordered_map<std::string, std::string>& packetData) {
        packetCount++;
    });
    
    size_t dataLen = deepNestedJson.size();
    parser.feed(deepNestedJson.data(), dataLen);
    
    if (packetCount != 1) {
        throw std::runtime_error("Deeply Nested JSON Test Failed: Expected 1 packet, parsed " + std::to_string(packetCount));
    }
}

// 't' Response Tests from testTResponseParser.cpp
void testTResponseParsing() {
    std::cout << "\n--- 't' Response Parsing Tests ---" << std::endl;
    
    // Statistical variables
    int totalFNodes = 0;
    std::vector<std::string> extractedFNodes;
    int packetsParsed = 0;
    
    // Create parser instance, set large field to 't'
    ActionPacketParser parser('\n');
    parser.setLargeFieldPath("t");
    
    // Set packet execution callback
    parser.setPacketExecCallback([&](const std::unordered_map<std::string, std::string>& packet) {
        packetsParsed++;
        std::cout << "Packet " << packetsParsed << " completed parsing\n";
    });
    
    // Add target node configuration
    parser.addTargetNode(TargetNodeConfig("t", true));
    // Set target node callback (real-time output)
    parser.setTargetNodeCallback([&](const std::string& fNode) {
        totalFNodes++;
        extractedFNodes.push_back(fNode);
        //std::cout << "✓ Extracted 'f' node " << totalFNodes << ": " << fNode << "\n";
    });
    
    // Test helper: Check if callback is properly set
    std::cout << "Callback registered successfully\n";
    
    // Test 1: Basic 't' response parsing
    std::cout << "\n=== Test 1: Basic 't' Response Parsing ===\n";
    // Simplify test data for easier debugging
    std::string tResponse = 
        "{\"id\":1,\"t\":[{\"h\":\"123\",\"p\":\"456\",\"t\":1,\"a\":\"file1.txt\",\"k\":\"key1\"}]}\n";
    
    // Simulate streaming input (character by character)
    for (char c : tResponse) {
        parser.feed(&c, 1);
    }
    
    std::cout << "Expected: 1 'f' nodes extracted\n";
    std::cout << "Actual: " << totalFNodes << " 'f' nodes extracted\n";
    if (totalFNodes != 1) {
        throw std::runtime_error("Test 1 Failed: Expected 1 'f' nodes, extracted " + std::to_string(totalFNodes));
    }
    std::cout << "✅ Test 1 Passed\n";
    
    // Reset statistical variables
    totalFNodes = 0;
    extractedFNodes.clear();
    packetsParsed = 0;
    // Reset parser state to ensure Test 2 is not affected by Test 1
    parser.resetParserState();
    
    // Test 2: Large file 't' response parsing (simulate many 'f' nodes)
    std::cout << "\n=== Test 2: Large 't' Response Parsing ===\n";
    
    // Build 't' response containing 100 'f' nodes
    std::string largeTResponse = "{\"id\":2,\"t\":[";
    for (int i = 0; i < 100; i++) {
        if (i > 0) largeTResponse += ",";
        largeTResponse += 
            std::string("{\"h\":\"file_") + std::to_string(i) + ",\"p\":\"folder_0\",\"t\":1,\"a\":\"file_" + std::to_string(i) + ".txt\",\"k\":\"key_" + std::to_string(i) + "\"}";
    }
    largeTResponse += "]}\n";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Simulate streaming input (larger chunks)
    size_t chunkSize = 32;
    for (size_t i = 0; i < largeTResponse.size(); i += chunkSize) {
        size_t len = std::min(chunkSize, largeTResponse.size() - i);
        parser.feed(largeTResponse.c_str() + i, len);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Expected: 100 'f' nodes extracted\n";
    std::cout << "Actual: " << totalFNodes << " 'f' nodes extracted\n";
    std::cout << "Processing time: " << duration.count() << "ms\n";
    
    if (totalFNodes != 100) {
        throw std::runtime_error("Test 2 Failed: Expected 100 'f' nodes, extracted " + std::to_string(totalFNodes));
    }
    std::cout << "✅ Test 2 Passed\n";
}

// Main function
int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << "ActionPacketParser Comprehensive Test Suite" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    // Run all tests
    runTest("Original ActionPacketParser Test", testOriginalActionPacketParser);
    
    // Basic Tests from testActionPacketParserExtended.cpp
    runTest("Basic Test Scenario", testBasicScenario);
    
    // Boundary Condition Tests
    runTest("Single Character Input", testSingleCharInput);
    runTest("Empty Data Input", testEmptyDataInput);
    runTest("Partial Data Input", testPartialDataInput);
    
    // Error Handling Tests
    runTest("Invalid JSON Format", testInvalidJson);
    runTest("Missing Large Field", testMissingLargeField);
    runTest("Deeply Nested JSON", testDeepNestedJson);
    
    // Function Extension Tests
    runTest("Different Delimiter", testDifferentDelimiter);
    runTest("Different Large Field Name", testDifferentLargeField);
    runTest("Multiple Large Fields", testMultipleLargeFields);
    runTest("JSON with Escape Characters", testJsonWithEscapeCharacters);
    
    // Performance Tests
    runTest("Large Data Processing", testPerformance);
    runTest("Large f Field Data", testLargeFFieldPerformance);
    
    // 't' Response Tests from testTResponseParser.cpp
    runTest("'t' Response Parsing", testTResponseParsing); 
    
    // Output test results statistics
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Test Results:" << std::endl;
    std::cout << "Total Tests: " << stats.total << std::endl;
    std::cout << "Passed Tests: " << stats.passed << std::endl;
    std::cout << "Failed Tests: " << stats.failed << std::endl;
    std::cout << "Pass Rate: " << (stats.total > 0 ? (stats.passed * 100.0 / stats.total) : 0) << "%" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    return stats.failed == 0 ? 0 : 1;
}
