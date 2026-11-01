#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace pool_diagnostics {
enum class Issue { Ready, Disconnected, WaitingWork, RollingRequired, HighDifficulty, InvalidDifficulty };
struct Input {
    bool connected=false,work=false;uint32_t mask=0;unsigned requiredBits=0;
    double difficulty=0,hashrate=0,acceptedWork=0,seconds=0;uint32_t accepted=0;
    const char *status="";
};
struct Result {
    Issue issue=Issue::Disconnected;unsigned rollingBits=0;bool compatible=false;
    double expectedShareSeconds=NAN,shareBasedHashrate=NAN;
};
inline const char *code(Issue issue) {
    switch(issue) {
        case Issue::Ready:return "ready";case Issue::WaitingWork:return "waiting_work";
        case Issue::RollingRequired:return "version_rolling_required";
        case Issue::HighDifficulty:return "infrequent_shares";
        case Issue::InvalidDifficulty:return "invalid_difficulty";
        default:return "disconnected";
    }
}
inline Result diagnose(const Input &in) {
    Result r;for(uint32_t n=in.mask;n;n>>=1)r.rollingBits+=n&1;
    if(std::isfinite(in.difficulty)&&in.difficulty>0&&std::isfinite(in.hashrate)&&in.hashrate>0) {
        double value=in.difficulty*4294967296.0/in.hashrate;
        if(std::isfinite(value))r.expectedShareSeconds=value;
    }
    if(in.accepted>=3&&in.seconds>=60&&std::isfinite(in.acceptedWork)&&in.acceptedWork>0)
        r.shareBasedHashrate=in.acceptedWork*4294967296.0/in.seconds;
    if(in.requiredBits&&((in.work&&r.rollingBits<in.requiredBits)||
       (!in.work&&in.status&&std::strstr(in.status,"version rolling"))))r.issue=Issue::RollingRequired;
    else if(!in.connected)r.issue=Issue::Disconnected;
    else if(!in.work)r.issue=Issue::WaitingWork;
    else if(!std::isfinite(in.difficulty)||in.difficulty<=0)r.issue=Issue::InvalidDifficulty;
    else { r.compatible=true;r.issue=r.expectedShareSeconds>3600?Issue::HighDifficulty:Issue::Ready; }
    return r;
}
}
