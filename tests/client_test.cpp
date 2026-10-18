#include "../src/client.hpp"
#include <iostream>
#include <chrono>
std::string Utf8(const std::wstring& w){
    int n=WideCharToMultiByte(CP_UTF8,0,w.data(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr);
    std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w.data(),static_cast<int>(w.size()),s.data(),n,nullptr,nullptr);return s;
}
int main(int argc,char** argv){
    using namespace strata;
    if(argc<4)return 2;
    unsigned port=static_cast<unsigned>(std::stoul(argv[1]));double seconds=std::stod(argv[2]);std::string expected=argv[3];
    DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
    Client c(nullptr,port);c.Configure(1000,argc>4 && std::string(argv[4])=="hidden");c.Start();
    auto start=std::chrono::steady_clock::now();
    while(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<seconds)Sleep(50);
    // The application owns one session; compare repeated requests after first-use initialization.
    DWORD warm=0;GetProcessHandleCount(GetCurrentProcess(),&warm);
    if(expected=="reuse")for(int i=0;i<20;++i){c.Refresh();Sleep(120);}
    auto stop=std::chrono::steady_clock::now();c.Stop();
    double stopMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-stop).count();
    auto p=c.Snapshot();GetProcessHandleCount(GetCurrentProcess(),&after);
    bool pass=(expected=="ok" || expected=="reuse")?p.connected:!p.connected && !p.metrics.gpu && !p.metrics.decode && p.metrics.model.empty();
    if(expected=="auth")pass=pass && p.error.find(L"授权")!=std::wstring::npos;
    if(expected=="format")pass=pass && p.error.find(L"格式")!=std::wstring::npos;
    if(expected=="large")pass=pass && p.error.find(L"过大")!=std::wstring::npos;
    if(expected=="http")pass=pass && p.error.find(L"HTTP")!=std::wstring::npos;
    if(expected!="ok" && expected!="reuse")pass=pass && p.error!=L"正在连接本机服务";
    if(expected=="reuse")pass=pass && after<=warm+4;
    pass=pass && stopMs<4000;
    Json out={{"pass",pass},{"connected",p.connected},{"error",Utf8(p.error)},{"stop_ms",stopMs},{"handles_before",before},{"handles_warm",warm},{"handles_after",after}};
    std::cout<<out.dump()<<"\n";return pass?0:1;
}
