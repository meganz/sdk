#ifndef ACTION_PACKET_PARSER_H
#define ACTION_PACKET_PARSER_H

#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <functional>

// JSON parsing state enum
enum class JsonParseState {
    Start,          // initial state
    InObject,       // inside JSON {}
    InArray,        // inside JSON array []
    InKey,          // inside key name (string)
    InValue,        // inside value
    InString,       // inside string value
    EscapeChar,     // escape character (e.g., ")
    Complete        // parsing complete
};

// Configuration struct: stores information for nodes that need special handling
struct TargetNodeConfig {
    std::string path;          // node path (e.g., "t.f" or "f")
    bool enableIncremental;    // whether to enable incremental processing (for large fields)
    
    TargetNodeConfig(const std::string& p = "", bool inc = false)
        : path(p), enableIncremental(inc) {}
};

// ActionPacket parser core class
class ActionPacketParser {
public:
    // Callback: execution logic after parsing a complete packet
    using PacketExecCallback = std::function<void(const std::unordered_map<std::string, std::string>&)>;
    
    // Callback: execution logic after parsing a complete target node
    using TargetNodeCallback = std::function<void(const std::string&)>;

    // Constructor: pass packet delimiter (e.g., '\n')
    ActionPacketParser(char packetDelimiter = '\n')
        : packetDelimiter_(packetDelimiter),
          currentState_(JsonParseState::Start), nestedLevel_(0),
          isParsingLargeField_(false), isParsingTargetNode_(false) {}

    // Input network byte stream (byte by byte/batch)
    void feed(const char* data, size_t len);

    // Set packet execution callback
    void setPacketExecCallback(PacketExecCallback callback) {
        execCallback_ = callback;
    }
    
    // Set target node execution callback
    void setTargetNodeCallback(TargetNodeCallback callback) {
        targetNodeCallback_ = callback;
    }
    
    // Add configuration for target nodes that need special handling
    void addTargetNode(const TargetNodeConfig& config) {
        targetNodes_.push_back(config);
    }
    
    // Set large field path (e.g., "t")
    void setLargeFieldPath(const std::string& path) {
        largeFieldPath_ = path;
    }
    
    // Reset parser state (for testing)
    void resetParserState();
    
    // Get current parsing state (for debugging)
    JsonParseState getCurrentState() const { return currentState_; }

private:
    // Helper function: check if path matches target path
    bool matchTargetPath(const std::vector<std::string>& path, const std::string& targetPath);
    
    // Helper function: build current JSON path string
    std::string buildCurrentPath() const;
    
    // Frame splitting: extract complete packets from byte stream
    void splitFrames(const char* data, size_t len);

    // Buffer single packet until complete
    void bufferPacket(const char* data, size_t len);

    // Incrementally parse JSON of a complete packet (focus on handling large field t)
    void parseJsonIncrementally();

    // Process single JSON character (core state machine)
    void processJsonChar(char c);

    // Execute complete packet (trigger callback)
    void executePacket();

private:
    // Frame splitting related
    char packetDelimiter_;                // actionpackets sequence delimiter (e.g., \n)
    std::vector<char> frameBuffer_;       // frame splitting buffer (stores unprocessed bytes)

    // Packet buffering related
    std::vector<char> packetBuffer_;      // single packet complete buffer
    bool isPacketComplete_ = false;       // flag indicating if current packet is complete
    size_t bytesParsed_ = 0;              // number of bytes parsed, used for incremental parsing

    // Incremental JSON parsing related
    JsonParseState currentState_;         // JSON parsing state
    int nestedLevel_;                     // nesting level (handles {} and [])
    std::string currentKey_;              // currently parsing key name
    std::string currentValue_;            // currently parsing non-large field value
    std::unordered_map<std::string, std::string> packetData_; // parsed packet data
    std::vector<std::string> jsonPath_;   // current JSON path tracking (e.g., ["t", "f"])

    // Large field parsing related
    std::string largeFieldPath_;          // path of large field to be incrementally parsed (e.g., "t")
    bool isParsingLargeField_;            // whether currently parsing large field
    std::string largeFieldBuffer_;        // large field incremental buffer (processed segment by segment)
    std::string largeFieldKey_;           // save current large field key name, used to add to packetData_ when parsing is complete

    // Target node parsing related (general node extraction mechanism)
    std::vector<TargetNodeConfig> targetNodes_; // target node configuration list
    bool isParsingTargetNode_;            // whether currently parsing target node
    std::string targetNodeBuffer_;        // target node incremental buffer
    int targetNodeNestedLevel_;           // target node internal nesting level
    std::string currentTargetPath_;       // currently matching target path

    // Execution callbacks
    PacketExecCallback execCallback_;
    TargetNodeCallback targetNodeCallback_; // target node callback (general replacement for fNodeCallback_)
};

#endif // ACTION_PACKET_PARSER_H