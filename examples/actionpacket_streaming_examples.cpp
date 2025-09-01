/**
 * @file actionpacket_streaming_examples.cpp
 * @brief Comprehensive examples for ActionPacket Streaming Parser
 */

#include "mega/megaclient.h"
#include "mega/actionpacketparser.h"
#include <memory>
#include <chrono>

using namespace mega;
using namespace std;

/**
 * Example 1: Basic Streaming Setup
 * Demonstrates the simplest way to enable actionpacket streaming
 */
class BasicStreamingExample {
public:
    void setup(MegaClient& client) {
        LOG_info << "Setting up basic actionpacket streaming";
        
        // Enable streaming - this is all you need for basic usage
        client.enableStreamingActionPackets(true);
        
        // Verify it's enabled
        if (client.streamingActionPacketsEnabled()) {
            LOG_info << "Streaming actionpackets successfully enabled";
        }
    }
};

/**
 * Example 2: Custom Packet Processing
 * Shows how to process individual actionpackets as they arrive
 */
class CustomPacketProcessingExample {
private:
    size_t packetsProcessed = 0;
    size_t treeUpdates = 0;
    size_t userUpdates = 0;
    
public:
    void setup(MegaClient& client) {
        LOG_info << "Setting up custom packet processing";
        
        // Enable streaming
        client.enableStreamingActionPackets(true);
        
        // Set up custom packet handler
        client.setActionPacketHandler([this](const string& packet) {
            this->handleActionPacket(packet);
        });
    }
    
private:
    void handleActionPacket(const string& packet) {
        packetsProcessed++;
        
        JSON json;
        json.begin(packet.c_str());
        
        if (!json.enterobject()) {
            LOG_warn << "Invalid actionpacket JSON";
            return;
        }
        
        // Parse the action type
        string action;
        if (json.storeobject(&action)) {
            if (action == "t") {
                handleTreeUpdate(json);
                treeUpdates++;
            } else if (action == "u") {
                handleUserUpdate(json);
                userUpdates++;
            } else {
                LOG_debug << "Unknown action type: " << action;
            }
        }
        
        // Log progress every 100 packets
        if (packetsProcessed % 100 == 0) {
            LOG_info << "Processed " << packetsProcessed << " packets "
                     << "(tree: " << treeUpdates << ", user: " << userUpdates << ")";
        }
    }
    
    void handleTreeUpdate(JSON& json) {
        // Process tree/node updates
        // This could be node additions, modifications, deletions
        LOG_debug << "Processing tree update";
    }
    
    void handleUserUpdate(JSON& json) {
        // Process user-related updates
        // This could be contacts, shares, etc.
        LOG_debug << "Processing user update";
    }
};

/**
 * Example 3: Memory-Constrained Environment
 * Perfect for mobile applications or embedded systems
 */
class MobileOptimizedExample {
public:
    void setup(MegaClient& client) {
        LOG_info << "Setting up mobile-optimized streaming";
        
        // Configure for limited memory environment
        auto parser = client.getActionPacketParser();
        if (parser) {
            // Limit memory usage to 10MB
            parser->setMemoryLimit(10 * 1024 * 1024);
            
            // Smaller packet size for better responsiveness
            parser->setMaxPacketSize(1 * 1024 * 1024);
        }
        
        // Enable streaming
        client.enableStreamingActionPackets(true);
        
        // Set up memory monitoring
        client.setActionPacketErrorHandler([](const string& error, bool recovered) {
            if (!recovered) {
                LOG_warn << "Memory limit reached, falling back to batch processing";
            }
        });
    }
};

/**
 * Example 4: High-Performance Server Application
 * Optimized for throughput and monitoring
 */
