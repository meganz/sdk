/*
 * @file mega/action_packet_parser.h
 * @brief Classes for manipulating json action packet parser
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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

#ifndef MEGA_ACTION_PACKET_PARSER_H
#define MEGA_ACTION_PACKET_PARSER_H 1

#include "json.h"
#include "megaclient.h"
#include "node.h"

#include <functional>
#include <map>
#include <memory>

namespace mega
{

class Node;
class MegaClient;

class ActionPacketParser
{
public:
    ActionPacketParser(MegaClient* client);
    ~ActionPacketParser() = default;

    void clear();

    m_off_t processChunk(const char* chunk, size_t length);

    // 在类定义中添加

public:
    enum ParseState
    {
        STATE_NOT_STARTED, // 未开始解析
        STATE_PARSING, // 解析中
        STATE_COMPLETED, // 解析成功完成
        STATE_FAILED // 解析失败
    };

    ParseState getState() const; // 获取当前解析状态

    bool hasFinished() const;
    bool hasFailed() const; 

private:
    ParseState mState = STATE_NOT_STARTED; // 添加状态成员变量
    std::string mUnparsedBuffer; // 未解析完成的数据缓冲区
    MegaClient* mClient;
    JSONSplitter mJsonSplitter;
    std::map<std::string, std::function<bool(JSON*)>> mFilters;
    size_t mChunkedProgress = 0;
    bool mHasStarted = false;
    int mActionpacketsProcessed = 0;
    std::unique_ptr<Node> mLastAPDeletedNode;
};

} // namespace

#endif