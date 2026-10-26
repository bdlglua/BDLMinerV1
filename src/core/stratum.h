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
    char extranonce2[40];  // up to 16 bytes, hex
    uint32_t ntime;
    uint32_t nonce;
    double difficulty;
};

class Protocol {
public:
    void reset();
    void setCredentials(const std::string &user, const std::string &password);

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
    uint32_t nextId_ = 10;
    uint32_t pendingIds_[kMaxPendingShares] = {};
    size_t pendingCount_ = 0;
    std::string lastError_;
};

}  // namespace stratum
