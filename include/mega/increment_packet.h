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

}