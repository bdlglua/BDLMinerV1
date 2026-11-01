// Bounded SemVer ordering, including historical two-component releases.
#pragma once
#include <cstdint>
#include <cstring>
#include <cctype>

namespace release_version {
struct Version { uint32_t core[3]{}; char pre[32]{}; };
inline bool identifier(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-'; }
inline bool identifiers(const char *p, size_t length, bool leadingZero) {
    if (!length) return false;
    size_t start=0;
    for(size_t i=0;i<=length;++i) if(i==length || p[i]=='.') {
        if(i==start) return false;
        bool numeric=true;
        for(size_t j=start;j<i;++j) { if(!identifier(p[j])) return false; numeric &= p[j]>='0'&&p[j]<='9'; }
        if(!leadingZero && numeric && i-start>1 && p[start]=='0') return false;
        start=i+1;
    }
    return true;
}
inline bool parse(const char *text, Version &out) {
    if(!text || std::strlen(text)>63) return false;
    Version v; const char *p=text;
    if(*p=='v'||*p=='V') ++p;
    unsigned parts=0;
    for(;parts<3;++parts) {
        const char *start=p; uint64_t n=0;
        while(*p>='0'&&*p<='9') { n=n*10+unsigned(*p++-'0'); if(n>UINT32_MAX)return false; }
        if(p==start || (p-start>1&&*start=='0')) return false;
        v.core[parts]=uint32_t(n);
        if(*p!='.') { ++parts;break; }
        if(parts==2) return false;
        ++p;
    }
    if(parts<2 || (*p!='-'&&*p!='+'&&*p)) return false;
    if(*p=='-') {
        const char *start=++p;while(*p&&*p!='+')++p;size_t n=p-start;
        if(n>=sizeof(v.pre)||!identifiers(start,n,false))return false;
        std::memcpy(v.pre,start,n);
    }
    if(*p=='+') { const char *start=++p;while(*p)++p;if(!identifiers(start,p-start,true))return false; }
    if(*p) return false;
    out=v;
    return true;
}
inline int compare(const char *left,const char *right) {
    Version a,b;if(!parse(left,a)||!parse(right,b))return 0;
    for(unsigned i=0;i<3;++i)if(a.core[i]!=b.core[i])return a.core[i]<b.core[i]?-1:1;
    if(!*a.pre||!*b.pre)return !*a.pre?(!*b.pre?0:1):-1;
    const char *x=a.pre,*y=b.pre;
    while(*x&&*y) {
        const char *xe=x,*ye=y;bool xn=true,yn=true;
        while(*xe&&*xe!='.') { xn &= *xe>='0'&&*xe<='9';++xe; }
        while(*ye&&*ye!='.') { yn &= *ye>='0'&&*ye<='9';++ye; }
        size_t nx=xe-x,ny=ye-y;
        if(xn!=yn)return xn?-1:1;
        if(xn&&nx!=ny)return nx<ny?-1:1;
        int d=std::strncmp(x,y,nx<ny?nx:ny);
        if(d)return d<0?-1:1;
        if(nx!=ny)return nx<ny?-1:1;
        x=*xe?xe+1:xe;y=*ye?ye+1:ye;
    }
    return *x?1:*y?-1:0;
}
inline bool find(const char *text,char *out,size_t size) {
    if(!text||!size)return false;
    for(const char *p=text;*p;++p) {
        if(*p<'0'||*p>'9')continue;
        const char *end=p;while(*end&&*end!=' '&&*end!=')'&&*end!=']')++end;
        size_t n=end-p;if(n>=size)continue;
        char candidate[64]{};if(n>=sizeof(candidate))continue;
        std::memcpy(candidate,p,n);Version v;
        if(parse(candidate,v)) { std::memcpy(out,candidate,n+1);return true; }
        p=end-1;
    }
    return false;
}
} // namespace release_version
