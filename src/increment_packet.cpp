#include "mega/increment_packet.h"

namespace mega
{
bool PacketState::addChunk(const PacketChunk& chunk)
{
    if (chunk.chunkIndex >= totalChunks || receivedFlags[chunk.chunkIndex])
    {
        return false;
    }

    chunks[chunk.chunkIndex] = std::move(chunk.data);
    receivedFlags[chunk.chunkIndex] = true;
    receivedCount++;
    return true;
}

bool PacketState::isComplete() const
{
    return receivedCount == totalChunks;
}

std::vector<uint8_t> PacketState::assemblePacket() const
{
    size_t totalSize = 0;
    for (const auto& chunk: chunks)
    {
        totalSize += chunk.size();
    }

    std::vector<uint8_t> result(totalSize);
    size_t offset = 0;
    for (const auto& chunk: chunks)
    {
        std::copy(chunk.begin(), chunk.end(), result.begin() + offset);
        offset += chunk.size();
    }

    return result;
}

double PacketState::getCompletionPercentage() const
{
    return (static_cast<double>(receivedCount) / totalChunks) * 100.0;
}

void ActionPacketProcessor::setPacketCompleteCallback(
    std::function<void(uint32_t, const std::vector<uint8_t>&)> callback)
{
    onPacketComplete = callback;
}

void ActionPacketProcessor::setPacketProgressCallback(
    std::function<void(uint32_t, double)> callback)
{
    onPacketProgress = callback;
}

void ActionPacketProcessor::processChunk(const PacketChunk& chunk)
{
    std::lock_guard<std::mutex> lock(mutex); 

    // find or create packet
    auto it = pendingPackets.find(chunk.packetId);
    if (it == pendingPackets.end())
    {
        auto state = std::make_unique<PacketState>(chunk.totalChunks);
        it = pendingPackets.emplace(chunk.packetId, std::move(state)).first;
    }

    // increment or add chunk to the packet
    if (it->second->addChunk(chunk))
    {
        // nofity progress
        if (onPacketProgress)
        {
            onPacketProgress(chunk.packetId, it->second->getCompletionPercentage());
        }

        // check if packet is complete
        if (it->second->isComplete())
        {
            auto packetData = it->second->assemblePacket();

            // if complete, call the complete callback
            if (onPacketComplete)
            {
                onPacketComplete(chunk.packetId, packetData);
            }

            // remove the packet from pending packets
            pendingPackets.erase(it);
        }
    }
}

void ActionPacketProcessor::clearPendingPackets()
{
    std::lock_guard<std::mutex> lock(mutex);
    pendingPackets.clear();
}

size_t ActionPacketProcessor::getPendingPacketCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return pendingPackets.size();
}
}
