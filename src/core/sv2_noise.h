// Stratum V2 Noise_NX initiator. Transport independent; authority is mandatory.
#pragma once
#include <cstddef>
#include <cstdint>

namespace sv2 {
bool authorityKey(const char *text, uint8_t key[32]); // checked Base58Check or 64 hex digits

class Noise {
public:
    using Random = void (*)(void *, size_t);
    Noise() = default;
    ~Noise();
    Noise(const Noise &) = delete;
    Noise &operator=(const Noise &) = delete;
    bool start(const uint8_t authority[32], Random random, uint8_t hello[64]);
    bool finish(const uint8_t response[234], uint32_t unixTime);
    // A frame's header and each <=65519-byte payload chunk are separate records.
    bool encrypt(const uint8_t *plain, size_t length, uint8_t *cipher);
    bool decrypt(const uint8_t *cipher, size_t length, uint8_t *plain);
    const char *error() const { return error_; }
private:
    void *context_ = nullptr;
    void *arena_ = nullptr;
    uint8_t authority_[32]{}, secret_[32]{}, ephemeral_[64]{}, h_[32]{}, ck_[32]{};
    uint8_t tx_[32]{}, rx_[32]{};
    uint64_t txNonce_ = 0, rxNonce_ = 0;
    bool ready_ = false, waiting_ = false;
    const char *error_ = "Noise not established";
    bool fail(const char *message);
};
} // namespace sv2
