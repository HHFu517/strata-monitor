#pragma once
#include <windows.h>
#include <filesystem>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>
namespace strata {
constexpr UINT WM_SERVICE=WM_APP+4, WM_SERVICE_BROWSER=WM_APP+5;
enum class ServicePhase { Off, Starting, Active, Stopping, Failed };
struct ServiceInfo {
    ServicePhase phase=ServicePhase::Off;
    bool owned=false,foreign=false;
    int preset=0;
    DWORD pid=0,exitCode=0;
    ULONGLONG stopAt=0,exitAt=0;
    unsigned browserRequests=0;
    std::wstring error,log;
};
struct PortInfo {bool ok=false;std::vector<DWORD> pids;bool Busy()const{return !pids.empty();}};
struct ServiceSpec {
    std::filesystem::path root,python,host,stateDir,logDir;
    std::wstring mutex=L"Local\\StrataGDrive200K";
    unsigned port=517;
    static ServiceSpec ForRoot(const std::filesystem::path& root,unsigned port);
    std::filesystem::path Record()const;
    std::filesystem::path Config(int preset)const;
};
std::wstring QuoteArgument(const std::wstring& value);
PortInfo ListeningPort(unsigned port);
std::wstring PresetSummary(const ServiceSpec& spec,int preset);
struct ExternalStopPlan;
class Service {
public:
    Service(HWND notify,ServiceSpec spec);
    ~Service();
    Service(const Service&)=delete;Service& operator=(const Service&)=delete;
    bool Recover();
    bool Start(int preset);
    bool Stop();
    bool ForceStop();
    // A service started outside this launcher: the confirmation names every process that will end.
    bool StopExternal();
    bool PrepareExternalStop(std::wstring& summary);
    bool OwnsListener()const;
    ServiceInfo Snapshot()const;
    void BeginDetach(); // Non-blocking: invalidate GUI notifications and wake the worker.
    void Detach(); // Never initiates a Stop. An already confirmed Stop finishes before destruction.
private:
    void Spawn(int preset);
    void Watch();
    void StopForeign(std::shared_ptr<const ExternalStopPlan> confirmed);
    void WaitQuiet(const std::vector<DWORD>& targets,int seconds);
    void Notify(UINT message=WM_SERVICE);
    void Failure(const std::wstring& error);
    void CloseManaged();
    void RemoveRecord();
    HWND notify_=nullptr;
    ServiceSpec spec_;
    mutable std::mutex mutex_;
    ServiceInfo info_;
    std::shared_ptr<const ExternalStopPlan> externalPlan_;
    std::thread worker_;
    HANDLE detach_=nullptr,process_=nullptr,job_=nullptr,stop_=nullptr,ready_=nullptr,browser_=nullptr;
    std::wstring run_;
    bool requestedStop_=false;
};
}
