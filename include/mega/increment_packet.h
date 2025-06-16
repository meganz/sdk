/**
 * @file mega/increment_packet.h
 * @brief incrment action  packet precessing
 *
 * (c) 2025-2025 by Mega Limited, Wellsford, New Zealand
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
#pragma once
#include "types.h"

namespace mega
{
// signal data packet chunk
struct PacketChunk
{
    uint32_t packetId; // chunk ID
    uint32_t chunkIndex; // chunk index
    uint32_t totalChunks; // total number of chunks in the packet
    std::vector<uint8_t> data; // chunk data

    PacketChunk(uint32_t id,
                uint32_t index,
                uint32_t total,
                const uint8_t* chunkData,
                size_t dataSize):
        packetId(id),
        chunkIndex(index),
        totalChunks(total),
        data(chunkData, chunkData + dataSize)
    {}
};

// The reception status of data packets
class PacketState
{
public:
    explicit PacketState(uint32_t total):
        totalChunks(total),
        chunks(total),
        receivedFlags(total, false),
        receivedCount(0)
    {}

    // add chunk to the packet state
    bool addChunk(const PacketChunk& chunk);

    // check if the packet is complete
    bool isComplete() const;

    // reassemble the complete packet data
    std::vector<uint8_t> assemblePacket() const;

    // calcuale the complate percnetage of the packet
    double getCompletionPercentage() const;

 private:
    uint32_t totalChunks;
    std::vector<std::vector<uint8_t>> chunks;
    std::vector<bool> receivedFlags;
    size_t receivedCount;
};

// increment actionpacket precessor
class ActionPacketProcessor
{
public:
    // set a callback for when a packet is complete
    void setPacketCompleteCallback(
        std::function<void(uint32_t, const std::vector<uint8_t>&)> callback);

    // set a callback for packet progress updates
    void setPacketProgressCallback(std::function<void(uint32_t, double)> callback);

    // precess the received chunk
    void processChunk(const PacketChunk& chunk);

    // clear all pending packets
    void clearPendingPackets();

    // get the state of a specific packet
    size_t getPendingPacketCount() const;

private:
    std::unordered_map<uint32_t, std::unique_ptr<PacketState>> pendingPackets;
    mutable std::mutex mutex;
    std::function<void(uint32_t, const std::vector<uint8_t>&)> onPacketComplete;
    std::function<void(uint32_t, double)> onPacketProgress;
};


// UNIT TEST ONLY
// simulate network sender(just for unit test)
class NetworkSender
{
public:
    NetworkSender(uint32_t chunkSize, double packetLossRate);

    // set packet loss rate(0.0-1.0)
    void setPacketLossRate(double rate);

    // send data chunk£¨automatically chunk£©
    template<typename ProcessChunkFunc>
    void sendPacket(const std::vector<uint8_t>& data, ProcessChunkFunc processChunk)
    {
        uint32_t packetId = nextPacketId++;
        uint32_t totalChunks = (data.size() + chunkSize - 1) / chunkSize;

        std::cout << "Sending packet " << packetId << " in " << totalChunks << " chunks"
                  << std::endl;

        for (uint32_t i = 0; i < totalChunks; ++i)
        {
            size_t offset = i * chunkSize;
            size_t chunkDataSize = std::min(chunkSize, static_cast<uint32_t>(data.size() - offset));

            // simulate packet losss
            if (lossDistribution(rng) < 100)
            {
                PacketChunk chunk(packetId, i, totalChunks, data.data() + offset, chunkDataSize);

                // simulate network delay
                std::this_thread::sleep_for(std::chrono::milliseconds(10 + rng() % 50));

                // process chunk
                processChunk(chunk);
            }
            else
            {
                std::cout << "Simulated loss of chunk " << i << " from packet " << packetId
                          << std::endl;
            }
        }
    }

private:
    uint32_t nextPacketId;
    uint32_t chunkSize;
    std::mt19937 rng;
    std::uniform_int_distribution<> lossDistribution;
};

//simulate network receive data(just for unit test)
class NetworkReceiver
{
public:
    NetworkReceiver();

    ~NetworkReceiver();

    // start thread to receive data
    void start();

    // stop thread
    void stop();

    // reveive chunk£¨call by sennder£©
    void receiveChunk(const PacketChunk& chunk);

private:
    // thread function: process received packets
    void processPackets();

private:
    ActionPacketProcessor processor;
    std::queue<PacketChunk> receiveQueue;
    mutable std::mutex queueMutex;
    std::condition_variable queueCV;
    std::thread receiverThread;
    bool running;
};

// simulate packet data for unit test
class simulatePacketData {
public:
    simulatePacketData();

    void simulateDataToUnitTest();
};
}