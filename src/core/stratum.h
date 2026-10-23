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
    Authorized,
    AuthFailed,
    NewJob,          // job() changed; clean() tells if old work must be dropped
    Difficulty,      // job().difficulty changed
    ShareAccepted,
    ShareRejected,
    Error,
};

struct Share {
    char jobId[64];
    char extranonce2[20];
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

    Event handleLine(const char *line);

    const work::Job &job() const { return job_; }
    bool hasJob() const { return hasJob_; }
    bool authorized() const { return authorized_; }
    const std::string &lastError() const { return lastError_; }

private:
    std::string user_, password_;
    work::Job job_;
    bool hasJob_ = false;
    bool authorized_ = false;
    uint32_t nextId_ = 10;
    std::string lastError_;
};

}  // namespace stratum
