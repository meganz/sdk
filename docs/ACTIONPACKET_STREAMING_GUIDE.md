# On-the-Fly Actionpacket Parsing - Usage Guide

## Overview

The on-the-fly actionpacket parsing feature provides memory-efficient, incremental processing of actionpacket sequences from the MEGA API. This is particularly useful for:

- Resource-constrained devices (mobile phones, IoT devices)
- Accounts with large numbers of nodes
- Long-running clients that accumulate many actionpackets
- Reducing memory footprint and improving responsiveness

## Quick Start

### Basic Usage

```cpp
#include "mega/megaclient.h"

// Initialize MegaClient as usual
MegaClient* client = new MegaClient(app, waiter, httpio, fsaccess, dbaccess, gfx, clientname, appkey);

// Enable streaming actionpacket parsing
client->enableStreamingActionPackets(true);

// Use MegaClient normally - streaming happens automatically
client->login("your_email", "your_password");
client->fetchnodes(); // Large responses processed incrementally
```

### Check if Streaming is Active

```cpp
if (client->streamingActionPacketsEnabled()) {
    LOG_info << "Using memory-efficient streaming processing";
} else {
    LOG_info << "Using traditional batch processing";
}
```

### Disable Streaming (Revert to Batch Processing)

```cpp
client->enableStreamingActionPackets(false);
```

## How It Works

### Architecture

1. **JSONSplitter Integration**: Extends the proven JSONSplitter pattern used by CommandFetchNodes
2. **Streaming HTTP Layer**: HttpReq supports incremental data callbacks
3. **ActionPacketParser**: Memory-bounded parser with configurable limits
4. **Automatic Fallback**: Gracefully falls back to batch processing on errors

### Processing Flow

```
Network Data → HttpReq → Streaming Callback → ActionPacketParser → MegaClient
     ↓              ↓            ↓                    ↓              ↓
  Chunks        Buffering    Incremental         Individual      Normal
  Received      + Stream     JSON Parsing        Packets         Processing
```

### Memory Management

- **Constant Memory**: Memory usage remains constant regardless of actionpacket size
- **Configurable Limits**: Default 100MB limit, adjustable per application needs
- **Batch Processing**: Large 't' elements processed in batches (default 1000 nodes)
- **Automatic Cleanup**: Memory is automatically freed as packets are processed

## Configuration Options

### Memory Limits

The parser includes configurable memory limits to prevent excessive memory usage:

```cpp
// Default settings (already optimized for most use cases):
// - Max memory limit: 100MB
// - Max batch size: 1000 nodes
// - Diagnostics: disabled
```

### For Mobile Devices

```cpp
client->enableStreamingActionPackets(true);
// Default settings are already optimized for mobile
```

### For High-Performance Servers

```cpp
client->enableStreamingActionPackets(true);
// Consider higher limits for server environments if needed
```

## Integration with Existing Code

### No Code Changes Required

The streaming parser is designed to be a drop-in replacement:

```cpp
// Existing code works unchanged:
client->login(email, password);
client->fetchnodes();
client->syncadd(sync);

// All actionpackets are now processed efficiently
```

### Backward Compatibility

- All existing APIs work unchanged
- Automatic fallback to batch processing on errors
- Can be enabled/disabled at runtime
- No impact on application logic

## Performance Benefits

### Memory Usage

- **Before**: Memory usage grows with actionpacket size (can exceed 1GB for large accounts)
- **After**: Constant memory usage (~100MB default limit)

### Processing Speed

- **Incremental**: Begin processing as soon as data arrives
- **Reduced Latency**: No waiting for complete download before processing
- **Better Responsiveness**: UI remains responsive during large updates

### Network Efficiency

- **Streaming**: Process data as it arrives over the network
- **Early Processing**: Start handling actionpackets before download completes
- **Bandwidth Optimization**: Better utilization of available bandwidth

## Error Handling

### Automatic Recovery

The system includes comprehensive error handling:

```cpp
// Automatic fallback on errors
if (streaming_parser_fails) {
    LOG_warn << "Streaming failed, falling back to batch processing";
    // Automatically switches to traditional procsc()
}
```

### Error Types Handled

1. **JSON Parsing Errors**: Malformed JSON, incomplete packets
2. **Memory Limit Violations**: Automatic cleanup and fallback
3. **Network Errors**: Retry and recovery mechanisms
4. **State Corruption**: Parser reset and reinitialize

### Monitoring and Diagnostics

```cpp
// Enable diagnostics for troubleshooting
parser->enableDiagnostics(true);

// Logs progress every 5 seconds:
// "ActionPacketParser progress: bytes=1048576 packets=150 memory=25MB"
```

## Use Cases

### Large Account Synchronization

```cpp
// Perfect for accounts with millions of nodes
client->enableStreamingActionPackets(true);
client->fetchnodes(); // Processes incrementally, constant memory
```

### Long-Running Clients

```cpp
// Ideal for clients that stay online and accumulate actionpackets
client->enableStreamingActionPackets(true);
// All subsequent actionpackets processed efficiently
```

### Mobile Applications

```cpp
// Optimized for mobile devices with limited memory
client->enableStreamingActionPackets(true);
// Default 100MB limit prevents OOM crashes
```

### Server Applications

```cpp
// Suitable for server applications handling multiple accounts
client->enableStreamingActionPackets(true);
// Scales to handle any account size
```

## Troubleshooting

### Common Issues

1. **Memory Warnings**: Adjust memory limits if needed
2. **Processing Delays**: Enable diagnostics to monitor progress
3. **Fallback to Batch**: Check logs for specific error causes

### Debug Information

```cpp
// Check parser state
if (client->streamingActionPacketsEnabled()) {
    // Streaming is active
    LOG_debug << "Streaming actionpacket parsing active";
} else {
    // Using batch processing
    LOG_debug << "Using batch actionpacket processing";
}
```

### Performance Monitoring

The parser automatically tracks:
- Bytes processed
- Number of actionpackets handled
- Processing time and speed
- Memory usage patterns
- Error counts and types

## Migration Guide

### From Existing Code

1. **Add one line**: `client->enableStreamingActionPackets(true);`
2. **No other changes needed**: All existing code continues to work
3. **Test thoroughly**: Verify behavior with your specific use cases
4. **Monitor performance**: Check memory usage and processing speed

### Gradual Rollout

```cpp
// Enable for specific scenarios first
if (large_account_detected) {
    client->enableStreamingActionPackets(true);
}

// Or enable based on device capabilities
if (mobile_device && low_memory) {
    client->enableStreamingActionPackets(true);
}
```

## Best Practices

1. **Enable Early**: Enable streaming before login/fetchnodes for maximum benefit
2. **Monitor Memory**: Watch memory usage in your application
3. **Test Extensively**: Test with various account sizes and network conditions
4. **Handle Errors**: Implement proper error handling for network issues
5. **Performance Testing**: Benchmark against your specific use cases

## API Reference

### MegaClient Methods

```cpp
// Enable/disable streaming actionpacket parsing
void enableStreamingActionPackets(bool enable = true);

// Check if streaming is enabled
bool streamingActionPacketsEnabled() const;

// Internal streaming entry point (called automatically)
bool procsc_streaming(const char* data, size_t len);
```

### HttpReq Streaming Support

```cpp
// Set streaming callback for incremental processing
void setStreamingCallback(StreamingCallback callback);

// Enable/disable streaming mode
void enableStreaming(bool enable = true);
```

This feature provides a production-ready solution for memory-efficient actionpacket processing, with automatic fallback and comprehensive error handling.
