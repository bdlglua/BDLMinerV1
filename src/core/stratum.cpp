#include "stratum.h"
#include "bm1370.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace stratum {

namespace {

constexpr uint32_t kIdSubscribe = 1;
constexpr uint32_t kIdAuthorize = 2;
constexpr uint32_t kIdSuggest = 3;

std::string serialize(JsonDocument &doc) {
    std::string out;
    serializeJson(doc, out);
    out += '\n';
    return out;
}

bool parseHex32(JsonVariant value, uint32_t &out) {
    const char *s = value.as<const char *>();
    uint8_t bytes[4];
    if (!s || !work::fromHex(s, bytes, sizeof(bytes))) return false;
    out = (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
    return true;
}

// extranonce2 sizes we can serialize (Share::extranonce2 holds 16 bytes).
bool validExtranonce2Size(int n) { return n >= 1 && n <= 16; }

// ArduinoJson keeps numbers like 0.0005 as a 32-bit float, which reads back
// as 0.00049999997. Restore the decimal value the pool actually sent so that
// shares are never checked against a slightly-too-easy target.
double readDifficulty(JsonVariant v) {
    double d = v | 0.0;
    // Integer variants were never rounded to float; decimal rounding would
    // corrupt exact values such as 16777216 into 16777220.
    if (v.is<int64_t>() || v.is<uint64_t>()) return d;
    if ((double)(float)d == d) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.7g", d);
        d = strtod(buf, nullptr);
    }
    return d;
}

}  // namespace

void Protocol::reset() {
    job_ = work::Job();
    extranonce1_.clear();
    extranonce2Size_ = 4;
    difficulty_ = 1.0;
    hasJob_ = false;
    authorized_ = false;
    nextId_ = 10;
    versionMask_ = 0;
    configureRequested_ = false;
    pendingCount_ = 0;
    lastError_.clear();
}

void Protocol::setCredentials(const std::string &user, const std::string &password) {
    user_ = user;
    password_ = password;
}

std::string Protocol::configureVersionRolling() {
    configureRequested_ = true;
    JsonDocument doc;
    doc["id"] = 4; doc["method"] = "mining.configure";
    JsonArray params = doc["params"].to<JsonArray>();
    params.add<JsonArray>().add("version-rolling");
    JsonObject options = params.add<JsonObject>();
    options["version-rolling.mask"] = "1fffe000";
    options["version-rolling.min-bit-count"] = 8;
    return serialize(doc);
}

std::string Protocol::subscribe(const char *agent) {
    JsonDocument doc;
    doc["id"] = kIdSubscribe;
    doc["method"] = "mining.subscribe";
    doc["params"].add(agent);
    return serialize(doc);
}

std::string Protocol::authorize() {
    JsonDocument doc;
    doc["id"] = kIdAuthorize;
    doc["method"] = "mining.authorize";
    doc["params"].add(user_);
    doc["params"].add(password_);
    return serialize(doc);
}

std::string Protocol::suggestDifficulty(double diff) {
    JsonDocument doc;
    doc["id"] = kIdSuggest;
    doc["method"] = "mining.suggest_difficulty";
    doc["params"].add(diff);
    return serialize(doc);
}

std::string Protocol::submit(const Share &share) {
    if (!canSubmit() || share.versionMask != versionMask_) return {};
    char ntime[9], nonce[9];
    snprintf(ntime, sizeof(ntime), "%08x", (unsigned)share.ntime);
    snprintf(nonce, sizeof(nonce), "%08x", (unsigned)share.nonce);

    JsonDocument doc;
    uint32_t id;
    bool used;
    do {
        id = nextId_++;
        if (nextId_ < 10) nextId_ = 10;
        used = false;
        for (size_t i = 0; i < pendingCount_; ++i) if (pendingIds_[i] == id) used = true;
    } while (used);
    doc["id"] = id;
    doc["method"] = "mining.submit";
    JsonArray p = doc["params"].to<JsonArray>();
    p.add(user_);
    p.add(share.jobId);
    p.add(share.extranonce2);
    p.add(ntime);
    p.add(nonce);
    if (versionMask_) {
        char bits[9]; snprintf(bits, sizeof(bits), "%08x", unsigned(share.version & versionMask_));
        p.add(bits);
    }
    std::string line = serialize(doc);
    if (!doc.overflowed()) pendingIds_[pendingCount_++] = id;
    else line.clear();
    return line;
}

