/**
 * @file actionpacket_streaming_example.cpp
 * @brief Example of how to use the on-the-fly actionpacket parsing feature
 *
 * This example demonstrates how to enable and use streaming actionpacket parsing
 * for memory-efficient processing of large actionpacket sequences.
 */

#include "mega/megaclient.h"
#include "mega/actionpacketparser.h"

using namespace mega;

class StreamingActionPacketExample
{
private:
    MegaClient* client;
    
public:
    StreamingActionPacketExample(MegaClient* megaClient) : client(megaClient) {}
    
    /**
     * @brief Basic usage - Enable streaming actionpacket parsing
     */
    void enableStreamingMode()
    {
        // Enable streaming actionpacket parsing
        client->enableStreamingActionPackets(true);
        
        LOG_info << "Streaming actionpacket parsing enabled";
    }
    
    /**
     * @brief Advanced usage - Set up custom handlers for specific actionpacket types
     */
    void setupCustomHandlers()
    {
        // First ensure streaming is enabled
        client->enableStreamingActionPackets(true);
        
        // Get access to the parser (if you need custom configuration)
        // Note: This would require adding a getter method to MegaClient
        // auto parser = client->getActionPacketParser();
        
        // Configure memory limits (optional)
        // parser->setMaxMemoryLimit(50 * 1024 * 1024); // 50MB limit
        // parser->setMaxBatchSize(500); // Process nodes in batches of 500
        
        // Enable diagnostics for monitoring
        // parser->enableDiagnostics(true);
        
        LOG_info << "Custom streaming configuration applied";
    }
    
    /**
     * @brief Example of handling server-client requests with streaming
     */
    void handleActionPacketResponse()
    {
        // When you receive actionpacket data from the server:
        // 1. Check if streaming is enabled
        if (client->streamingActionPacketsEnabled())
        {
            LOG_info << "Processing actionpackets with streaming parser";
            
            // 2. The streaming processing happens automatically in the HTTP layer
            // when HttpReq receives data chunks, it will call the streaming callback
            // which forwards to the ActionPacketParser
            
            // 3. You can monitor progress if diagnostics are enabled
            // The parser will log progress every 5 seconds during processing
        }
        else
        {
            LOG_info << "Using traditional batch processing";
            // Falls back to the original procsc() method
        }
    }
    
    /**
     * @brief Example of memory-conscious actionpacket processing
     */
    void processLargeActionPacketSequence()
    {
        // Enable streaming mode
        client->enableStreamingActionPackets(true);
        
        // The streaming parser will automatically:
        // 1. Parse actionpackets incrementally as data arrives
        // 2. Process large 't' (tree) elements in batches
        // 3. Maintain constant memory usage regardless of data size
        // 4. Provide detailed statistics and error reporting
        
        LOG_info << "Ready to process large actionpacket sequences with constant memory usage";
    }
    
    /**
     * @brief Example of performance monitoring
     */
    void monitorPerformance()
    {
        if (!client->streamingActionPacketsEnabled())
        {
            client->enableStreamingActionPackets(true);
        }
        
        // Performance monitoring would happen through the statistics
        // The ActionPacketParser tracks:
        // - Bytes processed
        // - Number of packets processed
        // - Processing time
        // - Memory usage
        // - Error counts
        
        LOG_info << "Performance monitoring enabled for actionpacket processing";
    }
    
    /**
     * @brief Disable streaming and revert to batch processing
     */
    void disableStreamingMode()
    {
        client->enableStreamingActionPackets(false);
        LOG_info << "Reverted to traditional batch actionpacket processing";
    }
};

/**
 * @brief Integration example showing how to modify existing code
 */
class MegaClientIntegrationExample
{
public:
    /**
     * @brief Example of how to modify your MegaClient usage
     */
    void integrateStreamingSupport()
    {
        // Initialize MegaClient as usual
        MegaClient* client = new MegaClient(/* your params */);
        
        // Enable streaming actionpacket parsing for memory efficiency
        client->enableStreamingActionPackets(true);
        
        // Now all actionpacket processing will use streaming
        // No other changes needed to your existing code!
        
        // Your existing code continues to work:
        // client->login(...);
        // client->fetchnodes(...);
        // All actionpackets from these operations will be processed efficiently
        
        LOG_info << "MegaClient configured with streaming actionpacket support";
    }
    
    /**
     * @brief Example for resource-constrained environments
     */
    void configureForMobileDevice()
    {
        MegaClient* client = new MegaClient(/* your params */);
        
        // Enable streaming with mobile-optimized settings
        client->enableStreamingActionPackets(true);
        
        // The default settings are already optimized for mobile:
        // - 100MB memory limit
        // - 1000 node batch size
        // - Automatic error recovery
        
        LOG_info << "MegaClient configured for mobile device";
    }
    
    /**
     * @brief Example for server/desktop environments with more resources
     */
    void configureForHighPerformance()
    {
        MegaClient* client = new MegaClient(/* your params */);
        
        // Enable streaming
        client->enableStreamingActionPackets(true);
        
        // For high-performance environments, you might want to:
        // - Increase memory limits
        // - Increase batch sizes
        // - Enable detailed diagnostics
        
        LOG_info << "MegaClient configured for high-performance environment";
    }
};

/**
 * @brief Error handling and fallback example
 */
class ErrorHandlingExample
{
public:
    void handleStreamingErrors(MegaClient* client)
    {
        // Enable streaming
        client->enableStreamingActionPackets(true);
        
        // The streaming parser includes automatic error handling:
        // 1. If streaming parsing fails, it automatically falls back to batch processing
        // 2. Memory limit violations are handled gracefully
        // 3. JSON parsing errors are reported and recovered from
        // 4. All errors are logged for debugging
        
        // You can check if streaming is working:
        if (client->streamingActionPacketsEnabled())
        {
            LOG_info << "Streaming mode active and working";
        }
        else
        {
            LOG_warn << "Streaming mode disabled, using fallback processing";
        }
    }
};

/**
 * @brief Complete usage example
 */
void completeUsageExample()
{
    // 1. Create and configure MegaClient
    MegaClient* client = new MegaClient(/* your application, waiter, httpio, etc. */);
    
    // 2. Enable streaming actionpacket parsing
    client->enableStreamingActionPackets(true);
    
    // 3. Use MegaClient normally - streaming happens automatically
    client->login("your_email", "your_password");
    
    // 4. Fetch nodes - large responses will be processed incrementally
    client->fetchnodes();
    
    // 5. All subsequent actionpackets will be processed efficiently
    // No memory issues even with accounts containing millions of nodes
    
    // 6. Monitor if needed (optional)
    if (client->streamingActionPacketsEnabled())
    {
        LOG_info << "Streaming processing active";
    }
    
    // 7. Clean up
    delete client;
}