class ServerOptimizedExample {
private:
    chrono::steady_clock::time_point startTime;
    size_t totalBytes = 0;
    size_t totalPackets = 0;
    
public:
    void setup(MegaClient& client) {
        LOG_info << "Setting up server-optimized streaming";
        startTime = chrono::steady_clock::now();
        
        // Configure for high throughput
        auto parser = client.getActionPacketParser();
        if (parser) {
            // Higher memory limit for server environment
            parser->setMemoryLimit(200 * 1024 * 1024);
            
            // Larger packet size for efficiency
            parser->setMaxPacketSize(10 * 1024 * 1024);
        }
        
        // Enable streaming
        client.enableStreamingActionPackets(true);
        
        // Set up performance monitoring
        client.setActionPacketHandler([this](const string& packet) {
            this->trackPerformance(packet);
        });
        
        // Set up error monitoring
        client.setActionPacketErrorHandler([this](const string& error, bool recovered) {
            this->handleError(error, recovered);
        });
    }
    
    void printStats() {
        auto now = chrono::steady_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(now - startTime);
        
        double throughputMBps = (double(totalBytes) / (1024 * 1024)) / (duration.count() / 1000.0);
        double packetsPerSecond = double(totalPackets) / (duration.count() / 1000.0);
        
        LOG_info << "Performance Stats:";
        LOG_info << "  Total packets: " << totalPackets;
        LOG_info << "  Total bytes: " << totalBytes;
        LOG_info << "  Duration: " << duration.count() << "ms";
        LOG_info << "  Throughput: " << throughputMBps << " MB/s";
        LOG_info << "  Packets/sec: " << packetsPerSecond;
    }
    
private:
    void trackPerformance(const string& packet) {
        totalPackets++;
        totalBytes += packet.size();
        
        // Log performance every 1000 packets
        if (totalPackets % 1000 == 0) {
            printStats();
        }
    }
    
    void handleError(const string& error, bool recovered) {
        if (recovered) {
            LOG_warn << "Streaming error recovered: " << error;
        } else {
            LOG_err << "Critical streaming error: " << error;
            // Could trigger alerts or restart procedures
        }
    }
};

/**
 * Example 5: Large Account Processing
 * Handles accounts with millions of files efficiently
 */
class LargeAccountExample {
private:
    size_t nodeCount = 0;
    size_t largeTreeElements = 0;
    
public:
    void setup(MegaClient& client) {
        LOG_info << "Setting up large account processing";
        
        // Configure for large accounts
        auto parser = client.getActionPacketParser();
        if (parser) {
            // Generous memory limit for large accounts
            parser->setMemoryLimit(500 * 1024 * 1024);
            
            // Enable tree element streaming (automatically enabled for large elements)
            LOG_info << "Tree element streaming will be automatically enabled for large responses";
        }
        
        // Enable streaming
        client.enableStreamingActionPackets(true);
        
        // Set up specialized handler for large accounts
        client.setActionPacketHandler([this](const string& packet) {
            this->handleLargeAccountPacket(packet);
        });
    }
    
private:
    void handleLargeAccountPacket(const string& packet) {
        JSON json;
        json.begin(packet.c_str());
        
        if (!json.enterobject()) return;
        
        string action;
        if (json.storeobject(&action) && action == "t") {
            // This is a tree update - count nodes
            if (json.enterarray()) {
                while (json.enterobject()) {
                    nodeCount++;
                    
                    // Check if this is a large tree element
                    if (packet.size() > 1024 * 1024) { // 1MB threshold
                        largeTreeElements++;
                        LOG_debug << "Processing large tree element: " << packet.size() << " bytes";
                    }
                    
                    json.leaveobject();
                }
                json.leavearray();
            }
            
            // Log progress for large operations
            if (nodeCount % 10000 == 0) {
                LOG_info << "Processed " << nodeCount << " nodes, "
                         << largeTreeElements << " large tree elements";
            }
        }
    }
};

/**
 * Example 6: Development and Debugging
 * Shows comprehensive logging and debugging features
 */
