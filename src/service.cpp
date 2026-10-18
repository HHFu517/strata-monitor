#include <winsock2.h>
#include <ws2tcpip.h>
#include "service.hpp"
#include "../vendor/json.hpp"
#include <iphlpapi.h>
#include <objbase.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <shellapi.h>
#pragma comment(lib,"shell32.lib")
#include <algorithm>
#include <fstream>
#include <iterator>
#include <cwctype>
#include <regex>
#pragma comment(lib,"iphlpapi.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"winhttp.lib")
namespace strata {
struct ProcRow {
    DWORD pid=0,parent=0;
    std::wstring name,image;
    ULONGLONG created=0;
};
struct ExternalStopPlan {
    bool portOk=false;
    std::vector<DWORD> listeners,engines,children,shims,blocked;
    std::vector<ProcRow> rows;
    bool Empty()const{return listeners.empty() && engines.empty() && children.empty() && shims.empty();}
    std::vector<DWORD> All()const {
        std::vector<DWORD> out;
        for(const std::vector<DWORD>* v:{&listeners,&engines,&children,&shims})for(DWORD pid:*v)if(std::find(out.begin(),out.end(),pid)==out.end())out.push_back(pid);
        return out;
    }
    // End the leftovers and the service first: a live server would otherwise start another engine.
    std::vector<DWORD> Ordered()const {
        std::vector<DWORD> out;
        auto add=[&](std::vector<DWORD> values){std::reverse(values.begin(),values.end());for(DWORD pid:values)if(std::find(out.begin(),out.end(),pid)==out.end())out.push_back(pid);};
        add(children);add(listeners);add(shims);add(engines);return out;
    }
};
namespace {
using Json=nlohmann::json;using Path=std::filesystem::path;
struct Handle {
    HANDLE h=nullptr;Handle()=default;explicit Handle(HANDLE value):h(value==INVALID_HANDLE_VALUE?nullptr:value){}
    ~Handle(){if(h)CloseHandle(h);}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    HANDLE Release(){auto result=h;h=nullptr;return result;}
};
std::wstring Widen(const std::string& s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n && !s.empty())throw std::runtime_error("invalid UTF-8");std::wstring out(n,0);
    if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);return out;
}
std::string Utf8(const std::wstring& s) {
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    if(!n && !s.empty())throw std::runtime_error("invalid path");std::string out(n,0);
    if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;
}
std::wstring Normal(const Path& p) {
    auto s=std::filesystem::absolute(p).lexically_normal().native();
    if(s.rfind(L"\\\\?\\",0)==0)s.erase(0,4);
    std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return s;
}
ULONGLONG Created(HANDLE process) {
    FILETIME a{},b{},c{},d{};if(!GetProcessTimes(process,&a,&b,&c,&d))return 0;
    return (static_cast<ULONGLONG>(a.dwHighDateTime)<<32)|a.dwLowDateTime;
}
Path Image(HANDLE process) {
    std::wstring s(32768,0);DWORD n=static_cast<DWORD>(s.size());
    if(!QueryFullProcessImageNameW(process,0,s.data(),&n))return {};s.resize(n);return Path(s);
}
bool Alive(HANDLE process){return process && WaitForSingleObject(process,0)==WAIT_TIMEOUT;}
bool Member(HANDLE process,HANDLE job){BOOL yes=FALSE;return IsProcessInJob(process,job,&yes) && yes;}
Json ReadJson(const Path& p,size_t max=65536) {
    std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot read JSON file");
    f.seekg(0,std::ios::end);auto size=f.tellg();if(size<0 || static_cast<size_t>(size)>max)throw std::runtime_error("JSON size");
    f.seekg(0);return Json::parse(std::string(std::istreambuf_iterator<char>(f),{}));
}
void WriteJson(const Path& path,const Json& value,const std::wstring& run) {
    auto temp=Path(path.native()+L".tmp-"+run);const auto text=value.dump(2);
    Handle file(CreateFileW(temp.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!file.h)throw std::runtime_error("cannot create service record");DWORD written=0;
    bool ok=WriteFile(file.h,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) && written==text.size() && FlushFileBuffers(file.h);
    CloseHandle(file.Release());
    if(!ok || !MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());throw std::runtime_error("cannot commit service record");
    }
}
std::wstring Identifier() {
    GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("GUID failed");wchar_t text[40];
    swprintf_s(text,L"%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",id.Data1,id.Data2,id.Data3,id.Data4[0],id.Data4[1],id.Data4[2],id.Data4[3],id.Data4[4],id.Data4[5],id.Data4[6],id.Data4[7]);return text;
}
bool ValidRun(const std::wstring& s){return s.size()==32 && std::all_of(s.begin(),s.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');});}
std::wstring Name(const wchar_t* kind,const std::wstring& run){return std::wstring(L"Local\\StrataMonitor.")+kind+L"."+run;}
std::wstring SystemError(const wchar_t* action){return std::wstring(action)+L"失败（Windows 错误 "+std::to_wstring(GetLastError())+L"）";}
void RetainLogs(const Path& directory) {
    try {
        const std::wregex pattern(L"startup-[0-9]{8}-[0-9]{6}-[0-9a-f]{32}\\.log");
        std::vector<std::filesystem::directory_entry> logs;
        for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.is_regular_file() && std::regex_match(entry.path().filename().native(),pattern))logs.push_back(entry);
        std::sort(logs.begin(),logs.end(),[](const auto& a,const auto& b){return a.last_write_time()>b.last_write_time();});
        for(size_t i=5;i<logs.size();++i){std::error_code ignored;std::filesystem::remove(logs[i].path(),ignored);std::filesystem::remove(Path(logs[i].path().native()+L".1"),ignored);}
    }catch(...){} // Log retention must not take inference down.
}
std::wstring Lower(const std::wstring& value) {
    std::wstring out=value;std::transform(out.begin(),out.end(),out.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return out;
}
std::vector<ProcRow> ProcessRows() {
    std::vector<ProcRow> out;
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    if(!snapshot.h)return out;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Process32FirstW(snapshot.h,&entry))do{out.push_back(ProcRow{entry.th32ProcessID,entry.th32ParentProcessID,entry.szExeFile,{}});}while(Process32NextW(snapshot.h,&entry));
    for(auto& row:out) {
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,row.pid));
        if(!process.h)continue;
        std::wstring path(32768,0);DWORD size=static_cast<DWORD>(path.size());
        if(QueryFullProcessImageNameW(process.h,0,path.data(),&size))row.image=path.substr(0,size);
        row.created=Created(process.h);
    }
    return out;
}
bool SameImage(const std::wstring& image,const Path& wanted){return !image.empty() && Normal(Path(image))==Normal(wanted);}
bool Under(const std::wstring& image,const Path& directory) {
    if(image.empty())return false;
    auto path=Normal(Path(image)),root=Normal(directory);
    return path.size()>root.size()+1 && path.rfind(root,0)==0 && path[root.size()]==L'\\';
}
bool PythonLike(const ProcRow& row){auto name=Lower(row.name);return name==L"python.exe" || name==L"pythonw.exe";}
// Read-only native query; unavailable/denied command lines fail closed.
std::wstring CommandLine(HANDLE process) {
    struct NativeString {USHORT length,maximum;PWSTR buffer;};
    using Query=LONG (WINAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);
    static auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationProcess"));
    if(!query)return {};
    ULONG size=0;query(process,60,nullptr,0,&size);
    if(size<sizeof(NativeString) || size>131072)return {};
    std::vector<BYTE> bytes(size);
    if(query(process,60,bytes.data(),size,&size)<0)return {};
    auto* value=reinterpret_cast<const NativeString*>(bytes.data());
    auto begin=reinterpret_cast<ULONG_PTR>(bytes.data()),end=begin+bytes.size(),text=reinterpret_cast<ULONG_PTR>(value->buffer);
    if(value->length%sizeof(wchar_t) || text<begin || text>end || value->length>end-text)return {};
    return std::wstring(value->buffer,value->length/sizeof(wchar_t));
}
bool ServerCommand(const ProcRow& row,const ServiceSpec& spec) {
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,row.pid));
    if(!process.h || !row.created || Created(process.h)!=row.created || Normal(Image(process.h))!=Normal(row.image))return false;
    auto command=CommandLine(process.h);int count=0;
    auto args=CommandLineToArgvW(command.c_str(),&count);if(!args)return false;
    bool yes=false;
    for(int i=1;i<count;++i) {
        std::wstring arg=args[i];
        if(arg==L"-u" || arg==L"-B" || arg==L"-E" || arg==L"-I" || arg==L"-s" || arg==L"-S" || arg==L"-O" || arg==L"-OO" || arg==L"-q")continue;
        yes=!arg.empty() && arg[0]!=L'-' && Path(arg).is_absolute() && SameImage(arg,spec.root/L"app/serve/server.py");break;
    }
    LocalFree(args);return yes;
}
const ProcRow* RowOf(const ExternalStopPlan& plan,DWORD pid) {
    for(const auto& row:plan.rows)if(row.pid==pid)return &row;return nullptr;
}
bool SameIdentity(const ProcRow& a,const ProcRow& b) {
    return a.pid==b.pid && a.created && a.created==b.created && SameImage(a.image,b.image);
}
bool AliveRow(const ProcRow& row) {
    Handle process(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,row.pid));
    return Alive(process.h) && Created(process.h)==row.created && SameImage(Image(process.h).native(),row.image);
}
bool Compatible(const ExternalStopPlan& current,const ExternalStopPlan& original,bool exact=false) {
    if(!current.portOk || !current.blocked.empty())return false;
    const auto old=original.All(),now=current.All();
    if(exact && old.size()!=now.size())return false;
    for(DWORD pid:now) {
        const auto* row=RowOf(current,pid);const auto* before=RowOf(original,pid);
        if(before && row && SameIdentity(*row,*before))continue;
        // Only a respawned model from this exact deployment may join an already confirmed stop.
        if(!exact && std::find(current.engines.begin(),current.engines.end(),pid)!=current.engines.end())continue;
        return false;
    }
    return true;
}

