#!/usr/bin/env python3
"""Exercise production HTML delivery: exact bytes/length, four payload writes."""
from pathlib import Path
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/web.cpp').read_text()
delivery = source[source.index('void sendPage('):source.index('void sendAsset(')]
prefix = r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
struct String : std::string {
    using std::string::string;
    using std::string::operator=;
    String(const std::string &s) : std::string(s) {}
};
#define strlen_P std::strlen
constexpr size_t CONTENT_LENGTH_UNKNOWN = SIZE_MAX;
struct { int language=0; } settings;
struct {
    size_t length=0;int writes=0;
    std::string body,cache,type;
    void sendHeader(const char *key,const char *value) {
        if(!std::strcmp(key,"Cache-Control"))cache=value;
    }
    void setContentLength(size_t n) {length=n;}
    void send(int code,const char *mime,const String &text) {
        assert(code==200);type=mime;body=text;writes=1;
    }
    void sendContent(const char *text) {body+=text;++writes;}
    void sendContent_P(const char *text) {body+=text;++writes;}
} server;
const char kWebVersion[]="cache-version";
'''
header = (root / 'web/header.html').read_text()
prefix += 'const char kHeader[]=' + json.dumps(header, ensure_ascii=False) + ';\n'
for page in ('dashboard', 'settings', 'log'):
    prefix += 'const char page_' + page + '[]=' + json.dumps((root / 'web' / (page + '.html')).read_text(), ensure_ascii=False) + ';\n'
tests = r'''
int main() {
    const char *pages[]={page_dashboard,page_settings,page_log};
    const char *scripts[]={"dashboard","settings","log"};
    for(int language=0;language<=1;++language)for(int i=0;i<3;++i){
        settings.language=language;
        sendPage("BDLMiner",pages[i],scripts[i]);
        assert(server.length==server.body.size()&&server.writes==4);
        assert(server.cache=="no-store"&&server.type=="text/html; charset=utf-8");
        auto lang=language?"fr":"en";
        assert(server.body.find(std::string("<html lang=\"")+lang+"\">")!=std::string::npos);
        assert(server.body.find(std::string("const LANG='")+lang+"'")!=std::string::npos);
        assert(server.body.find(std::string("/assets/")+scripts[i]+".js?v=cache-version")!=std::string::npos);
        const std::string ending="</div></div></body></html>";
        assert(server.body.substr(server.body.size()-ending.size())==ending);
        auto bodyAt=server.body.find("</head><body>")+std::strlen("</head><body>");
        assert(server.body.substr(bodyAt)==std::string(kHeader)+pages[i]+ending);
    }
    puts("Production HTTP pages: exact Content-Length, four payload writes, intact HTML/assets, English/French passed");
}
'''
with tempfile.TemporaryDirectory(prefix='bdl-web-delivery-') as temp:
    cpp=Path(temp)/'test.cpp'
    exe=Path(temp)/'test'
    cpp.write_text(prefix+delivery+tests)
    subprocess.run(['g++','-std=c++17','-O2',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
