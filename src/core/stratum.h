// Stratum V1 protocol state machine. Transport agnostic: feed it received
// lines, send the lines it returns. Portable (std C++ + ArduinoJson).
#pragma once

#include <stdint.h>

#include <string>

#include "work.h"

namespace stratum {

enum class Event {
    None,
    Subscribed,      // send authorize() (and suggestDifficulty()) now
    SubscribeFailed, // refused (e.g. user agent not allowed), see lastError()
    Authorized,
    AuthFailed,
    NewJob,          // job() changed; clean() tells if old work must be dropped
    Difficulty,      // difficulty() changed; applies to the next NewJob
    ShareAccepted,
    ShareRejected,
    Error,
};

struct Share {
    char jobId[64];
    char extranonce2[65];  // up to 32 bytes for SV2; V1 subscription still limited to 16
    uint32_t ntime;
    uint32_t nonce;
    double difficulty;
    uint32_t version;
    uint32_t versionMask = 0;
    uint8_t hash[32]; // exact SV2 target recheck if the pool changes target before submission
};

class Protocol {
public:
    void reset();
    void setCredentials(const std::string &user, const std::string &password);

    std::string configureVersionRolling();
    std::string subscribe(const char *agent);
    std::string authorize();
    std::string suggestDifficulty(double diff);
    std::string submit(const Share &share);
    static constexpr size_t kMaxPendingShares = 32;
    size_t pendingShares() const { return pendingCount_; }
    bool canSubmit() const { return pendingCount_ < kMaxPendingShares; }

    Event handleLine(const char *line);

    const work::Job &job() const { return job_; }
    bool hasJob() const { return hasJob_; }
    bool authorized() const { return authorized_; }
    double difficulty() const { return difficulty_; }
    const std::string &lastError() const { return lastError_; }

private:
    std::string user_, password_;
    work::Job job_;
    // Session updates apply to the next notify, not to work already issued.
    std::vector<uint8_t> extranonce1_;
    int extranonce2Size_ = 4;
    double difficulty_ = 1.0;
    bool hasJob_ = false;
    bool authorized_ = false;
    uint32_t versionMask_ = 0;
    bool configureRequested_ = false;
    uint32_t nextId_ = 10;
    uint32_t pendingIds_[kMaxPendingShares] = {};
    size_t pendingCount_ = 0;
    std::string lastError_;
};

}  // namespace stratum
