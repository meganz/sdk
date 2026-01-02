#include "ActionPacketParser.h"
#include <algorithm>
#include <cstring>
using namespace std;

// Input network byte stream (core entry point)
void ActionPacketParser::feed(const char* data, size_t len) {
    if (len == 0 || data == nullptr) return;
    // Step 1: Split frames (extract individual packets)
    splitFrames(data, len);
}

// Helper function: check if path matches target path
bool ActionPacketParser::matchTargetPath(const std::vector<std::string>& path, const std::string& targetPath) {
    if (targetPath.empty()) return false;
    
    // Split target path into components
    std::vector<std::string> targetComponents;
    size_t start = 0;
    size_t pos = targetPath.find('.');
    
    while (pos != std::string::npos) {
        targetComponents.push_back(targetPath.substr(start, pos - start));
        start = pos + 1;
        pos = targetPath.find('.', start);
    }
    targetComponents.push_back(targetPath.substr(start));
    
    // If path length is less than target component length, return false directly
    if (path.size() < targetComponents.size()) return false;
    
    // Match from the beginning of the path
    for (size_t i = 0; i < targetComponents.size(); i++) {
        if (path[i] != targetComponents[i]) {
            return false;
        }
    }
    
    // All components of the target path match
    return true;
}

// Helper function: build current JSON path string
std::string ActionPacketParser::buildCurrentPath() const {
    if (jsonPath_.empty()) return "";
    
    std::string path;
    for (size_t i = 0; i < jsonPath_.size(); i++) {
        if (i > 0) path += ".";
        path += jsonPath_[i];
    }
    return path;
}

// Frame splitting: extract complete packets from byte stream
void ActionPacketParser::splitFrames(const char* data, size_t len) {
    // Append new data to frame buffer
    frameBuffer_.insert(frameBuffer_.end(), data, data + len);
    
    // Find delimiter and split into complete packets
    while (true) {
        // Use std::find to find delimiter
        auto delimiterIt = std::find(frameBuffer_.begin(), frameBuffer_.end(), packetDelimiter_);
        if (delimiterIt == frameBuffer_.end()) {
            // No complete packet found, keep frame buffer content waiting for subsequent data
            break;
        }
        
        // Calculate delimiter position
        
        size_t delimiterPos = delimiterIt - frameBuffer_.begin();
        // Extract bytes before delimiter as a single packet (skip empty packets)
        if (delimiterPos > 0) {
            bufferPacket(frameBuffer_.data(), delimiterPos);
        }
        
        // Remove processed bytes (including delimiter)
        frameBuffer_.erase(frameBuffer_.begin(), delimiterIt + 1);
    }
}

// Buffer single packet until complete
void ActionPacketParser::bufferPacket(const char* data, size_t len) {
    // Append split packet bytes to packet buffer
    packetBuffer_.insert(packetBuffer_.end(), data, data + len);
    // Do not automatically mark packet as complete, let JSON parser decide for itself

    // Trigger incremental JSON parsing
    parseJsonIncrementally();
}

// Incrementally parse JSON of a complete packet (focus on handling large field t)
void ActionPacketParser::parseJsonIncrementally() {
    if (packetBuffer_.empty()) return;

    // Only parse newly added characters, not the entire buffer
    size_t bufferSize = packetBuffer_.size();
    for (size_t i = bytesParsed_; i < bufferSize; i++) {
        processJsonChar(packetBuffer_[i]);
        // If parsing is complete, execute packet and reset state
        if (currentState_ == JsonParseState::Complete) {
            // Ensure large field content is added to packetData_
            if (isParsingLargeField_ && !largeFieldBuffer_.empty()) {
                packetData_[largeFieldKey_] = largeFieldBuffer_;
            }
            executePacket();
            resetParserState();
            return;
        }
    }
    
    // Update number of bytes parsed
    bytesParsed_ = bufferSize;
    
    // If parsing is not complete, don't clear packetBuffer_, keep partial data waiting for subsequent input
}

