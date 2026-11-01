#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
namespace display_config {
constexpr unsigned kWidgets=8;
inline bool validWidget(unsigned n){return n>=1&&n<=kWidgets;}
inline unsigned nextPage(unsigned current,unsigned count,uint8_t enabled){
    if(!count)return 0;
    enabled|=1;
    for(unsigned n=1;n<=count;++n){unsigned p=(current+n)%count;if(enabled&(1u<<p))return p;}
    return 0;
}
struct Snapshot {double hashrate=0,best=0;float temperature=NAN,frequency=NAN;uint32_t accepted=0,uptime=0;int fanRpm=-1;bool connected=false;};
inline const char *label(unsigned widget,bool french){
    static const char *const en[]={"HASHRATE","SHARES","TEMP","BEST DIFF","UPTIME","POOL","FREQUENCY","FAN"};
    static const char *const fr[]={"HASHRATE","SHARES","TEMP","RECORD","DUREE","POOL","FREQUENCE","VENTILO"};
    return (french?fr:en)[validWidget(widget)?widget-1:0];
}
inline void scaled(char *out,size_t size,double n,const char *suffix){
    if(!std::isfinite(n)||n<0){std::snprintf(out,size,"--");return;}
    static const char units[]=" kMGTPE";unsigned unit=0;while(n>=1000&&unit<6){n/=1000;++unit;}
    std::snprintf(out,size,"%.2f %c%s",n,units[unit],suffix);
}
inline void value(unsigned widget,const Snapshot &s,char *out,size_t size,bool french){
    switch(widget){
        case 1:scaled(out,size,s.hashrate,"H/s");break;
        case 2:std::snprintf(out,size,"%u",unsigned(s.accepted));break;
        case 3:if(std::isfinite(s.temperature))std::snprintf(out,size,"%.1f C",s.temperature);else std::snprintf(out,size,"--");break;
        case 4:scaled(out,size,s.best,"");break;
        case 5:std::snprintf(out,size,"%uh %02um",unsigned(s.uptime/3600),unsigned(s.uptime/60%60));break;
        case 6:std::snprintf(out,size,"%s",s.connected?(french?"EN LIGNE":"ONLINE"):(french?"HORS LIGNE":"OFFLINE"));break;
        case 7:if(std::isfinite(s.frequency))std::snprintf(out,size,"%.0f MHz",s.frequency);else std::snprintf(out,size,"--");break;
        case 8:if(s.fanRpm>=0)std::snprintf(out,size,"%u RPM",unsigned(s.fanRpm));else std::snprintf(out,size,"--");break;
        default:std::snprintf(out,size,"--");
    }
}
}
