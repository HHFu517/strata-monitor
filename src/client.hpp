#pragma once
#include "model.hpp"
#include <windows.h>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace strata {
constexpr UINT WM_METRICS = WM_APP + 1;
struct Packet {
    Metrics metrics;
    bool connected = false;
    std::wstring error = L"正在连接本机服务";
    ULONGLONG received = 0;
};
class Client {
    HWND window_;
    unsigned port_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false, hidden_ = false, refresh_ = false;
    unsigned interval_ = 1000;
    Packet packet_;
    std::thread thread_;
    void Run();
public:
    Client(HWND window, unsigned port) : window_(window), port_(port) {}
    ~Client() { Stop(); }
    void Start();
    void Stop();
    void Configure(unsigned interval, bool hidden);
    void Refresh();
    Packet Snapshot();
};
}
