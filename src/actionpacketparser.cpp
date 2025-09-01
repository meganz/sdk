#include "mega/actionpacketparser.h"
#include "mega/megaclient.h"
#include "mega/logging.h"
#include "mega/utils.h"

#include <sstream>
#include <iomanip>

namespace mega {

// ActionPacketParser::Stats implementation
void ActionPacketParser::Stats::reset()
{
    bytesProcessed = 0;
    packetsProcessed = 0;
    partialPackets = 0;
    largeElements = 0;
    treeBatchesProcessed = 0;
    maxPacketSize = 0;
    totalMemoryUsed = 0;
    startTime = std::chrono::steady_clock::now();
    totalProcessingTime = std::chrono::milliseconds{0};
}

void ActionPacketParser::Stats::toJson(string& output) const
{
    auto duration = std::chrono::steady_clock::now() - startTime;
    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    
    std::ostringstream oss;
    oss << "{"
        << "\"bytesProcessed\":" << bytesProcessed << ","
        << "\"packetsProcessed\":" << packetsProcessed << ","
        << "\"partialPackets\":" << partialPackets << ","
        << "\"largeElements\":" << largeElements << ","
        << "\"treeBatchesProcessed\":" << treeBatchesProcessed << ","
        << "\"maxPacketSize\":" << maxPacketSize << ","
        << "\"totalMemoryUsed\":" << totalMemoryUsed << ","
        << "\"durationMs\":" << durationMs << ","
        << "\"processingTimeMs\":" << totalProcessingTime.count()
        << "}";
    output = oss.str();
}

// ActionPacketParser implementation
ActionPacketParser::ActionPacketParser(MegaClient* client)
    : JSONSplitter()
    , mClient(client)
{
    mStats.reset();
    mLastProgressLog = std::chrono::steady_clock::now();
    setupFilters();
}

ActionPacketParser::~ActionPacketParser() = default;

void ActionPacketParser::setupFilters()
{
    // Set up streaming filters for different JSON paths
    mFilters["<"] = [this](JSON* json) { return onStreamingStart(json); };
    mFilters[">"] = [this](JSON* json) { return onStreamingEnd(json); };
    mFilters["{[a"] = [this](JSON* json) { return onActionPacketArray(json); };
    mFilters["{[a{"] = [this](JSON* json) { return onActionPacket(json); };
    mFilters["{[a{[t"] = [this](JSON* json) { return onTreeElement(json); };
    mFilters["E"] = [this](JSON* json) { return onError(json); };
}

bool ActionPacketParser::processChunk(const char* data, size_t len)
{
    if (mHasError)
    {
        return false;
    }

    auto startTime = std::chrono::steady_clock::now();
    
    try
    {
        // Update statistics
        mStats.bytesProcessed += len;
        
        // Process using JSONSplitter
        m_off_t consumed = JSONSplitter::processChunk(&mFilters, data);
        
        if (hasFailed())
        {
            setError("JSONSplitter parsing failed");
            return false;
        }

        // Update processing time
        auto endTime = std::chrono::steady_clock::now();
        mStats.totalProcessingTime += std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        
        // Check memory limits
        if (!checkMemoryLimits())
        {
            setError("Memory limit exceeded");
            return false;
        }

        // Log progress periodically
        if (mDiagnosticsEnabled)
        {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - mLastProgressLog).count() >= 5)
            {
                logProgress();
                mLastProgressLog = now;
            }
        }

        return consumed > 0 || hasFinished();
    }
    catch (const std::exception& e)
    {
        setError(string("Exception during parsing: ") + e.what());
        return false;
    }
}

void ActionPacketParser::setPacketHandler(const string& actionType, PacketHandler handler)
{
    mPacketHandlers[actionType] = handler;
}

void ActionPacketParser::setNodeBatchHandler(NodeBatchHandler handler)
{
    mNodeBatchHandler = handler;
}

