#pragma once
#include <cstdint>
#include <cctype>
#include <cstring>

namespace stratum {
enum class RejectReason : uint8_t { Unknown, Stale, Duplicate, LowDifficulty, Authorization, Invalid, Count };
inline const char *reasonName(RejectReason r) {
    static const char *const names[]={"unknown","stale","duplicate","low_difficulty","authorization","invalid"};
    return names[unsigned(r)<unsigned(RejectReason::Count)?unsigned(r):0];
}
inline RejectReason rejectReason(int code,const char *message) {
    if(code==21)return RejectReason::Stale;
    if(code==22)return RejectReason::Duplicate;
    if(code==23)return RejectReason::LowDifficulty;
    if(code==24||code==25)return RejectReason::Authorization;
    char text[96]{};
    if(message)for(size_t i=0;i<sizeof(text)-1&&message[i];++i)text[i]=char(std::tolower(static_cast<unsigned char>(message[i])));
    if(std::strstr(text,"stale")||std::strstr(text,"job-not-found")||std::strstr(text,"invalid-job-id"))return RejectReason::Stale;
    if(std::strstr(text,"duplicate"))return RejectReason::Duplicate;
    if(std::strstr(text,"difficulty-too-low")||std::strstr(text,"low difficulty")||std::strstr(text,"low-difficulty"))return RejectReason::LowDifficulty;
    if(std::strstr(text,"unauthor")||std::strstr(text,"not-subscribed")||std::strstr(text,"invalid-channel"))return RejectReason::Authorization;
    if(std::strstr(text,"invalid")||std::strstr(text,"bad-"))return RejectReason::Invalid;
    return RejectReason::Unknown;
}
}
