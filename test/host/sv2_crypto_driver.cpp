// Native driver used by an independent Python Noise responder.
#include "core/sv2_noise.h"
#include "core/work.h"
#include <sodium/randombytes.h>
#include <iostream>
#include <cstdlib>

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    uint8_t authority[32], hello[64];
    if (!sv2::authorityKey(argv[1], authority)) { std::cout << "INVALID_KEY" << std::endl; return 0; }
    if (argc > 3) { std::cout << work::toHex(authority, 32) << std::endl; return 0; }
    sv2::Noise noise;
    if (!noise.start(authority, randombytes_buf, hello)) { std::cout << "START_ERROR " << noise.error() << std::endl; return 0; }
    std::cout << work::toHex(hello, 64) << std::endl;
    std::string line; if (!std::getline(std::cin, line)) return 2;
    uint8_t response[234]; if (!work::fromHex(line.c_str(), response, 234)) return 2;
    if (!noise.finish(response, std::strtoul(argv[2], nullptr, 10))) { std::cout << "ERROR " << noise.error() << std::endl; return 0; }
    std::cout << "READY" << std::endl;
    while (std::getline(std::cin, line)) {
        bool encrypt = line[0] == 'E';
        std::vector<uint8_t> bytes;
        if (!work::fromHex(line.c_str() + 1, bytes)) return 2;
        size_t size = bytes.size();
        if (encrypt) {
            std::vector<uint8_t> output(size + 16);
            if (!noise.encrypt(bytes.data(), size, output.data())) { std::cout << "ERROR" << std::endl; continue; }
            std::cout << work::toHex(output.data(), output.size()) << std::endl;
        } else {
            if (!noise.decrypt(bytes.data(), size, bytes.data())) { std::cout << "ERROR" << std::endl; continue; }
            std::cout << work::toHex(bytes.data(), size - 16) << std::endl;
        }
    }
}