ExternalStopPlan PlanForeign(const ServiceSpec& spec) {
    ExternalStopPlan plan;
    auto port=ListeningPort(spec.port);plan.portOk=port.ok;plan.rows=ProcessRows();
    auto rowOf=[&](DWORD pid)->const ProcRow*{for(const auto& row:plan.rows)if(row.pid==pid)return &row;return nullptr;};
    auto engine=spec.root/L"app/engine/strata.exe";
    for(const auto& row:plan.rows)if(SameImage(row.image,engine))plan.engines.push_back(row.pid);
    if(port.ok)for(DWORD pid:port.pids) {
        const ProcRow* row=rowOf(pid);
        if(!row || !PythonLike(*row) || !ServerCommand(*row,spec)){plan.blocked.push_back(pid);continue;}
        plan.listeners.push_back(pid);
    }
    for(DWORD root:plan.listeners)for(const auto& row:plan.rows) {
        if(std::find(plan.listeners.begin(),plan.listeners.end(),row.pid)!=plan.listeners.end())continue;
        const ProcRow* walker=rowOf(row.parent);bool under=false;int guard=0;
        while(walker && guard++<16){if(walker->pid==root){under=true;break;}walker=rowOf(walker->parent);}
        if(!under)continue;
        if((PythonLike(row) && ServerCommand(row,spec)) || SameImage(row.image,engine))plan.children.push_back(row.pid);
    }
    auto venv=spec.root/L"app/.venv";
    for(DWORD pid:plan.listeners) {
        const ProcRow* row=rowOf(pid);if(!row)continue;
        const ProcRow* walker=rowOf(row->parent);int guard=0;
        while(walker && guard++<4) {
            if(PythonLike(*walker) && Under(walker->image,venv) && ServerCommand(*walker,spec) && std::find(plan.shims.begin(),plan.shims.end(),walker->pid)==plan.shims.end())plan.shims.push_back(walker->pid);
            walker=rowOf(walker->parent);
        }
    }
    return plan;
}
std::wstring TargetsText(const ExternalStopPlan& plan) {
    std::vector<std::wstring> lines;
    auto label=[&](DWORD pid,const wchar_t* role){for(const auto& row:plan.rows)if(row.pid==pid)lines.push_back(L"PID "+std::to_wstring(pid)+L" \u00b7 "+row.name+L"（"+role+L"）");};
    for(DWORD pid:plan.listeners)label(pid,L"服务");
    for(DWORD pid:plan.children)label(pid,L"子进程");
    for(DWORD pid:plan.shims)label(pid,L"虚拟环境启动器");
    for(DWORD pid:plan.engines)label(pid,L"模型进程");
    std::wstring out;for(size_t i=0;i<lines.size();++i){if(i)out+=L"\n";out+=lines[i];}
    return out.empty()?L"没有找到可验证的 Strata 进程。":out;
}
std::wstring Indented(const std::wstring& value){std::wstring out;for(wchar_t c:value){out+=c;if(c==L'\n')out+=L"  ";}return out;}
bool AlivePid(DWORD pid){Handle process(OpenProcess(SYNCHRONIZE,FALSE,pid));return process.h && Alive(process.h);}
bool AnyAlive(const std::vector<DWORD>& pids){for(DWORD pid:pids)if(AlivePid(pid))return true;return false;}
bool TerminateOne(const ProcRow& row) {
    Handle process(OpenProcess(PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,row.pid));
    if(!process.h)return !AliveRow(row);
    if(!Alive(process.h))return true;
    if(Created(process.h)!=row.created || !SameImage(Image(process.h).native(),row.image))return false;
    return TerminateProcess(process.h,124) && WaitForSingleObject(process.h,10000)==WAIT_OBJECT_0;
}
bool SendCtrlC(DWORD pid,const ExternalStopPlan& plan) {
    const auto* target=RowOf(plan,pid);if(!target || !AliveRow(*target))return false;
    FreeConsole(); // A GUI owns no terminal; console probes may still have a windowless console.
    if(!AttachConsole(pid))return false;
    SetConsoleCtrlHandler(nullptr,TRUE);
    std::vector<DWORD> members(32);DWORD count=GetConsoleProcessList(members.data(),static_cast<DWORD>(members.size()));
    if(count>members.size()){members.resize(count);count=GetConsoleProcessList(members.data(),static_cast<DWORD>(members.size()));}
    bool safe=count>0 && count<=members.size();const auto allowed=plan.All();
    for(DWORD i=0;safe && i<count;++i)if(members[i]!=GetCurrentProcessId()) {
        const auto* row=RowOf(plan,members[i]);
        safe=std::find(allowed.begin(),allowed.end(),members[i])!=allowed.end() && row && AliveRow(*row);
    }
    BOOL sent=safe?GenerateConsoleCtrlEvent(CTRL_C_EVENT,0):FALSE;
    if(sent)Sleep(100); // keep our ignore handler until delivery is underway
    FreeConsole();return sent!=FALSE;
}
struct HttpHandle{HINTERNET h=nullptr;~HttpHandle(){if(h)WinHttpCloseHandle(h);}};
bool RequestUnload(unsigned port) {
    HttpHandle session;session.h=WinHttpOpen(L"StrataMonitor/0.5",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session.h)return false;
    WinHttpSetTimeouts(session.h,600,2000,2000,4000);
    HttpHandle connection;connection.h=WinHttpConnect(session.h,L"127.0.0.1",static_cast<INTERNET_PORT>(port),0);
    if(!connection.h)return false;
    HttpHandle request;request.h=WinHttpOpenRequest(connection.h,L"POST",L"/unload",nullptr,nullptr,0,0);
    if(!request.h)return false;
    static const char body[]="{}";
    if(!WinHttpSendRequest(request.h,L"Content-Type: application/json\r\n",DWORD(-1),const_cast<char*>(body),sizeof(body)-1,sizeof(body)-1,0))return false;
    if(!WinHttpReceiveResponse(request.h,nullptr))return false;
    DWORD status=0,size=sizeof(status);
    if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX))return false;
    return status==200 || status==409;
}
std::wstring StampText() {
    SYSTEMTIME now{};GetLocalTime(&now);wchar_t text[32];
    swprintf_s(text,L"%04u-%02u-%02u %02u:%02u:%02u",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);return text;
}
void AppendStopLog(const Path& file,const std::wstring& line) {
    try {
        std::error_code ignored;std::filesystem::create_directories(file.parent_path(),ignored);
        std::ofstream out(file,std::ios::binary|std::ios::app);if(!out)return;
        out<<Utf8(StampText())<<" [stop] "<<Utf8(line)<<"\n";out.flush();out.close();
        std::error_code sizeError;auto size=std::filesystem::file_size(file,sizeError);
        if(!sizeError && size>65536) {
            std::ifstream in(file,std::ios::binary);if(!in)return;
            in.seekg(static_cast<std::streamoff>(size)-32768);
            std::string tail((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
            std::ofstream rewritten(file,std::ios::binary|std::ios::trunc);rewritten<<tail;
        }
    }catch(...){} // Stop logging must never take the shutdown down.
}
std::wstring Tail(const std::wstring& path) {
    try {
        std::ifstream f(Path(path),std::ios::binary);if(!f)return L"后台进程已退出，请查看启动日志。";
        f.seekg(0,std::ios::end);auto size=f.tellg();if(size>4096)f.seekg(size-static_cast<std::streamoff>(4096));else f.seekg(0);
        std::string text(std::istreambuf_iterator<char>(f),{});auto pos=text.rfind("[launcher] error:");
        if(pos!=std::string::npos)text.erase(0,pos);else if(text.size()>700)text.erase(0,text.size()-700);
        while(!text.empty() && (text.back()=='\r'||text.back()=='\n'))text.pop_back();return Widen(text);
    } catch(...){return L"后台进程已退出，请查看启动日志。";}
}
}
ServiceSpec ServiceSpec::ForRoot(const Path& root,unsigned port) {
    ServiceSpec s;s.root=std::filesystem::absolute(root).lexically_normal();s.port=port;
    if(port!=517)s.mutex=L"Local\\StrataMonitor.Service-"+std::to_wstring(port);
    s.python=s.root/L"app/.venv/Scripts/python.exe";s.host=s.root/L"monitor/background_host.py";
    s.stateDir=s.root/L"monitor/run";s.logDir=s.root/L"monitor/logs/launcher";return s;
}
Path ServiceSpec::Record()const{return stateDir/(L"service-"+std::to_wstring(port)+L".json");}
Path ServiceSpec::Config(int preset)const{return root/L"app"/(preset?L"strata-iq3_s.json":L"strata-iq3_xxs.json");}
std::wstring QuoteArgument(const std::wstring& s) {
    std::wstring out=L"\"";size_t slashes=0;
    for(wchar_t c:s) {
        if(c==L'\\'){++slashes;continue;}
        if(c==L'\"'){out.append(slashes*2+1,L'\\');out+=c;slashes=0;continue;}
        out.append(slashes,L'\\');slashes=0;out+=c;
    }
    out.append(slashes*2,L'\\');return out+L"\"";
}
PortInfo ListeningPort(unsigned port) {
    PortInfo out;if(!port || port>65535)return out;
    for(ULONG family:{ULONG(AF_INET),ULONG(AF_INET6)}) {
        ULONG size=0;DWORD code=GetExtendedTcpTable(nullptr,&size,FALSE,family,TCP_TABLE_OWNER_PID_LISTENER,0);
        if(code==ERROR_NOT_SUPPORTED && family==AF_INET6)continue;
        if(code!=ERROR_INSUFFICIENT_BUFFER && code!=NO_ERROR)return out;
        std::vector<BYTE> bytes((std::max)(size,ULONG(16)));
        for(int attempt=0;attempt<3;++attempt) {
            code=GetExtendedTcpTable(bytes.data(),&size,FALSE,family,TCP_TABLE_OWNER_PID_LISTENER,0);
            if(code!=ERROR_INSUFFICIENT_BUFFER)break;bytes.resize(size);
        }
        if(code!=NO_ERROR)return out;
        auto add=[&](DWORD value,DWORD pid){unsigned p=((value&255)<<8)|((value>>8)&255);if(p==port && std::find(out.pids.begin(),out.pids.end(),pid)==out.pids.end())out.pids.push_back(pid);};
        if(family==AF_INET) {auto* table=reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(bytes.data());for(DWORD i=0;i<table->dwNumEntries;++i)add(table->table[i].dwLocalPort,table->table[i].dwOwningPid);}
        else {auto* table=reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(bytes.data());for(DWORD i=0;i<table->dwNumEntries;++i)add(table->table[i].dwLocalPort,table->table[i].dwOwningPid);}
    }
    out.ok=true;return out;
}
std::wstring PresetSummary(const ServiceSpec& spec,int preset) {
    try {
        auto j=ReadJson(spec.Config(preset),256*1024);std::wstring context=L"—";
        if(j.contains("args") && j["args"].is_array())for(size_t i=0;i+1<j["args"].size();++i)if(j["args"][i]=="--max-context") {
            auto x=j["args"][i+1];context=x.is_string()?Widen(x.get<std::string>()):std::to_wstring(x.get<unsigned>());break;
        }
        return context+L" context · "+(j.contains("vision")?L"HIP vision":L"text only");
    } catch(...){return L"配置暂不可读；启动时会显示具体错误。";}
}
Service::Service(HWND notify,ServiceSpec spec):notify_(notify),spec_(std::move(spec)) {detach_=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!detach_)throw std::runtime_error("event allocation");}
Service::~Service(){Detach();if(detach_)CloseHandle(detach_);}
void Service::Notify(UINT message){HWND window;{std::lock_guard<std::mutex> lock(mutex_);window=notify_;}if(window)PostMessageW(window,message,0,0);}
ServiceInfo Service::Snapshot()const{std::lock_guard<std::mutex> lock(mutex_);return info_;}
void Service::Failure(const std::wstring& error){{std::lock_guard<std::mutex> lock(mutex_);info_.phase=ServicePhase::Failed;info_.owned=false;info_.error=error;}Notify();}
void Service::CloseManaged() {
    std::lock_guard<std::mutex> lock(mutex_);
    for(HANDLE* h:{&process_,&job_,&stop_,&ready_,&browser_}){if(*h)CloseHandle(*h);*h=nullptr;}
}
void Service::RemoveRecord(){try{auto j=ReadJson(spec_.Record());if(j.value("run",std::string{})==Utf8(run_))DeleteFileW(spec_.Record().c_str());}catch(...){};}
void Service::BeginDetach() {{std::lock_guard<std::mutex> lock(mutex_);notify_=nullptr;}if(detach_)SetEvent(detach_);}
void Service::Detach() {BeginDetach();if(worker_.joinable())worker_.join();CloseManaged();}
bool Service::Recover() {
    if(!std::filesystem::exists(spec_.Record()))return false;
    try {
        auto j=ReadJson(spec_.Record());auto run=Widen(j.at("run").get<std::string>());int preset=j.at("preset").get<int>();
        if(j.at("protocol")!=1 || !ValidRun(run) || preset<0 || preset>1 || j.at("port")!=spec_.port ||
            Normal(Path(Widen(j.at("root").get<std::string>())))!=Normal(spec_.root) ||
            Normal(Path(Widen(j.at("program").get<std::string>())))!=Normal(spec_.python) ||
            Normal(Path(Widen(j.at("host").get<std::string>())))!=Normal(spec_.host) ||
            Normal(Path(Widen(j.at("config").get<std::string>())))!=Normal(spec_.Config(preset)) ||
            Widen(j.at("mutex").get<std::string>())!=spec_.mutex)return false;
        Path logPath(Widen(j.at("log").get<std::string>()));
        if(Normal(logPath.parent_path())!=Normal(spec_.logDir) || logPath.filename().native().find(run+L".log")==std::wstring::npos)return false;
        DWORD pid=j.at("pid").get<DWORD>();if(!pid)return false;
        Handle process(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));
        Handle job(OpenJobObjectW(JOB_OBJECT_QUERY|JOB_OBJECT_TERMINATE,FALSE,Name(L"Job",run).c_str()));
        Handle stop(OpenEventW(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,Name(L"Stop",run).c_str()));
        Handle ready(OpenEventW(SYNCHRONIZE,FALSE,Name(L"Ready",run).c_str()));
        Handle browser(OpenEventW(SYNCHRONIZE,FALSE,Name(L"Browser",run).c_str()));
        if(!Alive(process.h)||!job.h||!stop.h||!ready.h||!browser.h || Created(process.h)!=j.at("created").get<ULONGLONG>() || Normal(Image(process.h))!=Normal(spec_.python) || !Member(process.h,job.h))return false;
        {
            std::lock_guard<std::mutex> lock(mutex_);run_=run;process_=process.Release();job_=job.Release();stop_=stop.Release();ready_=ready.Release();browser_=browser.Release();
            info_.owned=true;info_.pid=pid;info_.preset=preset;info_.log=Widen(j.at("log").get<std::string>());info_.phase=ServicePhase::Starting;
        }
        worker_=std::thread(&Service::Watch,this);Notify();return true;
    }catch(...){CloseManaged();return false;}
}
bool Service::Start(int preset) {
    if(preset<0||preset>1)return false;
    {std::lock_guard<std::mutex> lock(mutex_);if(info_.owned || info_.phase==ServicePhase::Starting || info_.phase==ServicePhase::Stopping)return false;}
    if(worker_.joinable())worker_.join();ResetEvent(detach_);
    {std::lock_guard<std::mutex> lock(mutex_);requestedStop_=false;auto exit=info_.exitAt;info_=ServiceInfo{};info_.exitAt=exit;info_.phase=ServicePhase::Starting;info_.preset=preset;info_.owned=true;}
    try{worker_=std::thread(&Service::Spawn,this,preset);}catch(...){Failure(L"无法创建后台控制线程。");return false;}Notify();return true;
}
bool Service::Stop() {
    {std::lock_guard<std::mutex> lock(mutex_);if(!info_.owned || info_.phase==ServicePhase::Stopping)return false;
        requestedStop_=true;info_.phase=ServicePhase::Stopping;info_.stopAt=GetTickCount64();if(stop_ && !SetEvent(stop_))return false;}
    Notify();return true;
}
bool Service::ForceStop() {
    std::lock_guard<std::mutex> lock(mutex_);
    return info_.owned && info_.phase==ServicePhase::Stopping && job_ && process_ && Member(process_,job_) && TerminateJobObject(job_,125);
}
bool Service::PrepareExternalStop(std::wstring& summary) {
    auto plan=std::make_shared<ExternalStopPlan>(PlanForeign(spec_));
    bool ok=plan->portOk && plan->blocked.empty() && !plan->Empty();
    if(!plan->portOk)summary=L"无法读取本机端口状态，不会结束任何进程。";
    else if(!plan->blocked.empty())summary=L"端口由无法验证为本部署 server.py 的进程占用，不会停止任何进程。";
    else summary=TargetsText(*plan);
    std::lock_guard<std::mutex> lock(mutex_);externalPlan_=ok?plan:nullptr;return ok;
}
bool Service::StopExternal() {
    std::shared_ptr<const ExternalStopPlan> plan;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(info_.owned || info_.phase==ServicePhase::Starting || info_.phase==ServicePhase::Stopping || !externalPlan_)return false;
        plan=externalPlan_;externalPlan_.reset();
        requestedStop_=true;auto exit=info_.exitAt;info_=ServiceInfo{};info_.exitAt=exit;
        info_.phase=ServicePhase::Stopping;info_.foreign=true;info_.stopAt=GetTickCount64();
    }
    if(worker_.joinable())worker_.join();ResetEvent(detach_);
    try{worker_=std::thread(&Service::StopForeign,this,plan);}catch(...){Failure(L"无法创建停止控制线程。");return false;}
    Notify();return true;
}
void Service::WaitQuiet(const std::vector<DWORD>& targets,int seconds) {
    ULONGLONG deadline=GetTickCount64()+static_cast<ULONGLONG>(seconds)*1000;
    for(;;) {
        if(!AnyAlive(targets))return;
        if(WaitForSingleObject(detach_,0)==WAIT_OBJECT_0) {
            ULONGLONG soon=GetTickCount64()+3000;if(soon<deadline)deadline=soon;
        }
        if(GetTickCount64()>=deadline)return;Sleep(250);
    }
}
void Service::StopForeign(std::shared_ptr<const ExternalStopPlan> confirmed) {
    try {
        auto stopLog=spec_.logDir.parent_path()/L"stop.log";
        {std::lock_guard<std::mutex> lock(mutex_);info_.log=stopLog.native();}
        auto plan=PlanForeign(spec_);
        if(!Compatible(plan,*confirmed,true)){Failure(L"确认期间服务进程已改变，未停止任何进程。请重新确认。");return;}
        AppendStopLog(stopLog,L"收到停止请求，目标：\n"+Indented(TargetsText(plan)));
        bool signalled=false;
        for(DWORD pid:plan.listeners)if(SendCtrlC(pid,plan)){signalled=true;AppendStopLog(stopLog,L"已向仅包含已验证目标的终端发送 Ctrl+C。");break;}
        if(signalled)WaitQuiet(plan.All(),45);
        else AppendStopLog(stopLog,L"未发送 Ctrl+C：终端不存在、不可访问或包含未确认的进程。");
        auto current=PlanForeign(spec_);
        if(!Compatible(current,*confirmed)){Failure(L"停止期间端口或服务身份已改变，未继续结束进程。请重新确认。");return;}
        if(AnyAlive(plan.All()) && !current.listeners.empty()) {
            if(RequestUnload(spec_.port))AppendStopLog(stopLog,L"已请求服务释放模型（POST /unload）。");
            WaitQuiet(plan.All(),10);
        }
        current=PlanForeign(spec_);
        if(!Compatible(current,*confirmed)){Failure(L"停止期间端口或服务身份已改变，未继续结束进程。请重新确认。");return;}
        // Retain confirmed shims/children even after their listener has gone away.
        auto terminate=[&](const ExternalStopPlan& targets) {
            for(DWORD pid:targets.Ordered()) {
                const auto* row=RowOf(targets,pid);
                if(row && AliveRow(*row))AppendStopLog(stopLog,TerminateOne(*row)?L"已结束 PID "+std::to_wstring(pid):L"结束 PID "+std::to_wstring(pid)+L" 失败");
            }
        };
        terminate(current);terminate(*confirmed);
        WaitQuiet(current.All(),15);
        auto left=PlanForeign(spec_);
        if(!Compatible(left,*confirmed)){Failure(L"端口已被其他实例占用，未结束新实例。请重新确认。");return;}
        terminate(left);left=PlanForeign(spec_);
        bool originalAlive=false;for(DWORD pid:confirmed->All()){const auto* row=RowOf(*confirmed,pid);if(row && AliveRow(*row))originalAlive=true;}
        bool clean=left.portOk && left.blocked.empty() && left.Empty() && !originalAlive;
        AppendStopLog(stopLog,clean?L"停止完成：已验证的进程已退出，端口已释放。":L"停止未完成，仍有进程存活；请重新确认或查看日志。");
        {
            std::lock_guard<std::mutex> lock(mutex_);info_.exitAt=GetTickCount64();info_.foreign=true;
            info_.phase=clean?ServicePhase::Off:ServicePhase::Failed;
            info_.error=clean?L"":L"仍有 Strata 进程未退出；详见 monitor/logs/stop.log。";
        }
        Notify();
    }catch(const std::exception&){Failure(L"停止状态检查失败；未继续结束未验证的进程，请查看日志并重试。");}
}
bool Service::OwnsListener()const {
    auto port=ListeningPort(spec_.port);if(!port.ok || !port.Busy())return false;std::lock_guard<std::mutex> lock(mutex_);if(!info_.owned || !job_)return false;
    for(DWORD pid:port.pids){Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));if(!process.h || !Member(process.h,job_))return false;}return true;
}
void Service::Spawn(int preset) {
    Handle process,thread,job,stop,ready,browser,log,input;bool resumed=false;
    try {
        auto port=ListeningPort(spec_.port);if(!port.ok)throw std::runtime_error("无法确认端口状态，未启动任何进程。");if(port.Busy())throw std::runtime_error("服务端口已被占用，未结束占用进程；请连接已有服务。");
        if(std::filesystem::exists(spec_.Record()))try{auto old=ReadJson(spec_.Record());DWORD pid=old.value("pid",DWORD(0));Handle p(OpenProcess(SYNCHRONIZE,FALSE,pid));if(Alive(p.h))throw std::runtime_error("存在仍存活但未验证的启动记录，未替换该进程。");}catch(const nlohmann::json::exception&){throw std::runtime_error("启动记录无效，未覆盖记录。");}
        for(const Path& p:{spec_.python,spec_.host,spec_.Config(preset),spec_.root/L"app/serve/server.py"})if(!std::filesystem::is_regular_file(p))throw std::runtime_error("启动所需文件缺失，请检查Python、Host、服务脚本和模型配置。");
        std::filesystem::create_directories(spec_.stateDir);std::filesystem::create_directories(spec_.logDir);
        run_=Identifier();SECURITY_ATTRIBUTES inherit{sizeof(inherit),nullptr,TRUE};
        job.h=CreateJobObjectW(&inherit,Name(L"Job",run_).c_str());if(!job.h || GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("cannot create private job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job.h,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("cannot configure private job");
        stop.h=CreateEventW(&inherit,TRUE,FALSE,Name(L"Stop",run_).c_str());if(!stop.h || GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("cannot create stop event");
        ready.h=CreateEventW(&inherit,TRUE,FALSE,Name(L"Ready",run_).c_str());if(!ready.h || GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("cannot create ready event");
        browser.h=CreateEventW(&inherit,FALSE,FALSE,Name(L"Browser",run_).c_str());if(!browser.h || GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("cannot create browser event");
        SYSTEMTIME now{};GetLocalTime(&now);wchar_t prefix[64];swprintf_s(prefix,L"startup-%04u%02u%02u-%02u%02u%02u-",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);
        auto logPath=spec_.logDir/(std::wstring(prefix)+run_+L".log");log.h=CreateFileW(logPath.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,&inherit,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        input.h=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&inherit,OPEN_EXISTING,0,nullptr);if(!log.h || !input.h)throw std::runtime_error("cannot open startup log");
        RetainLogs(spec_.logDir);
        {std::lock_guard<std::mutex> lock(mutex_);info_.log=logPath.native();}
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<BYTE> attributes(bytes);
        auto* list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());if(!InitializeProcThreadAttributeList(list,1,0,&bytes))throw std::runtime_error("handle list");
        struct AttributeGuard{LPPROC_THREAD_ATTRIBUTE_LIST p;~AttributeGuard(){DeleteProcThreadAttributeList(p);}} guard{list};
        HANDLE inherited[]={job.h,stop.h,ready.h,browser.h,log.h,input.h};if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr))throw std::runtime_error("handle inheritance");
        STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;si.StartupInfo.hStdInput=input.h;si.StartupInfo.hStdOutput=log.h;si.StartupInfo.hStdError=log.h;si.lpAttributeList=list;
        std::wstring command=QuoteArgument(spec_.python.native())+L" -B "+QuoteArgument(spec_.host.native())+L" --record "+QuoteArgument(spec_.Record().native())+L" --run "+run_;
        PROCESS_INFORMATION pi{};auto cwd=spec_.root/L"app";
        if(!CreateProcessW(spec_.python.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT,nullptr,cwd.c_str(),&si.StartupInfo,&pi))throw std::runtime_error(Utf8(SystemError(L"后台启动")));
        process.h=pi.hProcess;thread.h=pi.hThread;
        if(!AssignProcessToJobObject(job.h,process.h)) {TerminateProcess(process.h,1);throw std::runtime_error("无法隔离后台进程组，未开始加载模型。");}
        Json record={{"protocol",1},{"run",Utf8(run_)},{"pid",pi.dwProcessId},{"created",Created(process.h)},{"root",Utf8(spec_.root.native())},{"program",Utf8(spec_.python.native())},{"host",Utf8(spec_.host.native())},{"config",Utf8(spec_.Config(preset).native())},{"preset",preset},{"port",spec_.port},{"mutex",Utf8(spec_.mutex)},{"log",Utf8(logPath.native())}};
        WriteJson(spec_.Record(),record,run_);
        if(ResumeThread(thread.h)==DWORD(-1))throw std::runtime_error("cannot resume host");resumed=true;
        for(HANDLE h:{job.h,stop.h,ready.h,browser.h})SetHandleInformation(h,HANDLE_FLAG_INHERIT,0);
        {
            std::lock_guard<std::mutex> lock(mutex_);process_=process.Release();job_=job.Release();stop_=stop.Release();ready_=ready.Release();browser_=browser.Release();info_.pid=pi.dwProcessId;
            if(requestedStop_)SetEvent(stop_);
        }
        Notify();Watch();
    } catch(const std::exception& e) {
        if(job.h && process.h)TerminateJobObject(job.h,1);else if(process.h && !resumed)TerminateProcess(process.h,1);
        RemoveRecord();Failure(Widen(e.what()));
    }
}
void Service::Watch() {
    bool ready=false;
    for(;;) {
        HANDLE handles[4];{std::lock_guard<std::mutex> lock(mutex_);handles[0]=detach_;handles[1]=process_;handles[2]=browser_;handles[3]=ready_;}
        DWORD code=WaitForMultipleObjects(ready?3:4,handles,FALSE,INFINITE);
        if(code==WAIT_OBJECT_0){CloseManaged();return;}
        if(code==WAIT_OBJECT_0+3) {
            ready=true;{std::lock_guard<std::mutex> lock(mutex_);if(!requestedStop_)info_.phase=ServicePhase::Active;}Notify();continue;
        }
        if(code==WAIT_OBJECT_0+2) {{std::lock_guard<std::mutex> lock(mutex_);++info_.browserRequests;}Notify(WM_SERVICE_BROWSER);continue;}
        DWORD exitCode=1;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(process_)GetExitCodeProcess(process_,&exitCode);
            // The root is gone: end only this validated job's leftover children.
            if(job_)TerminateJobObject(job_,exitCode);
        }
        for(int i=0;i<250;++i) {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};bool done;
            {std::lock_guard<std::mutex> lock(mutex_);done=!job_ || !QueryInformationJobObject(job_,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr) || accounting.ActiveProcesses==0;}
            if(done)break;Sleep(20);
        }
        RemoveRecord();CloseManaged();
        {
            std::lock_guard<std::mutex> lock(mutex_);info_.owned=false;info_.exitCode=exitCode;info_.exitAt=GetTickCount64();
            info_.phase=requestedStop_ || exitCode==0?ServicePhase::Off:ServicePhase::Failed;
            info_.error=info_.phase==ServicePhase::Failed?Tail(info_.log):L"";
        }
        Notify();return;
    }
}
}
