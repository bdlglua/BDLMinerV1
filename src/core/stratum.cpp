#include "stratum.h"

#include <ArduinoJson.h>
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

uint32_t parseHex32(const char *s) { return s ? (uint32_t)strtoul(s, nullptr, 16) : 0; }

// extranonce2 sizes we can serialize (Share::extranonce2 holds 16 bytes).
bool validExtranonce2Size(int n) { return n >= 1 && n <= 16; }

// ArduinoJson keeps numbers like 0.0005 as a 32-bit float, which reads back
// as 0.00049999997. Restore the decimal value the pool actually sent so that
// shares are never checked against a slightly-too-easy target.
double readDifficulty(JsonVariant v) {
    double d = v | 0.0;
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
    hasJob_ = false;
    authorized_ = false;
    nextId_ = 10;
    lastError_.clear();
}

void Protocol::setCredentials(const std::string &user, const std::string &password) {
    user_ = user;
    password_ = password;
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
    char ntime[9], nonce[9];
    snprintf(ntime, sizeof(ntime), "%08x", (unsigned)share.ntime);
    snprintf(nonce, sizeof(nonce), "%08x", (unsigned)share.nonce);

    JsonDocument doc;
    doc["id"] = nextId_++;
    doc["method"] = "mining.submit";
    JsonArray p = doc["params"].to<JsonArray>();
    p.add(user_);
    p.add(share.jobId);
    p.add(share.extranonce2);
    p.add(ntime);
    p.add(nonce);
    return serialize(doc);
}

Event Protocol::handleLine(const char *line) {
    JsonDocument doc;
    if (deserializeJson(doc, line)) {
        lastError_ = "bad json";
        return Event::Error;
    }

    const char *method = doc["method"];
    if (method) {
        JsonArray params = doc["params"];
        if (!strcmp(method, "mining.notify")) {
            if (params.size() < 9) return Event::Error;
            work::Job j;
            j.id = params[0] | "";

            // prevhash: eight 32-bit words, each byte-swapped vs header order.
            uint8_t prev[32];
            if (!work::fromHex(params[1] | "", prev, 32)) return Event::Error;
            for (int w = 0; w < 8; w++)
                for (int b = 0; b < 4; b++) j.prevHash[4 * w + b] = prev[4 * w + 3 - b];

            if (!work::fromHex(params[2] | "", j.coinb1)) return Event::Error;
            if (!work::fromHex(params[3] | "", j.coinb2)) return Event::Error;
            for (JsonVariant br : params[4].as<JsonArray>()) {
                std::vector<uint8_t> b;
                if (!work::fromHex(br | "", b) || b.size() != 32) return Event::Error;
                j.branches.push_back(b);
            }
            j.version = parseHex32(params[5]);
            j.nbits = parseHex32(params[6]);
            j.ntime = parseHex32(params[7]);
            j.clean = params[8] | false;

            j.extranonce1 = job_.extranonce1;
            j.extranonce2Size = job_.extranonce2Size;
            j.difficulty = job_.difficulty;
            job_ = std::move(j);
            hasJob_ = true;
            return Event::NewJob;
        }
        if (!strcmp(method, "mining.set_difficulty")) {
            double d = readDifficulty(params[0]);
            if (d <= 0) return Event::None;
            job_.difficulty = d;
            return Event::Difficulty;
        }
        if (!strcmp(method, "mining.set_extranonce")) {
            std::vector<uint8_t> en1;
            int size = params[1] | job_.extranonce2Size;
            if (!validExtranonce2Size(size) || !work::fromHex(params[0] | "", en1)) return Event::Error;
            job_.extranonce1 = en1;
            job_.extranonce2Size = size;
            // Before the first notify there is no job to restart yet.
            return hasJob_ ? Event::NewJob : Event::None;
        }
        return Event::None;  // client.reconnect, client.show_message...
    }

    // Response to one of our requests.
    uint32_t id = doc["id"] | 0u;
    JsonVariant result = doc["result"];
    JsonVariant error = doc["error"];
    bool ok = !result.isNull() && !(result.is<bool>() && !result.as<bool>()) && error.isNull();
    if (!error.isNull()) {
        lastError_.clear();
        if (error.is<JsonArray>()) lastError_ = error[1] | "error";
        else serializeJson(error, lastError_);
    }

    if (id == kIdSubscribe) {
        // [[subscriptions...], extranonce1, extranonce2_size]. Some pools put
        // a refusal message in "result" instead, with a null error.
        if (!ok || !result.is<JsonArray>()) {
            if (result.is<const char *>()) lastError_ = result.as<const char *>();
            else if (lastError_.empty()) lastError_ = "subscribe refused";
            return Event::SubscribeFailed;
        }
        if (!work::fromHex(result[1] | "", job_.extranonce1)) return Event::Error;
        job_.extranonce2Size = result[2] | 4;
        if (!validExtranonce2Size(job_.extranonce2Size)) {
            lastError_ = "unsupported extranonce2 size";
            return Event::SubscribeFailed;
        }
        return Event::Subscribed;
    }
    if (id == kIdAuthorize) {
        authorized_ = ok;
        return ok ? Event::Authorized : Event::AuthFailed;
    }
    if (id == kIdSuggest) return Event::None;
    if (id >= 10) return ok ? Event::ShareAccepted : Event::ShareRejected;
    return Event::None;
}

}  // namespace stratum
