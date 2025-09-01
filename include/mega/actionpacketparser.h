#pragma once

#include "mega/json.h"
#include "mega/types.h"
#include "mega/node.h"

#include <functional>
#include <memory>
#include <map>
#include <string>
#include <unordered_map>

namespace mega {

class MegaClient;

/**
 * @brief Streaming parser for actionpackets using the proven JSONSplitter pattern
 * 
 * This class extends the existing JSONSplitter to provide memory-efficient,
 * incremental parsing of actionpacket sequences. It can handle arbitrarily
 * large actionpacket responses without loading the entire JSON into memory.
 * 
 * Features:
 * - Memory-bounded processing (constant memory footprint)
 * - Incremental parsing of 't' elements (large node trees)
 * - Error recovery and fallback mechanisms  
 * - Production-level diagnostics and monitoring
 */
class MEGA_API ActionPacketParser : public JSONSplitter
{
public:
    // Callback for processing individual actionpackets
    using PacketHandler = std::function<bool(JSON*, const string& actionType, size_t packetSize)>;
    
    // Callback for processing large 't' elements incrementally
    using NodeBatchHandler = std::function<bool(JSON*, size_t batchCount)>;

    // Statistics for monitoring and debugging
    struct Stats
    {
        size_t bytesProcessed = 0;
        size_t packetsProcessed = 0;
        size_t partialPackets = 0;
        size_t largeElements = 0;
        size_t treeBatchesProcessed = 0;
        size_t maxPacketSize = 0;
        size_t totalMemoryUsed = 0;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::milliseconds totalProcessingTime{0};
        
        void reset();
        void toJson(string& output) const;
    };

    ActionPacketParser(MegaClient* client);
    ~ActionPacketParser();

    // Process a chunk of actionpacket JSON data
    bool processChunk(const char* data, size_t len);

    // Set packet handler for specific actionpacket types
    void setPacketHandler(const string& actionType, PacketHandler handler);

    // Set handler for large 't' element batches
    void setNodeBatchHandler(NodeBatchHandler handler);

    // Configuration
    void setMaxMemoryLimit(size_t bytes) { mMaxMemoryLimit = bytes; }
    void setMaxBatchSize(size_t count) { mMaxBatchSize = count; }
    void enableDiagnostics(bool enable) { mDiagnosticsEnabled = enable; }

    // State and statistics
    const Stats& getStats() const { return mStats; }
    bool hasError() const { return mHasError; }
    const string& getLastError() const { return mLastError; }
    
    // Memory and performance monitoring
    size_t getCurrentMemoryUsage() const;
    void dumpState(string& output) const;

    // Reset parser state for reuse
    void reset();

protected:
    // Override JSONSplitter for custom actionpacket processing
    bool processActionPacketArray();
    bool processActionPacket();
    bool processTreeElement(JSON& json);
    
    // Memory management for large elements
    bool checkMemoryLimits();
    void flushPendingData();
    
    // Error handling
    void setError(const string& error);
    void logProgress() const;

private:
    MegaClient* mClient;
    
    // Parser state
    bool mInsideActionPacketArray = false;
    bool mInsideActionPacket = false;
    size_t mCurrentPacketStart = 0;
    size_t mCurrentPacketDepth = 0;
    string mCurrentActionType;
    
    // Tree element handling
    bool mInsideTreeElement = false;
    size_t mTreeElementBatchCount = 0;
    string mTreeElementBuffer;
    
    // Handlers
    std::unordered_map<string, PacketHandler> mPacketHandlers;
    NodeBatchHandler mNodeBatchHandler;
    
    // Configuration
    size_t mMaxMemoryLimit = 100 * 1024 * 1024; // 100MB default
    size_t mMaxBatchSize = 1000; // Process nodes in batches of 1000
    bool mDiagnosticsEnabled = false;
    
    // Error handling
    bool mHasError = false;
    string mLastError;
    
    // Statistics and monitoring
    mutable Stats mStats;
    std::chrono::steady_clock::time_point mLastProgressLog;
    
    // Streaming filters for JSONSplitter
    std::map<string, std::function<bool(JSON*)>> mFilters;
    
    // Initialize streaming filters
    void setupFilters();
    
    // Filter callbacks
    bool onStreamingStart(JSON* json);
    bool onStreamingEnd(JSON* json);
    bool onActionPacketArray(JSON* json);
    bool onActionPacket(JSON* json);
    bool onTreeElement(JSON* json);
    bool onError(JSON* json);
};

/**
 * @brief Helper class for incremental processing of large 't' elements
 * 
 * This class provides batch processing of node trees within actionpackets
 * to prevent memory exhaustion when processing very large updates.
 */
class MEGA_API TreeElementProcessor
{
public:
    struct NodeBatch
    {
        std::vector<JSON> nodes;
        size_t totalSize = 0;
        
        void clear();
        bool addNode(JSON& nodeJson, size_t nodeSize);
        bool isFull(size_t maxSize, size_t maxCount) const;
    };

    TreeElementProcessor(size_t maxBatchSize = 1000, size_t maxBatchMemory = 10 * 1024 * 1024);
    
    // Process nodes incrementally
    bool processNode(JSON& nodeJson, std::function<bool(const NodeBatch&)> processor);
    
    // Flush any remaining nodes
    bool flush(std::function<bool(const NodeBatch&)> processor);
    
    // Statistics
    size_t getProcessedCount() const { return mProcessedCount; }
    size_t getBatchCount() const { return mBatchCount; }

private:
    NodeBatch mCurrentBatch;
    size_t mMaxBatchSize;
    size_t mMaxBatchMemory;
    size_t mProcessedCount = 0;
    size_t mBatchCount = 0;
};

} // namespace mega