class DebugExample {
public:
    void setup(MegaClient& client) {
        LOG_info << "Setting up debug-enabled streaming";
        
        // Enable streaming
        client.enableStreamingActionPackets(true);
        
        // Set up comprehensive debugging
        client.setActionPacketHandler([this](const string& packet) {
            this->debugPacket(packet);
        });
        
        client.setActionPacketErrorHandler([this](const string& error, bool recovered) {
            this->debugError(error, recovered);
        });
        
        // Log configuration
        auto parser = client.getActionPacketParser();
        if (parser) {
            LOG_debug << "Parser memory limit: " << parser->getMemoryLimit();
            LOG_debug << "Parser max packet size: " << parser->getMaxPacketSize();
        }
    }
    
private:
    void debugPacket(const string& packet) {
        LOG_debug << "Received packet: " << packet.size() << " bytes";
        
        // Parse and log action type
        JSON json;
        json.begin(packet.c_str());
        if (json.enterobject()) {
            string action;
            if (json.storeobject(&action)) {
                LOG_debug << "Action type: " << action;
            }
        }
        
        // Log first 100 characters for debugging
        string preview = packet.substr(0, min(packet.size(), size_t(100)));
        LOG_debug << "Packet preview: " << preview << "...";
    }
    
    void debugError(const string& error, bool recovered) {
        LOG_debug << "Streaming error: " << error << " (recovered: " << recovered << ")";
    }
};

/**
 * Example 7: Migration from Batch Processing
 * Shows how to migrate existing code to streaming
 */
class MigrationExample {
public:
    // Old approach - batch processing
    void oldApproach(MegaClient& client) {
        LOG_info << "Using old batch processing approach";
        
        // Disable streaming (default behavior)
        client.enableStreamingActionPackets(false);
        
        // All actionpackets are processed after complete download
        // Higher memory usage, but simpler processing model
    }
    
    // New approach - streaming
    void newApproach(MegaClient& client) {
        LOG_info << "Using new streaming approach";
        
        // Enable streaming
        client.enableStreamingActionPackets(true);
        
        // Optional: Add packet handler for immediate processing
        client.setActionPacketHandler([](const string& packet) {
            // Process packets as they arrive
            // This is optional - you can still use the old processing model
            processActionPacketStreaming(packet);
        });
    }
    
private:
    static void processActionPacketStreaming(const string& packet) {
        // Your existing packet processing logic can be moved here
        // for immediate processing as packets arrive
        LOG_debug << "Processing packet immediately: " << packet.size() << " bytes";
    }
};

/**
 * Example 8: Complete Application Integration
 * Shows a complete application using all features
 */
class CompleteApplicationExample {
private:
    unique_ptr<MegaClient> client;
    chrono::steady_clock::time_point sessionStart;
    
    // Statistics
    struct Stats {
        size_t totalPackets = 0;
        size_t totalBytes = 0;
        size_t treeUpdates = 0;
        size_t userUpdates = 0;
        size_t errors = 0;
        size_t recoveredErrors = 0;
    } stats;
    
public:
    void initializeApplication(MegaApp* app, Waiter* waiter, HttpIO* httpio,
                              FileSystemAccess* fs, DbAccess* dbaccess,
                              GfxProc* gfx) {
        LOG_info << "Initializing complete application with streaming";
        sessionStart = chrono::steady_clock::now();
        
        // Create MegaClient
        client = make_unique<MegaClient>(app, waiter, httpio, fs, dbaccess, gfx, "StreamingApp");
        
        // Configure streaming
        setupStreaming();
        
        // Setup monitoring
        setupMonitoring();
    }
    
    void login(const string& email, const string& password) {
        LOG_info << "Logging in with streaming enabled";
        client->login(email.c_str(), password.c_str());
    }
    
