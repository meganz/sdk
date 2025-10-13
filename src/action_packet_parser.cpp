#include "mega/action_packet_parser.h"
#include "mega/megaclient.h"
#include "mega/logging.h"
#include "mega/name_id.h"
#include <mutex>

namespace mega {

ActionPacketParser::ActionPacketParser(MegaClient* client) 
    : mClient(client), mLastAPDeletedNode(nullptr)
{
    initializeFilters();
}


// 更新 clear 方法
void ActionPacketParser::clear()
{
    mJsonSplitter.clear();
    mChunkedProgress = 0;
    mActionpacketsProcessed = 0;
    mLastAPDeletedNode = nullptr;
    mHasStarted = false;
    mUnparsedBuffer.clear();
    mState = STATE_NOT_STARTED;  // 重置状态为未开始
}

// 实现 getState 方法
ActionPacketParser::ParseState ActionPacketParser::getState() const
{
    return mState;
}

// 更新 hasFinished 和 hasFailed 方法以保持向后兼容
bool ActionPacketParser::hasFinished() const
{
    return mState == STATE_COMPLETED;
}

bool ActionPacketParser::hasFailed() const
{
    return mState == STATE_FAILED;
}

// 修改 processChunk 方法以正确更新状态
m_off_t ActionPacketParser::processChunk(const char* chunk, size_t length)
{
    // 如果已经完成或失败，不再处理
    if (mState == STATE_COMPLETED || mState == STATE_FAILED) {
        return 0;
    }

    // 1. 将新数据添加到未解析缓冲区
    mUnparsedBuffer.append(chunk, length);
    
    // 2. 更新状态为解析中
    if (mState == STATE_NOT_STARTED) {
        mState = STATE_PARSING;
    }
    
    // 3. 创建包含所有未解析数据的 JSON 对象
    JSON json(mUnparsedBuffer.c_str());
    bool start = !mHasStarted;
    m_off_t consumed = 0;

    if (start) {
        if (!json.enterarray()) {
            // 解析错误，但不清除缓冲区，可能需要更多数据
            mState = STATE_FAILED;
            return 0;
        }
        consumed++;
        mHasStarted = true;
    }

    // 4. 使用 JSONSplitter 解析数据
    m_off_t splitterConsumed = mJsonSplitter.processChunk(&mFilters, json.pos);
    if (mJsonSplitter.hasFailed()) {
        mUnparsedBuffer.clear();
        mState = STATE_FAILED;
        return 0;
    }

    consumed += splitterConsumed;
    mChunkedProgress += static_cast<size_t>(consumed);
    
    // 5. 处理解析完成的情况
    if (mJsonSplitter.hasFinished()) {
        if (!json.leavearray()) {
            LOG_err << "Unexpected end of JSON stream: " << json.pos;
            mState = STATE_FAILED;
        }
        else {
            consumed++;
            mState = STATE_COMPLETED;
        }
        mUnparsedBuffer.clear();
    }
    // 6. 处理未解析完的数据
    else if (consumed > 0) {
        // 移除已解析的数据，保留未解析部分
        mUnparsedBuffer = mUnparsedBuffer.substr(consumed);
        mState = STATE_PARSING;  // 确保状态为解析中
    }

    return consumed;
}

m_off_t ActionPacketParser::totalChunkedProgress() const
{
    return static_cast<m_off_t>(mChunkedProgress);
}

void ActionPacketParser::initializeFilters()
{
    // 块开始解析
    mFilters.emplace("<", [this](JSON *) {
        std::unique_lock<recursive_mutex> lock(mClient->nodeTreeMutex);
        return true;
    });

    // 块结束解析
    mFilters.emplace(">", [this](JSON *) {
        return true;
    });

    // 处理单个 actionpacket 对象
    mFilters.emplace("{}", [this](JSON *json) {
        processActionPacket(json);
        return true;
    });

    // 特别处理 't' 元素（节点添加）
    mFilters.emplace("{\"t\"", [this](JSON *json) {
        processNodesElement(json);
        return true;
    });

    // 解析错误处理
    mFilters.emplace("E", [this](JSON*) {
        LOG_err << "Error parsing actionpacket stream";
        return true;
    });
}

void ActionPacketParser::processActionPacket(JSON *json)
{
    if (!json->enterobject()) return;
    
    // 确保 "a" 属性是对象中的第一个元素
    if (json->getnameid() == makeNameid("a")) {
        name_id name = json->getnameidvalue();
        
        // 处理不同类型的 action
        switch (name) {
            case name_id::u:
                // 节点更新
                mClient->sc_updatenode();
                break;
            
            case makeNameid("t"):
                // 't' 元素会通过专门的过滤器处理
                break;
            
            case name_id::d:
                // 节点删除
                mLastAPDeletedNode = mClient->sc_deltree();
                break;
            
            case makeNameid("s"):
            case makeNameid("s2"):
                // 共享添加/更新/撤销
                if (mClient->sc_shares()) {
                    mClient->mergenewshares(1);
                }
                break;
            
            case name_id::c:
                // 联系人添加/更新
                mClient->sc_contacts();
                break;
            
            case makeNameid("fa"):
                // 文件属性更新
                mClient->sc_fileattr();
                break;
            
            case makeNameid("ua"):
                // 用户属性更新
                mClient->sc_userattr();
                break;
            
            case name_id::psts:
            case name_id::psts_v2:
            case makeNameid("ftr"):
                if (mClient->sc_upgrade(name)) {
                    mClient->app->account_updated();
                    mClient->abortbackoff(true);
                }
                break;
            
            case name_id::pses:
                // 支付提醒
                mClient->sc_paymentreminder();
                break;
            
            case name_id::ipc:
                // 传入的待处理联系人请求（给我们的）
                mClient->sc_ipc();
                break;
            
            case makeNameid("opc"):
                // 传出的待处理联系人请求（来自我们的）
                mClient->sc_opc();
                break;
            
            case name_id::upci:
                // 传入的待处理联系人请求更新（接受/拒绝/忽略）
                mClient->sc_upc(true);
                break;
            
            case name_id::upco:
                // 传出的待处理联系人请求更新（来自他们，接受/拒绝/忽略）
                mClient->sc_upc(false);
                break;
            
            case makeNameid("ph"):
                // 公共链接处理
                mClient->sc_ph();
                break;
            
            case makeNameid("se"):
                // 设置电子邮件
                mClient->sc_se();
                break;
            
#ifdef ENABLE_CHAT
            case makeNameid("mcpc"):
            {
                bool readingPublicChat = true;
                // 聊天创建 / 对等方的邀请 / 对等方的移除
                mClient->sc_chatupdate(readingPublicChat);
            }
            break;
            
            case makeNameid("mcc"):
                // 聊天创建 / 对等方的邀请 / 对等方的移除
                mClient->sc_chatupdate(false);
                break;
            
            case makeNameid("mcfpc"):
            case makeNameid("mcfc"):
                // 聊天标志更新
                mClient->sc_chatflags();
                break;
            
            case makeNameid("mcpna"):
            case makeNameid("mcna"):
                // 授予 / 撤销对节点的访问权限
                // 注意：这里可能需要更多处理，具体取决于sc_chatnodes的实现
                break;
#endif
        }
    }
    
    json->leaveobject();
}

void ActionPacketParser::processNodesElement(JSON *json)
{
    bool isMoveOperation = false;
    
    if (!mClient->loggedIntoFolder())
        mClient->useralerts.beginNotingSharedNodes();
    
    handle originatingUser = mClient->sc_newnodes(
        mClient->fetchingnodes ? nullptr : mLastAPDeletedNode.get(), 
        isMoveOperation
    );
    
    mClient->mergenewshares(1);
    
    if (!mClient->loggedIntoFolder())
        mClient->useralerts.convertNotedSharedNodes(true, originatingUser);
    
    mLastAPDeletedNode = nullptr;
}

} // namespace