size_t ActionPacketParser::getCurrentMemoryUsage() const
{
    // Estimate current memory usage
    size_t usage = sizeof(*this);
    usage += mTreeElementBuffer.capacity();
    usage += mLastError.capacity();
    usage += mCurrentActionType.capacity();
    
    // Add filter map overhead
    usage += mFilters.size() * (sizeof(string) + sizeof(std::function<bool(JSON*)>));
    usage += mPacketHandlers.size() * (sizeof(string) + sizeof(PacketHandler));
    
    return usage;
}

void ActionPacketParser::dumpState(string& output) const
{
    std::ostringstream oss;
    oss << "ActionPacketParser State:\n"
        << "  InsideActionPacketArray: " << mInsideActionPacketArray << "\n"
        << "  InsideActionPacket: " << mInsideActionPacket << "\n"
        << "  CurrentActionType: " << mCurrentActionType << "\n"
        << "  InsideTreeElement: " << mInsideTreeElement << "\n"
        << "  TreeElementBatchCount: " << mTreeElementBatchCount << "\n"
        << "  HasError: " << mHasError << "\n"
        << "  LastError: " << mLastError << "\n"
        << "  Memory Usage: " << getCurrentMemoryUsage() << " bytes\n";
    
    string stats;
    mStats.toJson(stats);
    oss << "  Stats: " << stats << "\n";
    
    output = oss.str();
}

void ActionPacketParser::reset()
{
    JSONSplitter::clear();
    mInsideActionPacketArray = false;
    mInsideActionPacket = false;
    mCurrentPacketStart = 0;
    mCurrentPacketDepth = 0;
    mCurrentActionType.clear();
    mInsideTreeElement = false;
    mTreeElementBatchCount = 0;
    mTreeElementBuffer.clear();
    mHasError = false;
    mLastError.clear();
    mStats.reset();
    mLastProgressLog = std::chrono::steady_clock::now();
}

bool ActionPacketParser::checkMemoryLimits()
{
    size_t currentUsage = getCurrentMemoryUsage();
    mStats.totalMemoryUsed = std::max(mStats.totalMemoryUsed, currentUsage);
    
    if (currentUsage > mMaxMemoryLimit)
    {
        LOG_warn << "ActionPacketParser memory usage " << currentUsage 
                 << " exceeds limit " << mMaxMemoryLimit;
        return false;
    }
    
    return true;
}

void ActionPacketParser::flushPendingData()
{
    if (!mTreeElementBuffer.empty() && mNodeBatchHandler)
    {
        JSON json(mTreeElementBuffer.c_str());
        mNodeBatchHandler(&json, mTreeElementBatchCount);
        mTreeElementBuffer.clear();
        mTreeElementBatchCount = 0;
    }
}

void ActionPacketParser::setError(const string& error)
{
    mHasError = true;
    mLastError = error;
    LOG_err << "ActionPacketParser error: " << error;
}

void ActionPacketParser::logProgress() const
{
    LOG_debug << "ActionPacketParser progress:"
              << " bytes=" << mStats.bytesProcessed
              << " packets=" << mStats.packetsProcessed 
              << " partial=" << mStats.partialPackets
              << " memory=" << getCurrentMemoryUsage();
}

// Filter callback implementations
bool ActionPacketParser::onStreamingStart(JSON* json)
{
    if (mDiagnosticsEnabled)
    {
        LOG_debug << "ActionPacketParser: Starting stream processing";
    }
    return true;
}

bool ActionPacketParser::onStreamingEnd(JSON* json)
{
    // Flush any pending data
    flushPendingData();
    
    if (mDiagnosticsEnabled)
    {
        LOG_debug << "ActionPacketParser: Finished stream processing";
        logProgress();
    }
    return true;
}

bool ActionPacketParser::onActionPacketArray(JSON* json)
{
    mInsideActionPacketArray = true;
    if (mDiagnosticsEnabled)
    {
        LOG_debug << "ActionPacketParser: Entering actionpacket array";
    }
    return true;
}