// core fuction：process each character in JSON
void ActionPacketParser::processJsonChar(char c) {
    /*static unordered_map<JsonParseState,string> stateNames={
        {JsonParseState::Start,"Start"},
        {JsonParseState::InObject,"InObject"},
        {JsonParseState::InArray,"InArray"},
        {JsonParseState::InKey,"InKey"},
        {JsonParseState::InValue,"InValue"},
        {JsonParseState::InString,"InString"},
        {JsonParseState::EscapeChar,"EscapeChar"},
        {JsonParseState::Complete,"Complete"},
    };
    cout<<stateNames[currentState_]<<":"<<c<<endl;*/
    switch (currentState_) {
        case JsonParseState::Start: {
            if (c == '{') {
                currentState_ = JsonParseState::InObject;
                nestedLevel_ = 1;
            }
            break;
        }

        case JsonParseState::InObject: {
            // end of object
            if (c == '}') {
                nestedLevel_--;

                //cout<<nestedLevel_<<endl;
                
                // if it's not large field, append character to current path value
                if (!isParsingLargeField_ && !jsonPath_.empty()) {
                    std::string currentKey = jsonPath_.back();
                    packetData_[currentKey] += c;
                }
                
                if (!jsonPath_.empty()) {
                    jsonPath_.pop_back(); // Exit current object
                }
                if (nestedLevel_ == 0) {
                    currentState_ = JsonParseState::Complete;
                    return;
                }
            } 
            // start of object
            else if (c == '{') {
                nestedLevel_++;
                
                // if it's not large field, append character to current path value
                if (!isParsingLargeField_ && !jsonPath_.empty()) {
                    std::string currentKey = jsonPath_.back();
                    packetData_[currentKey] += c;
                }
                
                // If there is a current key, add it to the path
                if (!currentKey_.empty()) {
                    jsonPath_.push_back(currentKey_);
                    currentKey_.clear();
                }
            } 
            // start of array [
            else if (c == '[') {
                nestedLevel_++;
                
                // if it's not large field, append character to current path value
                if (!isParsingLargeField_ && !jsonPath_.empty()) {
                    std::string currentKey = jsonPath_.back();
                    packetData_[currentKey] += c;
                }
                
                currentState_ = JsonParseState::InArray;
                // If there is a current key, add it to the path
                if (!currentKey_.empty()) {
                    jsonPath_.push_back(currentKey_);
                    currentKey_.clear();
                }
            } 
            // key start
            else if (c == '"') {
                // if it's not large field, append character to current path value
                if (!isParsingLargeField_ && !jsonPath_.empty()) {
                    std::string currentKey = jsonPath_.back();
                    packetData_[currentKey] += c;
                }
                
                currentState_ = JsonParseState::InKey;
                currentKey_.clear();
            }
            else {
                // if it's not large field, append character to current path value
                if (!isParsingLargeField_ && !jsonPath_.empty()) {
                    std::string currentKey = jsonPath_.back();
                    packetData_[currentKey] += c;
                }
                // if it's large field, append character to large field buffer
                else if (isParsingLargeField_) {
                    largeFieldBuffer_ += c;
                }
            }
            break;
        }

        case JsonParseState::InKey: {
            // Escape character
            if (c == '\\') {
                currentState_ = JsonParseState::EscapeChar;
                currentKey_ += c;
                break;
            }
            
            // if it's not escape character, append character to current key
            if (c == '"' && currentState_ != JsonParseState::EscapeChar) {
                // key name parse complete, switch to value parse state
                currentState_ = JsonParseState::InValue;
                currentValue_.clear();
                
                // Build current path (including the key just parsed)
                std::string currentPath = buildCurrentPath();
                if (!currentPath.empty()) {
                    currentPath += ".";
                }
                currentPath += currentKey_;
                
                // Check if it's a large field (based on configured large field path)
                isParsingLargeField_ = matchTargetPath(jsonPath_, largeFieldPath_) || 
                                     (largeFieldPath_ == currentKey_ && jsonPath_.empty());
                
                // If it's a large field, save the key name
                if (isParsingLargeField_) {
                    largeFieldKey_ = currentKey_;
                    // Initialize large field buffer
                    largeFieldBuffer_.clear();
                }
                
                break;
            }
            
            // Add character to key name
            currentKey_ += c;
            
            break;
        }

        case JsonParseState::InValue: {
            // Check if parsing target node
            if (isParsingTargetNode_) {
                // Add character to target node buffer
                targetNodeBuffer_ += c;
            }

            // If it's a large field, add all characters to buffer
            if (isParsingLargeField_) {
                largeFieldBuffer_ += c;
            }
            
            // Handle array start
            if (c == '[') {
                nestedLevel_++;
                currentState_ = JsonParseState::InArray;
                // Build current complete path
                std::string currentPath = buildCurrentPath();
                if (!currentKey_.empty()) {
                    // Save key name
                    std::string key = currentKey_;
                    
                    // If not a large field, save key-value pair
                    if (!isParsingLargeField_) {
                        // Array value starts with '['
                        packetData_[key] = "[";
                    }
                    
                    currentPath += (currentPath.empty() ? "" : ".") + key;
                    // Add key to path
                    jsonPath_.push_back(key);
                    currentKey_.clear();
                }
                
                // Check if matches any target node path
                for (const auto& config : targetNodes_) {
                    if (currentPath == config.path) {
                        currentTargetPath_ = config.path;
                        break;
                    }
                }
            }
            // Handle object start
            else if (c == '{') {
                nestedLevel_++;
                currentState_ = JsonParseState::InObject;
                // Build current complete path
                std::string currentPath = buildCurrentPath();
                if (!currentKey_.empty()) {
                    // Save key name
                    std::string key = currentKey_;
                    
                    // If not a large field, save key-value pair
                    if (!isParsingLargeField_) {
                        // Object value starts with '{'
                        packetData_[key] = "{";
                    }
                    
                    currentPath += (currentPath.empty() ? "" : ".") + key;
                    // Add key to path
                    jsonPath_.push_back(key);
                    currentKey_.clear();
                }
                
                // Check if matches any target node path
                for (const auto& config : targetNodes_) {
                    if (currentPath == config.path) {
                        currentTargetPath_ = config.path;
                        break;
                    }
                }
            }
            // Handle value end (comma or closing brace)
            else if (c == ',' || c == '}') {
                // Save current value
                if (!currentKey_.empty()) {
                    packetData_[currentKey_] = currentValue_;
                }
                
                // Handle object end
                if (c == '}') {
                    nestedLevel_--;
                    if (!jsonPath_.empty()) {
                        jsonPath_.pop_back();
                    }
                    if (nestedLevel_ == 0) {
                        currentState_ = JsonParseState::Complete;
                        break;
                    }
                }
                // Switch back to object state only if not in Complete state
                if (currentState_ != JsonParseState::Complete) {
                    currentState_ = JsonParseState::InObject;
                }
            }
            // Handle string start
            else if (c == '"') {
                currentState_ = JsonParseState::InString;
            }
            // Handle numeric value
            else if (std::isdigit(c)) {
                currentValue_ += c;
            }
            // Handle whitespace characters (skip)
            else if (std::isspace(c)) {
                // Skip
            }
            // Add other characters to current value
            else {
                currentValue_ += c;
            }
            break;
        }

        case JsonParseState::InString: {
            // Check if parsing target node
            if (isParsingTargetNode_) {
                // Add all characters (including escape characters) to target node buffer
                targetNodeBuffer_ += c;
            }
            
            // If it's a large field, add all characters to buffer
            if (isParsingLargeField_) {
                largeFieldBuffer_ += c;
            }
            
            if (c == '"' && currentState_ != JsonParseState::EscapeChar) {
                // String value parsing complete
                if (!isParsingLargeField_) {
                    packetData_[currentKey_] = currentValue_;
                }
                currentState_ = JsonParseState::InObject;
            } else if (c == '\\') {
                currentState_ = JsonParseState::EscapeChar;
            } else if (currentState_ != JsonParseState::EscapeChar) {
                currentValue_ += c;
            }
            break;
        }

        case JsonParseState::EscapeChar: {
            // For target nodes, add all escape characters to buffer
            if (isParsingTargetNode_) {
                targetNodeBuffer_ += c;
            }
            
            // If it's a large field, add all characters to buffer
            if (isParsingLargeField_) {
                largeFieldBuffer_ += c;
            } else {
                // Not a large field, handle escape characters normally
                if (currentKey_.empty()) {
                    currentState_ = JsonParseState::InKey;
                    currentKey_ += c;
                } else {
                    currentState_ = JsonParseState::InString;
                    currentValue_ += c;
                }
            }
            break;
        }

        case JsonParseState::InArray: {
            // Incremental buffering for large field array content
            if (isParsingLargeField_) {
                largeFieldBuffer_ += c;
            }
            // If not a large field, append character to current path value
            else if (!jsonPath_.empty()) {
                std::string currentKey = jsonPath_.back();
                packetData_[currentKey] += c;
            }

            // Check if current path matches any target node
            std::string currentPath = buildCurrentPath();
            if (currentTargetPath_.empty()) {
                for (const auto& config : targetNodes_) {
                    if (matchTargetPath(jsonPath_, config.path) && config.enableIncremental) {
                        currentTargetPath_ = config.path;
                        break;
                    }
                }
            }

            // Target node processing
            if (!currentTargetPath_.empty()) {
                if (isParsingTargetNode_) {
                    // Add character to target node buffer
                    targetNodeBuffer_ += c;
                    
                    // Track target node nesting level
                    if (c == '{') {
                        targetNodeNestedLevel_++;
                    } else if (c == '}') {
                        targetNodeNestedLevel_--;
                        // Check if target node is complete (nesting level returns to 0)
                        if (targetNodeNestedLevel_ == 0) {
                            // Trigger target node callback
                            if (targetNodeCallback_) {
                                targetNodeCallback_(targetNodeBuffer_);
                            }
                            // Reset target node parsing state but keep currentTargetPath_ to continue processing other nodes
                            isParsingTargetNode_ = false;
                            targetNodeBuffer_.clear();
                            targetNodeNestedLevel_ = 0;
                        }
                    }
                } else if (c == '{') {
                    // Start parsing new target node
                    isParsingTargetNode_ = true;
                    targetNodeBuffer_ = "{";
                    targetNodeNestedLevel_ = 1;
                }
            }

            // Handle array end
            if (c == ']') {
                nestedLevel_--;
                //cout<<nestedLevel_<<endl;
                if (!jsonPath_.empty()) {
                    jsonPath_.pop_back(); // Exit current array
                }
                
                if (isParsingLargeField_) {
                    // Large field array ends, save its content to packetData
                    packetData_[largeFieldKey_] = largeFieldBuffer_;
                    largeFieldBuffer_.clear();
                    isParsingLargeField_ = false;
                    currentTargetPath_.clear();
                    // Return to object state after array ends
                    currentState_ = JsonParseState::InObject;
                } else if (nestedLevel_ == 0) {
                    currentState_ = JsonParseState::Complete;
                } else {
                    currentState_ = JsonParseState::InObject;
                }
            }
            // Handle object start within array
            else if (c == '{') {
                nestedLevel_++;
            } else if (c == '}') {
                nestedLevel_--;
                //cout<<nestedLevel_<<endl;
            }
            // Handle array start within array
            else if (c == '[') {
                nestedLevel_++;
            } 
            // Skip whitespace characters
            else if (std::isspace(c)) {
                // Skip
            }
            break;
        }

        default:
            break;
    }
}

// Execute complete packet (trigger callback)
void ActionPacketParser::executePacket() {
    // If parsing large field, add buffer content to packetData_
    if (isParsingLargeField_ && !largeFieldBuffer_.empty()) {
        packetData_[largeFieldKey_] = largeFieldBuffer_;
    }
    
    if (execCallback_) {
        execCallback_(packetData_);
    }
    // Clear packet buffer
    packetBuffer_.clear();
    isPacketComplete_ = false;
}

// Reset parsing state (for next packet)
void ActionPacketParser::resetParserState() {
    // Reset state
    currentState_ = JsonParseState::Start;
    nestedLevel_ = 0;
    isParsingLargeField_ = false;
    isParsingTargetNode_ = false;
    targetNodeNestedLevel_ = 0;
    isPacketComplete_ = false;
    bytesParsed_ = 0; // 重置已解析字节计数
    
    // 清空缓冲区和路径
    currentKey_.clear();
    currentValue_.clear();
    largeFieldBuffer_.clear();
    targetNodeBuffer_.clear();
    packetData_.clear();
    packetBuffer_.clear();
    jsonPath_.clear();
    currentTargetPath_.clear();
}