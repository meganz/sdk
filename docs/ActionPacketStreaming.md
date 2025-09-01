# ActionPacket Streaming Parser - Usage Guide

## Overview
The ActionPacket Streaming Parser provides memory-efficient processing of large actionpacket sequences from the MEGA API. Instead of loading entire actionpacket responses into memory, it processes them incrementally as data arrives.

## Quick Start

### Enable Streaming
```cpp
#include "mega/megaclient.h"
#include "mega/actionpacketparser.h"

// Create MegaClient instance
MegaClient client(app, waiter, httpio, fs, dbaccess, gfx, "MyApp");

// Enable streaming actionpacket processing
client.enableStreamingActionPackets(true);

// Setup custom packet handler (optional)
client.setActionPacketHandler([](const string& packet) {
    // Process individual actionpacket
    JSON json;
    json.begin(packet.c_str());
    
    if (json.enterobject()) {
        string action;
        if (json.storeobject(&action)) {
            LOG_info << "Received action: " << action;
        }
    }
});
```

### Check Streaming Status
```cpp
if (client.streamingActionPacketsEnabled()) {
    LOG_info << "Streaming actionpackets enabled";
}
```

## Configuration Options

### Memory Limits
```cpp
// Configure before enabling streaming
ActionPacketParser parser(client);
parser.setMemoryLimit(50 * 1024 * 1024);  // 50MB limit
parser.setMaxPacketSize(10 * 1024 * 1024); // 10MB per packet
```

### Tree Element Processing
For accounts with millions of nodes, the parser automatically enables tree element streaming:
```cpp
// Automatically enabled for large 't' elements in actionpackets
// No additional configuration required
```

## Advanced Usage

### Custom Packet Handler
```cpp
class MyPacketHandler {
public:
    void handlePacket(const string& packet) {
        JSON json;
        json.begin(packet.c_str());
        
        if (json.enterobject()) {
            string action;
            if (json.storeobject(&action)) {
                if (action == "t") {
                    handleTreeAction(json);
                } else if (action == "u") {
                    handleUserAction(json);
                }
            }
        }
    }
    
private:
    void handleTreeAction(JSON& json) {
        // Process tree/node updates
    }
    
    void handleUserAction(JSON& json) {
        // Process user-related updates
    }
};

MyPacketHandler handler;
client.setActionPacketHandler([&handler](const string& packet) {
    handler.handlePacket(packet);
});
```

### Monitoring and Statistics
```cpp
// Get processing statistics
auto stats = client.getActionPacketStats();
LOG_info << "Packets processed: " << stats.packetsProcessed;
LOG_info << "Bytes processed: " << stats.bytesProcessed;
LOG_info << "Memory peak: " << stats.memoryPeak;
LOG_info << "Processing time: " << stats.totalProcessingTime << "ms";

// Check if tree streaming was used
if (stats.treeStreamingUsed) {
    LOG_info << "Large tree elements were streamed";
}
```

### Error Handling
```cpp
// Enable error callbacks
client.setActionPacketErrorHandler([](const string& error, bool recovered) {
    if (recovered) {
        LOG_warn << "Streaming error recovered: " << error;
    } else {
        LOG_err << "Streaming error: " << error;
    }
});
```

## Production Deployment

### Memory Monitoring
```cpp
// Monitor memory usage in production
class MemoryMonitor {
    Timer checkTimer;
    size_t maxMemory = 100 * 1024 * 1024; // 100MB limit
    
public:
    void startMonitoring(MegaClient& client) {
        checkTimer.set([&client, this]() {
            auto stats = client.getActionPacketStats();
            if (stats.memoryPeak > maxMemory) {
                LOG_warn << "High memory usage: " << stats.memoryPeak;
                // Could trigger memory cleanup or restart
            }
        }, 30000); // Check every 30 seconds
    }
};
```

