#pragma once

#include <chrono>
#include <iostream>
#include <megaapi.h>
#include <thread>
#include <time.h>
#include <random>
#include <condition_variable>
#include <queue>

namespace mega
{

// UNIT TEST ONLY
// simulate network sender(just for unit test)
// signal data packet chunk
struct PacketTestChunk
{
    uint32_t packetId; // chunk ID
    uint32_t chunkIndex; // chunk index
    uint32_t totalChunks; // total number of chunks in the packet
    std::vector<uint8_t> data; // chunk data

    PacketTestChunk(uint32_t id,
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

class NetworkSender
{
public:
    NetworkSender(uint32_t chunkSize,
                  double packetLossRate,
                  MegaApi* mageApi);

    // set packet loss rate(0.0-1.0)
    void setPacketDataLossRate(double rate);

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
                // simulate network delay
                std::this_thread::sleep_for(std::chrono::milliseconds(10 + rng() % 50));

                // process chunk
                megaApi->processChunk(packetId,
                                      i,
                                      totalChunks,
                                      data.data() + offset,
                                      chunkDataSize);
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

    mega::MegaApi* megaApi; // pointer to MegaApi instance for processing chunks
};

//simulate network receive data(just for unit test)
class NetworkReceiver
{
public:
    NetworkReceiver(MegaApi* mageApi);

    ~NetworkReceiver();

    // start thread to receive data
    void start();

    // stop thread
    void stop();

    // reveive chunk£¨call by sennder£©
    void receiveChunk(const PacketTestChunk& chunk);

private:
    // thread function: process received packets
    void processPackets();

private:
    std::queue<PacketTestChunk> receiveQueue;
    mutable std::mutex queueMutex;
    std::condition_variable queueCV;
    std::thread receiverThread;
    bool running;

    MegaApi* megaApi; // pointer to MegaApi instance for processing chunks
};

// simulate packet data for unit test
class simulatePacketData {
public:
    simulatePacketData();

    void simulateDataToUnitTest(MegaApi* megaApi);
};
}