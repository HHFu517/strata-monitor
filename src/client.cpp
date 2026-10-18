#include "client.hpp"
#include <winhttp.h>
#include <algorithm>
#include <utility>
#pragma comment(lib,"winhttp.lib")
namespace strata {
namespace {
struct Handle {
    HINTERNET value = nullptr;
    explicit Handle(HINTERNET h) : value(h) {}
    ~Handle() { if (value) WinHttpCloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
Packet Fetch(HINTERNET session, HINTERNET connection) {
    Packet p;
    p.error = L"未连接 · 等待 Strata 启动";
    if (!session || !connection) return p;
    Handle request(WinHttpOpenRequest(connection,L"GET",L"/metrics",nullptr,
        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0));
    if (!request.value) return p;
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled));
    const ULONGLONG begin = GetTickCount64();
    if (!WinHttpSendRequest(request.value,L"Accept: application/json\r\n",DWORD(-1),
            WINHTTP_NO_REQUEST_DATA,0,0,0) || !WinHttpReceiveResponse(request.value,nullptr)) return p;
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX)) return p;
    if (status != 200) {
        p.error = status == 401 || status == 403 ? L"接口需要授权 · 本版仅支持无密钥本机监控" : L"接口返回 HTTP " + std::to_wstring(status);
        return p;
    }
    std::string body;
    char buffer[8192];
    for (;;) {
        if (GetTickCount64()-begin > 2500) { p.error=L"接口超时 · 下次自动重试"; return p; }
        DWORD count = 0;
        if (!WinHttpReadData(request.value,buffer,sizeof(buffer),&count)) return p;
        if (!count) break;
        if (body.size()+count > 256*1024) { p.error=L"接口响应过大"; return p; }
        body.append(buffer,count);
    }
    try {
        p.metrics = Parse(body);
        p.connected = true;
        p.error.clear();
        p.received = GetTickCount64();
    } catch (...) { p.error = L"数据格式异常 · 需要 Strata /metrics 接口"; }
    return p;
}
}
void Client::Start() { thread_ = std::thread(&Client::Run,this); }
void Client::Stop() {
    { std::lock_guard<std::mutex> guard(mutex_); stop_=true; }
    wake_.notify_one();
    if (thread_.joinable()) thread_.join();
}
void Client::Configure(unsigned interval, bool hidden) {
    { std::lock_guard<std::mutex> guard(mutex_); interval_=interval; hidden_=hidden; refresh_=true; }
    wake_.notify_one();
}
void Client::Refresh() {
    { std::lock_guard<std::mutex> guard(mutex_); refresh_=true; }
    wake_.notify_one();
}
Packet Client::Snapshot() { std::lock_guard<std::mutex> guard(mutex_); return packet_; }
void Client::Run() {
    Handle session(WinHttpOpen(L"StrataMonitor/0.3",WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if (session.value) WinHttpSetTimeouts(session.value,500,600,600,800);
    Handle connection(session.value ? WinHttpConnect(session.value,L"127.0.0.1",
        static_cast<INTERNET_PORT>(port_),0) : nullptr);
    for (;;) {
        { std::lock_guard<std::mutex> guard(mutex_); if (stop_) return; refresh_=false; }
        Packet next=Fetch(session.value,connection.value);
        { std::lock_guard<std::mutex> guard(mutex_); if(stop_) return; packet_=std::move(next); }
        if(window_)PostMessageW(window_,WM_METRICS,0,0);
        std::unique_lock<std::mutex> guard(mutex_);
        unsigned delay = hidden_ ? (std::max)(interval_,5000u) : interval_;
        wake_.wait_for(guard,std::chrono::milliseconds(delay),[this]{ return stop_ || refresh_; });
        if(stop_) return;
    }
}
}
