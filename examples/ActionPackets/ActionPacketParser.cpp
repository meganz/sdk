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
                    jsonPath_.pop_back(); // 退出当前对象
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
                
                // 如果当前有键，将键添加到路径中
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
                // 如果当前有键，将键添加到路径中
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
            if (currentState_ != JsonParseState::EscapeChar) {
                currentKey_ += c;
            }
            
            // if it's not escape character, append character to current key
            if (c == '"' && currentState_ != JsonParseState::EscapeChar) {
                // key name parse complete, switch to value parse state
                currentState_ = JsonParseState::InValue;
                currentValue_.clear();
                
                // 构建当前路径（包括刚刚解析的键）
                std::string currentPath = buildCurrentPath();
                if (!currentPath.empty()) {
                    currentPath += ".";
                }
                currentPath += currentKey_;
                
                // 检查是否为大字段（根据配置的大字段路径）
                isParsingLargeField_ = matchTargetPath(jsonPath_, largeFieldPath_) || 
                                     (largeFieldPath_ == currentKey_ && jsonPath_.empty());
                
                // 如果是大字段，保存键名
                if (isParsingLargeField_) {
                    largeFieldKey_ = currentKey_;
                    // 初始化大字段缓冲区
                    largeFieldBuffer_.clear();
                }
                
                break;
            }
            
            // 将字符添加到键名
            currentKey_ += c;
            
            break;
        }

        case JsonParseState::InValue: {
            // 检查是否正在解析目标节点
            if (isParsingTargetNode_) {
                // 添加字符到目标节点缓冲区
                targetNodeBuffer_ += c;
            }

            // 如果是大字段，将所有字符添加到缓冲区
            if (isParsingLargeField_) {
                largeFieldBuffer_ += c;
            }
            
            // 处理数组开始
            if (c == '[') {
                nestedLevel_++;
                currentState_ = JsonParseState::InArray;
                // 构建当前完整路径
                std::string currentPath = buildCurrentPath();
                if (!currentKey_.empty()) {
                    // 保存键名
                    std::string key = currentKey_;
                    
                    // 如果不是大字段，保存键值对
                    if (!isParsingLargeField_) {
                        // 数组值以'['开头
                        packetData_[key] = "[";
                    }
                    
                    currentPath += (currentPath.empty() ? "" : ".") + key;
                    // 将键添加到路径中
                    jsonPath_.push_back(key);
                    currentKey_.clear();
                }
                
                // 检查是否匹配任何目标节点路径
                for (const auto& config : targetNodes_) {
                    if (currentPath == config.path) {
                        currentTargetPath_ = config.path;
                        break;
                    }
                }
            }
            // 处理对象开始
            else if (c == '{') {
                nestedLevel_++;
                currentState_ = JsonParseState::InObject;
                // 构建当前完整路径
                std::string currentPath = buildCurrentPath();
                if (!currentKey_.empty()) {
                    // 保存键名
                    std::string key = currentKey_;
                    
                    // 如果不是大字段，保存键值对
                    if (!isParsingLargeField_) {
                        // 对象值以'{'开头
                        packetData_[key] = "{";
                    }
                    
                    currentPath += (currentPath.empty() ? "" : ".") + key;
                    // 将键添加到路径中
                    jsonPath_.push_back(key);
                    currentKey_.clear();
                }
                
                // 检查是否匹配任何目标节点路径
                for (const auto& config : targetNodes_) {
                    if (currentPath == config.path) {
                        currentTargetPath_ = config.path;
                        break;
                    }
                }
            }
            // 处理值结束（逗号或右大括号）
            else if (c == ',' || c == '}') {
                // 保存当前值
                if (!currentKey_.empty()) {
                    packetData_[currentKey_] = currentValue_;
                }
                
                // 处理对象结束
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
                // 只有在不是Complete状态时才切换回对象状态
                if (currentState_ != JsonParseState::Complete) {
                    currentState_ = JsonParseState::InObject;
                }
            }
            // 处理字符串开始
            else if (c == '"') {
                currentState_ = JsonParseState::InString;
            }
            // 处理数字值
            else if (std::isdigit(c)) {
                currentValue_ += c;
            }
            // 处理空格等空白字符（跳过）
            else if (std::isspace(c)) {
                // 跳过
            }
            // 其他字符添加到当前值
            else {
                currentValue_ += c;
            }
            break;
        }

        case JsonParseState::InString: {
            // 检查是否正在解析目标节点
            if (isParsingTargetNode_) {
                // 添加所有字符（包括转义字符）到目标节点缓冲区
                targetNodeBuffer_ += c;
            }
            
            // 如果是大字段，将所有字符添加到缓冲区
            if (isParsingLargeField_) {
                largeFieldBuffer_ += c;
            }
            
            if (c == '"' && currentState_ != JsonParseState::EscapeChar) {
                // 字符串值解析完成
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
            // 对于目标节点，添加所有转义字符到缓冲区
            if (isParsingTargetNode_) {
                targetNodeBuffer_ += c;
            }
            
            // 如果是大字段，将所有字符添加到缓冲区
            if (isParsingLargeField_) {
                largeFieldBuffer_ += c;
            } else {
                // 非大字段，正常处理转义字符
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
            // 大字段数组内容增量缓冲
            if (isParsingLargeField_) {
                largeFieldBuffer_ += c;
            }
            // 如果不是大字段，将字符追加到当前路径对应的值
            else if (!jsonPath_.empty()) {
                std::string currentKey = jsonPath_.back();
                packetData_[currentKey] += c;
            }

            // 检查当前路径是否匹配任何目标节点
            std::string currentPath = buildCurrentPath();
            if (currentTargetPath_.empty()) {
                for (const auto& config : targetNodes_) {
                    if (matchTargetPath(jsonPath_, config.path) && config.enableIncremental) {
                        currentTargetPath_ = config.path;
                        break;
                    }
                }
            }

            // 目标节点处理
            if (!currentTargetPath_.empty()) {
                if (isParsingTargetNode_) {
                    // 添加字符到目标节点缓冲区
                    targetNodeBuffer_ += c;
                    
                    // 跟踪目标节点嵌套层级
                    if (c == '{') {
                        targetNodeNestedLevel_++;
                    } else if (c == '}') {
                        targetNodeNestedLevel_--;
                        // 检查目标节点是否完整（嵌套层级回到0）
                        if (targetNodeNestedLevel_ == 0) {
                            // 触发目标节点回调
                            if (targetNodeCallback_) {
                                targetNodeCallback_(targetNodeBuffer_);
                            }
                            // 重置目标节点解析状态，但保持currentTargetPath_以继续处理其他节点
                            isParsingTargetNode_ = false;
                            targetNodeBuffer_.clear();
                            targetNodeNestedLevel_ = 0;
                        }
                    }
                } else if (c == '{') {
                    // 开始解析新的目标节点
                    isParsingTargetNode_ = true;
                    targetNodeBuffer_ = "{";
                    targetNodeNestedLevel_ = 1;
                }
            }

            // 处理数组结束
            if (c == ']') {
                nestedLevel_--;
                //cout<<nestedLevel_<<endl;
                if (!jsonPath_.empty()) {
                    jsonPath_.pop_back(); // 退出当前数组
                }
                
                if (isParsingLargeField_) {
                    // 大字段数组结束，将其内容保存到packetData中
                    packetData_[largeFieldKey_] = largeFieldBuffer_;
                    largeFieldBuffer_.clear();
                    isParsingLargeField_ = false;
                    currentTargetPath_.clear();
                    // 数组结束后回到对象状态
                    currentState_ = JsonParseState::InObject;
                } else if (nestedLevel_ == 0) {
                    currentState_ = JsonParseState::Complete;
                } else {
                    currentState_ = JsonParseState::InObject;
                }
            }
            // 处理数组内的对象开始
            else if (c == '{') {
                nestedLevel_++;
            } else if (c == '}') {
                nestedLevel_--;
                //cout<<nestedLevel_<<endl;
            }
            // 处理数组内的数组开始
            else if (c == '[') {
                nestedLevel_++;
            } 
            // 跳过空白字符
            else if (std::isspace(c)) {
                // 跳过
            }
            break;
        }

        default:
            break;
    }
}

// 执行完整的packet（触发回调）
void ActionPacketParser::executePacket() {
    // 如果正在解析大字段，将缓冲区内容添加到packetData_
    if (isParsingLargeField_ && !largeFieldBuffer_.empty()) {
        packetData_[largeFieldKey_] = largeFieldBuffer_;
    }
    
    if (execCallback_) {
        execCallback_(packetData_);
    }
    // 清空packet缓冲区
    packetBuffer_.clear();
    isPacketComplete_ = false;
}

// 重置解析状态（用于下一个packet）
void ActionPacketParser::resetParserState() {
    // 重置状态
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