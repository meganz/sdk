#include "increment_packet_unit_test.h"
#include <functional>

namespace mega
{
NetworkSender::NetworkSender(uint32_t chunkSize = 1024, double packetLossRate = 0.0, MegaApi* mAp = nullptr):
    nextPacketId(1),
    chunkSize(chunkSize),
    rng(std::random_device{}()),
    lossDistribution(0),
    megaApi(mAp)
{
    // Set up packet loss simulation, with a range of 0 - 99. 
    // For example, a packet loss rate of 10% corresponds to 0 - 9.
    lossDistribution = std::uniform_int_distribution<>(0, 99);
    setPacketDataLossRate(packetLossRate);
}

void NetworkSender::setPacketDataLossRate(double rate)
{
    lossDistribution = std::uniform_int_distribution<>(0, static_cast<int>(100.0 * (1.0 - rate)));
}

NetworkReceiver::NetworkReceiver(MegaApi* mApi):
    running(false),
    megaApi(mApi)
{
    megaApi->setPacketCompleteCallback(std::function<void(uint32_t, const std::vector<uint8_t>&)>(
        [](uint32_t packetId, const std::vector<uint8_t>& data)
        {
            std::cout << "Packet " << packetId << " completed (" << data.size() << " bytes)"
                      << std::endl;
            char firstChar = (data.size() > 0) ? static_cast<char>(data[0]) : '\0';
            std::cout << "First character of packet: " << firstChar << std::endl;
        }));

    // Set the data packet progress callback
    megaApi->setPacketProgressCallback(
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

void NetworkReceiver::receiveChunk(const PacketTestChunk& chunk)
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
            megaApi->processChunk(chunk.packetId,
                                  chunk.chunkIndex,
                                  chunk.totalChunks,
                                  chunk.data.data(),
                                  chunk.data.size());
            lock.lock();
        }
    }
}

simulatePacketData::simulatePacketData() {}

void simulatePacketData::simulateDataToUnitTest(MegaApi* megaApi)
{
    std::cout << "=== ActionPacket Incremental Processing Demo ===" << std::endl;

    // create sender and receiver instances
    NetworkSender sender(1024, 0.1, megaApi); // chunk size of 1024 bytes, 10% packet loss
    NetworkReceiver receiver(megaApi);

    // start the receiver to process incoming packets
    receiver.start();

    // create a vector to hold test packets
    std::vector<std::vector<uint8_t>> testPackets;

    // test data packets1: small text data
    std::string textData = "Hello, this is a test packet for incremental processing demonstration!";
    testPackets.push_back(std::vector<uint8_t>(textData.begin(), textData.end()));

    // test packets2: large binary data
    std::vector<uint8_t> binaryData(8192);
    for (size_t i = 0; i < binaryData.size(); ++i)
    {
        binaryData[i] = static_cast<uint8_t>(i % 256);
    }
    testPackets.push_back(binaryData);

    // test packets3: random data
    std::vector<uint8_t> randomData(4096);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (size_t i = 0; i < randomData.size(); ++i)
    {
        randomData[i] = static_cast<uint8_t>(dis(gen));
    }
    testPackets.push_back(randomData);

    // send each packet using the sender
    for (const auto& packet: testPackets)
    {
        sender.sendPacket(packet,
                          [&](const PacketTestChunk& chunk)
                          {
                              receiver.receiveChunk(chunk);
                          });

        // data loss simulation
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // wait for all packets to be processed
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // stop the receiver
    receiver.stop();

    std::cout << "=== Demo completed ===" << std::endl;
}
}
