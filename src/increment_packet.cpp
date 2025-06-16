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

NetworkSender::NetworkSender(uint32_t chunkSize = 1024, double packetLossRate = 0.0):
    nextPacketId(1),
    chunkSize(chunkSize),
    rng(std::random_device{}())
{
    // Set up packet loss simulation, with a range of 0 - 99. 
    // For example, a packet loss rate of 10% corresponds to 0 - 9.
    lossDistribution = std::uniform_int_distribution<>(0, 99);
    setPacketLossRate(packetLossRate);
}

void NetworkSender::setPacketLossRate(double rate)
{
    lossDistribution = std::uniform_int_distribution<>(0, static_cast<int>(100.0 * (1.0 - rate)));
}

NetworkReceiver::NetworkReceiver():
    running(false)
{
    // Set the callback for the completion of the data packet
    processor.setPacketCompleteCallback(
        [](uint32_t packetId, const std::vector<uint8_t>& data)
        {
            std::cout << "Packet " << packetId << " completed (" << data.size() << " bytes)"
                      << std::endl;

            // Example: Verify the content of the data packet
            char firstChar = (data.size() > 0) ? static_cast<char>(data[0]) : '\0';
            std::cout << "First character of packet: " << firstChar << std::endl;
        });

    // Set the data packet progress callback
    processor.setPacketProgressCallback(
        [](uint32_t packetId, double progress)
        {
            std::cout << "Packet " << packetId << " progress: " << progress << "%" << std::endl;
        });
}

NetworkReceiver::~NetworkReceiver()
{
    stop();
}

void NetworkReceiver::start()
{
    if (!running)
    {
        running = true;
        receiverThread = std::thread(&NetworkReceiver::processPackets, this);
    }
}

void NetworkReceiver::stop()
{
    if (running)
    {
        running = false;
        queueCV.notify_one();
        if (receiverThread.joinable())
        {
            receiverThread.join();
        }
    }
}

void NetworkReceiver::receiveChunk(const PacketChunk& chunk)
{
    std::lock_guard<std::mutex> lock(queueMutex); 
    receiveQueue.push(chunk);
    queueCV.notify_one();
}

void NetworkReceiver::processPackets()
{
    while (running)
    {
        std::unique_lock<std::mutex> lock(queueMutex); 
        queueCV.wait(lock,
                     [this]
                     {
                         return !receiveQueue.empty() || !running;
                     });

        if (!running)
            break;

        // Process all data chunks in the queue
        while (!receiveQueue.empty())
        {
            auto chunk = std::move(receiveQueue.front());
            receiveQueue.pop();
            lock.unlock();

            // precess chunk
            processor.processChunk(chunk);
            lock.lock();
        }
    }
}

simulatePacketData::simulatePacketData() {}

void simulatePacketData::simulateDataToUnitTest() {
    std::cout << "=== ActionPacket Incremental Processing Demo ===" << std::endl;

    // 创建网络组件
    NetworkSender sender(1024, 0.1); // 块大小1024字节，丢包率10%
    NetworkReceiver receiver;

    // 启动接收器
    receiver.start();

    // 创建测试数据
    std::vector<std::vector<uint8_t>> testPackets;

    // 测试数据包1：文本数据
    std::string textData = "Hello, this is a test packet for incremental processing demonstration!";
    testPackets.push_back(std::vector<uint8_t>(textData.begin(), textData.end()));

    // 测试数据包2：更大的二进制数据
    std::vector<uint8_t> binaryData(8192);
    for (size_t i = 0; i < binaryData.size(); ++i)
    {
        binaryData[i] = static_cast<uint8_t>(i % 256);
    }
    testPackets.push_back(binaryData);

    // 测试数据包3：中等大小的随机数据
    std::vector<uint8_t> randomData(4096);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (size_t i = 0; i < randomData.size(); ++i)
    {
        randomData[i] = static_cast<uint8_t>(dis(gen));
    }
    testPackets.push_back(randomData);

    // 发送所有测试数据包
    for (const auto& packet: testPackets)
    {
        sender.sendPacket(packet,
                          [&](const PacketChunk& chunk)
                          {
                              receiver.receiveChunk(chunk);
                          });

        // 数据包之间添加一些延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 等待所有数据包处理完成
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 停止接收器
    receiver.stop();

    std::cout << "=== Demo completed ===" << std::endl;
}
}