Event Protocol::handleLine(const char *line) {
    auto invalid = [this](const char *message) {
        lastError_ = message;
        return Event::Error;
    };
    JsonDocument doc;
    if (deserializeJson(doc, line)) {
        lastError_ = "bad json";
        return Event::Error;
    }

    const char *method = doc["method"];
    if (method) {
        JsonArray params = doc["params"];
        if (!strcmp(method, "mining.notify")) {
            if (params.size() < 9 || !params[0].is<const char *>() ||
                !params[0].as<const char *>()[0] ||
                strlen(params[0].as<const char *>()) >= sizeof(Share::jobId) ||
                !params[1].is<const char *>() || !params[2].is<const char *>() ||
                !params[3].is<const char *>() || !params[4].is<JsonArray>() || !params[8].is<bool>())
                return invalid("invalid job parameters");
            work::Job j;
            j.id = params[0].as<const char *>();

            // prevhash: eight 32-bit words, each byte-swapped vs header order.
            uint8_t prev[32];
            if (!work::fromHex(params[1], prev, 32)) return invalid("invalid previous block hash");
            for (int w = 0; w < 8; w++)
                for (int b = 0; b < 4; b++) j.prevHash[4 * w + b] = prev[4 * w + 3 - b];

            if (!work::fromHex(params[2], j.coinb1) || !work::fromHex(params[3], j.coinb2))
                return invalid("invalid coinbase hex");
            JsonArray branches = params[4].as<JsonArray>();
            if (branches.size() > 32) return invalid("too many merkle branches");
            for (JsonVariant br : branches) {
                std::vector<uint8_t> b;
                if (!br.is<const char *>() || !work::fromHex(br, b) || b.size() != 32)
                    return invalid("invalid merkle branch");
                j.branches.push_back(b);
            }
            if (!parseHex32(params[5], j.version) || !parseHex32(params[6], j.nbits) ||
                !parseHex32(params[7], j.ntime))
                return invalid("invalid block header hex");
            j.clean = params[8].as<bool>();
            j.versionMask = versionMask_;

            j.extranonce1 = extranonce1_;
            j.extranonce2Size = extranonce2Size_;
            j.difficulty = difficulty_;
            job_ = std::move(j);
            hasJob_ = true;
            return Event::NewJob;
        }
        if (!strcmp(method, "mining.set_version_mask")) {
            uint32_t mask;
            if (!configureRequested_ || !versionMask_ || !parseHex32(params[0], mask) ||
                (mask & ~bm1370::kVersionMask)) return invalid("invalid version rolling mask");
            versionMask_ = mask;
            if (hasJob_) { job_.versionMask = mask; job_.clean = true; return Event::NewJob; }
            return Event::None;
        }
        if (!strcmp(method, "mining.set_difficulty")) {
            if (!params[0].is<double>()) return invalid("invalid difficulty");
            double d = readDifficulty(params[0]);
            if (!isfinite(d) || d <= 0) return invalid("invalid difficulty");
            difficulty_ = d;
            if (!hasJob_) job_.difficulty = d;
            return Event::Difficulty;
        }
        if (!strcmp(method, "mining.set_extranonce")) {
            std::vector<uint8_t> en1;
            int size = params[1] | 0;
            if (!params[0].is<const char *>() || !params[1].is<int>() || !validExtranonce2Size(size) ||
                !work::fromHex(params[0], en1))
                return invalid("invalid extranonce parameters");
            extranonce1_ = std::move(en1);
            extranonce2Size_ = size;
            if (!hasJob_) {
                job_.extranonce1 = extranonce1_;
                job_.extranonce2Size = size;
            }
            return Event::None;
        }
        return Event::None;  // client.reconnect, client.show_message...
    }

    // Response to one of our requests.
    if (!doc["id"].is<uint32_t>()) return Event::None;
    uint32_t id = doc["id"].as<uint32_t>();
    size_t pending = pendingCount_;
    if (id >= 10) {
        for (size_t i = 0; i < pendingCount_; ++i) if (pendingIds_[i] == id) { pending = i; break; }
        if (pending == pendingCount_) return Event::None;
    }
    JsonVariant result = doc["result"];
    JsonVariant error = doc["error"];
    bool ok = !result.isNull() && !(result.is<bool>() && !result.as<bool>()) && error.isNull();
    if (!error.isNull()) {
        lastError_.clear();
        if (error.is<JsonArray>()) lastError_ = error[1] | "error";
        else serializeJson(error, lastError_);
    }

    if (id == 4) {
        if (!configureRequested_) return Event::None;
        uint32_t mask = 0;
        bool rolling = ok && result["version-rolling"].is<bool>() && result["version-rolling"].as<bool>();
        if (rolling && (!parseHex32(result["version-rolling.mask"], mask) || (mask & ~bm1370::kVersionMask)))
            return invalid("invalid configured version mask");
        versionMask_ = rolling ? mask : 0;
        if (hasJob_) { job_.versionMask = versionMask_; job_.clean = true; return Event::NewJob; }
        return Event::None;
    }
    if (id == kIdSubscribe) {
        // [[subscriptions...], extranonce1, extranonce2_size]. Some pools put
        // a refusal message in "result" instead, with a null error.
        if (!ok || !result.is<JsonArray>()) {
            if (result.is<const char *>()) lastError_ = result.as<const char *>();
            else if (lastError_.empty()) lastError_ = "subscribe refused";
            return Event::SubscribeFailed;
        }
        std::vector<uint8_t> en1;
        if (!result[0].is<JsonArray>() || !result[1].is<const char *>() || !result[2].is<int>() ||
            !work::fromHex(result[1], en1)) {
            lastError_ = "invalid subscription parameters";
            return Event::SubscribeFailed;
        }
        int size = result[2].as<int>();
        if (!validExtranonce2Size(size)) {
            lastError_ = "unsupported extranonce2 size";
            return Event::SubscribeFailed;
        }
        extranonce1_ = std::move(en1);
        extranonce2Size_ = size;
        if (!hasJob_) {
            job_.extranonce1 = extranonce1_;
            job_.extranonce2Size = size;
        }
        return Event::Subscribed;
    }
    if (id == kIdAuthorize) {
        ok = ok && result.is<bool>() && result.as<bool>();
        if (ok && authorized_) return Event::None; // duplicate replies do not restart mining work
        if (!ok && error.isNull()) lastError_ = result.is<bool>() ? "authorization refused" : "invalid authorization response";
        authorized_ = ok;
        return ok ? Event::Authorized : Event::AuthFailed;
    }
    if (id == kIdSuggest) return Event::None;
    if (id >= 10) {
        if (!result.is<bool>() && error.isNull()) return invalid("invalid share response");
        pendingIds_[pending] = pendingIds_[--pendingCount_];
        return ok ? Event::ShareAccepted : Event::ShareRejected;
    }
    return Event::None;
}

}  // namespace stratum
