#include "sv2_noise.h"
#include "sha256.h"
#include "work.h"
#include <cstring>

namespace sv2 {
bool authorityKey(const char *text, uint8_t key[32]) {
    if (!text) return false;
    size_t length = strlen(text);
    if (length == 64) return work::fromHex(text, key, 32);
    if (length < 48 || length > 55 || text[0] == '1') return false;
    const char *alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    uint8_t decoded[38]{};
    for (size_t j = 0; j < length; ++j) {
        const char *digit = strchr(alphabet, text[j]); if (!digit) return false;
        unsigned carry = digit - alphabet;
        for (int i = 37; i >= 0; --i) { carry += 58u * decoded[i]; decoded[i] = carry; carry >>= 8; }
        if (carry) return false;
    }
    uint8_t hash[32]; sha::sha256d(decoded, 34, hash);
    if (decoded[0] != 1 || decoded[1] != 0 || memcmp(hash, decoded + 34, 4)) return false;
    memcpy(key, decoded + 2, 32); return true;
}

} // namespace sv2