bool ActionPacketParser::onActionPacket(JSON* json)
{
    if (!mInsideActionPacketArray)
    {
        setError("Unexpected actionpacket outside of array");
        return false;
    }

    mInsideActionPacket = true;
    mCurrentPacketStart = json->pos - json->getjson();
    
    // Extract action type
    if (json->enterobject())
    {
        if (json->getnameid() == makeNameid("a"))
        {
            string actionType;
            json->storeobject(&actionType);
            mCurrentActionType = actionType;
            
            // Calculate packet size
            const char* start = json->getjson() + mCurrentPacketStart;
            size_t packetSize = strlen(start); // Simplified - in production would calculate exact size
            mStats.maxPacketSize = std::max(mStats.maxPacketSize, packetSize);
            
            // Check if we have a handler for this action type
            auto it = mPacketHandlers.find(actionType);
            if (it != mPacketHandlers.end())
            {
                // Reset JSON position to start of packet for handler
                JSON packetJson(json->getjson() + mCurrentPacketStart);
                bool result = it->second(&packetJson, actionType, packetSize);
                
                if (!result)
                {
                    setError("Packet handler failed for action type: " + actionType);
                    return false;
                }
            }
            
            mStats.packetsProcessed++;
            
            if (mDiagnosticsEnabled)
            {
                LOG_debug << "ActionPacketParser: Processing " << actionType 
                         << " packet (size: " << packetSize << ")";
            }
        }
        json->leaveobject();
    }
    
    mInsideActionPacket = false;
    return true;
}

bool ActionPacketParser::onTreeElement(JSON* json)
{
    mInsideTreeElement = true;
    mStats.largeElements++;
    
    if (mDiagnosticsEnabled)
    {
        LOG_debug << "ActionPacketParser: Processing large tree element";
    }
    
    // For large 't' elements, we could implement incremental processing here
    // For now, delegate to the node batch handler if available
    if (mNodeBatchHandler)
    {
        bool result = mNodeBatchHandler(json, ++mTreeElementBatchCount);
        mStats.treeBatchesProcessed++;
        
        if (!result)
        {
            setError("Node batch handler failed");
            return false;
        }
    }
    
    mInsideTreeElement = false;
    return true;
}

bool ActionPacketParser::onError(JSON* json)
{
    setError("JSON parsing error detected");
    return false;
}

// TreeElementProcessor implementation
void TreeElementProcessor::NodeBatch::clear()
{
    nodes.clear();
    totalSize = 0;
}

bool TreeElementProcessor::NodeBatch::addNode(JSON& nodeJson, size_t nodeSize)
{
    nodes.push_back(nodeJson);
    totalSize += nodeSize;
    return true;
}

bool TreeElementProcessor::NodeBatch::isFull(size_t maxSize, size_t maxCount) const
{
    return nodes.size() >= maxCount || totalSize >= maxSize;
}

TreeElementProcessor::TreeElementProcessor(size_t maxBatchSize, size_t maxBatchMemory)
    : mMaxBatchSize(maxBatchSize)
    , mMaxBatchMemory(maxBatchMemory)
{
}

bool TreeElementProcessor::processNode(JSON& nodeJson, std::function<bool(const NodeBatch&)> processor)
{
    size_t nodeSize = strlen(nodeJson.getvalue()); // Simplified size calculation
    
    // Add node to current batch
    mCurrentBatch.addNode(nodeJson, nodeSize);
    
    // Check if batch is full
    if (mCurrentBatch.isFull(mMaxBatchMemory, mMaxBatchSize))
    {
        // Process the batch
        bool result = processor(mCurrentBatch);
        if (!result)
        {
            return false;
        }
        
        mProcessedCount += mCurrentBatch.nodes.size();
        mBatchCount++;
        mCurrentBatch.clear();
    }
    
    return true;
}

bool TreeElementProcessor::flush(std::function<bool(const NodeBatch&)> processor)
{
    if (!mCurrentBatch.nodes.empty())
    {
        bool result = processor(mCurrentBatch);
        if (result)
        {
            mProcessedCount += mCurrentBatch.nodes.size();
            mBatchCount++;
        }
        mCurrentBatch.clear();
        return result;
    }
    return true;
}

} // namespace mega