    void getStats() const {
        auto now = chrono::steady_clock::now();
        auto duration = chrono::duration_cast<chrono::seconds>(now - sessionStart);
        
        LOG_info << "Session Statistics:";
        LOG_info << "  Duration: " << duration.count() << " seconds";
        LOG_info << "  Total packets: " << stats.totalPackets;
        LOG_info << "  Total bytes: " << stats.totalBytes;
        LOG_info << "  Tree updates: " << stats.treeUpdates;
        LOG_info << "  User updates: " << stats.userUpdates;
        LOG_info << "  Errors: " << stats.errors;
        LOG_info << "  Recovered errors: " << stats.recoveredErrors;
        
        if (duration.count() > 0) {
            LOG_info << "  Packets/sec: " << stats.totalPackets / duration.count();
            LOG_info << "  Bytes/sec: " << stats.totalBytes / duration.count();
        }
    }
    
private:
    void setupStreaming() {
        // Configure parser
        auto parser = client->getActionPacketParser();
        if (parser) {
            // Balanced settings for general use
            parser->setMemoryLimit(50 * 1024 * 1024); // 50MB
            parser->setMaxPacketSize(5 * 1024 * 1024); // 5MB
        }
        
        // Enable streaming
        client->enableStreamingActionPackets(true);
        
        // Set up packet handler
        client->setActionPacketHandler([this](const string& packet) {
            this->handlePacket(packet);
        });
        
        // Set up error handler
        client->setActionPacketErrorHandler([this](const string& error, bool recovered) {
            this->handleError(error, recovered);
        });
    }
    
    void setupMonitoring() {
        // Could set up periodic statistics reporting
        // Timer to print stats every 5 minutes
        // (Timer implementation depends on your application framework)
    }
    
    void handlePacket(const string& packet) {
        stats.totalPackets++;
        stats.totalBytes += packet.size();
        
        // Parse action type
        JSON json;
        json.begin(packet.c_str());
        if (json.enterobject()) {
            string action;
            if (json.storeobject(&action)) {
                if (action == "t") {
                    stats.treeUpdates++;
                    handleTreeUpdate(json);
                } else if (action == "u") {
                    stats.userUpdates++;
                    handleUserUpdate(json);
                }
            }
        }
    }
    
    void handleTreeUpdate(JSON& json) {
        // Application-specific tree update processing
        LOG_debug << "Processing tree update";
    }
    
    void handleUserUpdate(JSON& json) {
        // Application-specific user update processing
        LOG_debug << "Processing user update";
    }
    
    void handleError(const string& error, bool recovered) {
        stats.errors++;
        if (recovered) {
            stats.recoveredErrors++;
            LOG_warn << "Streaming error recovered: " << error;
        } else {
            LOG_err << "Critical streaming error: " << error;
        }
    }
};

/**
 * Main function demonstrating all examples
 */
int main() {
    // Note: This is a demonstration - you'd need to initialize
    // MegaClient dependencies (app, waiter, httpio, etc.) in a real application
    
    LOG_info << "ActionPacket Streaming Examples";
    
    // Example usage would look like:
    /*
    MegaApp app;
    Waiter waiter;
    HttpIO httpio;
    FileSystemAccess fs;
    DbAccess dbaccess;
    GfxProc gfx;
    
    MegaClient client(&app, &waiter, &httpio, &fs, &dbaccess, &gfx, "ExampleApp");
    
    // Choose an example based on your use case:
    
    // 1. Basic usage
    BasicStreamingExample basic;
    basic.setup(client);
    
    // 2. Custom processing
    CustomPacketProcessingExample custom;
    custom.setup(client);
    
    // 3. Mobile optimization
    MobileOptimizedExample mobile;
    mobile.setup(client);
    
    // 4. Server optimization
    ServerOptimizedExample server;
    server.setup(client);
    
    // 5. Large accounts
    LargeAccountExample large;
    large.setup(client);
    
    // 6. Debugging
    DebugExample debug;
    debug.setup(client);
    
    // 7. Complete application
    CompleteApplicationExample complete;
    complete.initializeApplication(&app, &waiter, &httpio, &fs, &dbaccess, &gfx);
    complete.login("user@example.com", "password");
    */
    
    return 0;
}