### Performance Optimization
```cpp
// For high-throughput scenarios
client.enableStreamingActionPackets(true);

// Adjust buffer sizes based on network conditions
if (isSlowNetwork()) {
    // Smaller buffers for slow networks
    parser.setMaxPacketSize(1 * 1024 * 1024); // 1MB
} else {
    // Larger buffers for fast networks
    parser.setMaxPacketSize(10 * 1024 * 1024); // 10MB
}
```

### Error Recovery
```cpp
// Automatic fallback on streaming errors
client.setActionPacketErrorHandler([](const string& error, bool recovered) {
    if (!recovered) {
        LOG_err << "Streaming failed, using batch processing: " << error;
        // Streaming automatically falls back to batch processing
        // No manual intervention required
    }
});
```

## Best Practices

### 1. Enable Early
Enable streaming before making requests that might return large actionpacket sequences:
```cpp
client.enableStreamingActionPackets(true);
client.login(email, password); // Will use streaming for subsequent actionpackets
```

### 2. Monitor Memory
Set appropriate memory limits based on your application's constraints:
```cpp
// For mobile apps
parser.setMemoryLimit(20 * 1024 * 1024); // 20MB

// For desktop apps
parser.setMemoryLimit(100 * 1024 * 1024); // 100MB

// For server applications
parser.setMemoryLimit(500 * 1024 * 1024); // 500MB
```

### 3. Handle Large Accounts
For accounts with millions of files, streaming is essential:
```cpp
// Automatically handled - no special configuration needed
// Parser detects large tree elements and streams them
```

### 4. Error Logging
Enable comprehensive logging for production debugging:
```cpp
client.setActionPacketErrorHandler([](const string& error, bool recovered) {
    // Log to your application's logging system
    MyLogger::logError("ActionPacket", error, recovered);
});
```

## Common Use Cases

### 1. Mobile Applications
```cpp
// Limited memory environment
client.enableStreamingActionPackets(true);
parser.setMemoryLimit(10 * 1024 * 1024); // 10MB limit
```

### 2. Desktop Sync Clients
```cpp
// Balance memory and performance
client.enableStreamingActionPackets(true);
parser.setMemoryLimit(50 * 1024 * 1024); // 50MB limit
```

### 3. Server Applications
```cpp
// High throughput scenarios
client.enableStreamingActionPackets(true);
parser.setMemoryLimit(200 * 1024 * 1024); // 200MB limit

// Enable detailed monitoring
startPerformanceMonitoring(client);
```

### 4. Batch Processing
```cpp
// When streaming isn't needed
client.enableStreamingActionPackets(false);
// Uses traditional batch processing for all actionpackets
```

## Troubleshooting

### High Memory Usage
```cpp
// Check current settings
auto stats = client.getActionPacketStats();
if (stats.memoryPeak > expectedLimit) {
    // Reduce memory limits
    parser.setMemoryLimit(stats.memoryPeak / 2);
    parser.setMaxPacketSize(1 * 1024 * 1024);
}
```

### Slow Processing
```cpp
// Check if tree streaming is being used
auto stats = client.getActionPacketStats();
if (!stats.treeStreamingUsed && stats.packetsProcessed > 1000) {
    LOG_info << "Consider enabling tree streaming for better performance";
}
```

### Network Issues
```cpp
// Streaming handles network interruptions automatically
// No special handling required - falls back to batch processing
```

## Migration from Batch Processing

### Existing Code
```cpp
// Old approach - loads everything into memory
client.enableStreamingActionPackets(false);
// Process after complete download
```

### Updated Code
```cpp
// New approach - processes incrementally
client.enableStreamingActionPackets(true);

// Optional: Add packet handler for immediate processing
client.setActionPacketHandler([](const string& packet) {
    // Process packets as they arrive
    processActionPacket(packet);
});
```

The streaming parser is fully backward compatible - existing code will continue to work without changes while gaining the memory efficiency benefits.
