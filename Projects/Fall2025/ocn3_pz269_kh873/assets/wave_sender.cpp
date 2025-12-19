#include <arpa/inet.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

constexpr int PACKET_SIZE = 1024;
constexpr int PORT = 1234;  // target port
constexpr double SAMPLE_RATE = 44100;

constexpr const char* FILENAME = "output.wav";

// modify these IP below
constexpr const char* DEST_IP = "172.20.10.8";

// constexpr const char *IP_BROADCASTING = "172.20.10.255";

int main(int argc, char* argv[]) {
    // if (argc < 3) {
    //     std::cerr << "Usage" << argv[0] << " <wav file> <target IP>\n";
    //     return 1;
    // }
    // const char *filename = argv[1];
    // const char *dest_ip = argv[2];

    std::ifstream wav(FILENAME, std::ios::binary);
    if (!wav.is_open()) {
        std::cerr << "File not found " << FILENAME << "\n";
        return 1;
    }

    // GPT, remove header
    char riff[4], wave[4];
    uint32_t riff_size;
    wav.read(riff, 4);
    wav.read(reinterpret_cast<char*>(&riff_size), 4);
    wav.read(wave, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0 ||
        std::strncmp(wave, "WAVE", 4) != 0) {
        std::cerr << "Not a valid WAV file\n";
        return 1;
    }

    char tag[4];
    uint32_t chunk_size;
    bool found = false;

    while (wav.read(tag, 4)) {
        wav.read(reinterpret_cast<char*>(&chunk_size), 4);
        if (wav.gcount() < 4) break;

        std::cout << "Chunk: " << std::string(tag, 4) << " (" << chunk_size
                  << " bytes)\n";

        if (std::strncmp(tag, "data", 4) == 0) {
            found = true;
            break;
        }

        wav.seekg(chunk_size + (chunk_size % 2), std::ios::cur);
        wav.clear();
    }

    if (!found) {
        std::cerr << "Data block not found\n";
        return 1;
    }

    std::cout << "Found Data block\n";

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    int broadcastEnable = 1;
    // setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable,
    // sizeof(broadcastEnable));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, DEST_IP, &addr.sin_addr);

    constexpr double bytes_per_second = SAMPLE_RATE * 2 * 2;  // 16 bit stereo
    constexpr double packet_time_sec = PACKET_SIZE / bytes_per_second;

    std::vector<char> buffer(PACKET_SIZE);
    int seq = 0;

    auto next_time = std::chrono::steady_clock::now();

    while (wav.read(buffer.data(), buffer.size()) || wav.gcount() > 0) {
        size_t bytes = wav.gcount();
        assert(bytes % 4 == 0);
        std::vector<int16_t> packet((bytes + 1) / 2);
        std::memcpy(packet.data(), buffer.data(), bytes);
        std::for_each(packet.begin(), packet.end(), [](auto& x) {
            // x >>= 4;
            x = (static_cast<int32_t>(x) + 32768) >> 4;
        });
        sendto(sock, packet.data(), packet.size() * sizeof(int16_t), 0,
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

        next_time +=
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(packet_time_sec * .98));
        std::this_thread::sleep_until(next_time);
    }

    std::cout << "Sent\n";
    close(sock);
    wav.close();
    return 0;
}
