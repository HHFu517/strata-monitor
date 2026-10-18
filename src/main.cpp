#include "view.hpp"
#include "settings.hpp"
#include "updates.hpp"
#include "launcher.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"dwmapi.lib")
namespace strata {
namespace {
constexpr UINT WM_TRAY=WM_APP+2, WM_RESTORE=WM_APP+3;
enum Command { Show=201, ToggleTop, ToggleCompact, ToTray, RefreshNow, Rate1, Rate2, Rate5, Quit, ResetSize, LauncherPage, StopService, OpenLog, OpenWeb, ForceService };
struct Buffer {
    HDC dc=nullptr; HBITMAP bitmap=nullptr,old=nullptr; int w=0,h=0;
    void Clear() {
        if(dc && old)SelectObject(dc,old);
        if(bitmap)DeleteObject(bitmap);
        if(dc)DeleteDC(dc);
        dc=nullptr;bitmap=nullptr;old=nullptr;w=h=0;
    }
    ~Buffer(){Clear();}
    bool Ensure(HDC target,int width,int height) {
        if(dc && width==w && height==h)return true;
        Clear();if(width<=0 || height<=0)return false;
        dc=CreateCompatibleDC(target);bitmap=CreateCompatibleBitmap(target,width,height);
        if(!dc || !bitmap){Clear();return false;}
        old=static_cast<HBITMAP>(SelectObject(dc,bitmap));w=width;h=height;return true;
    }
};
struct App {
    HWND window=nullptr,tooltip=nullptr,buttons[4]{},stopButton=nullptr;
    bool stopEnabled=false,stopArmed=false,externalLive=false;ULONGLONG portChecked=0;
    HICON icon=nullptr; unsigned dpi=96; int hot=0;
    bool tray=false,quitting=false,launcher=false,manualLauncher=false,wasOwned=false,blockAttach=false,readyValidated=false,pendingBrowser=false,hasMonitored=false;
    ULONGLONG readyAfter=0;
    HWND launchButtons[5]{};
    LauncherState launch;
    ServiceSpec spec;
    std::unique_ptr<Service> service;
    UINT taskbarCreated=0;
    Settings settings;
    ViewState view;
    Buffer buffer;
    std::unique_ptr<TextResources> text;
    TrayTipCache trayTips;
    std::unique_ptr<Client> client;
    int Px(int dip)const{return MulDiv(dip,static_cast<int>(dpi),96);}
    void RememberSize() {
        if(!window || IsIconic(window) || IsZoomed(window))return;
        RECT r{};GetClientRect(window,&r);
        int w=MulDiv(r.right,96,static_cast<int>(dpi)),h=MulDiv(r.bottom,96,static_cast<int>(dpi));
        if(launcher){if(w>=MinLaunchWidth && h>=MinLaunchHeight){settings.launchWidth=w;settings.launchHeight=h;}return;}
        if(w<MinWindowWidth || h<(view.compact?MinMiniHeight:MinFullHeight))return;
        if(view.compact){settings.miniWidth=w;settings.miniHeight=h;}
        else {settings.width=w;settings.height=h;}
    }
    void Save() {
        RememberSize();
        if(!IsIconic(window) && !IsZoomed(window)) {
            RECT r{};GetWindowRect(window,&r);
            if(launcher){settings.launchX=r.left;settings.launchY=r.top;}else {settings.x=r.left;settings.y=r.top;}
        }
        settings.compact=view.compact;settings.topmost=view.topmost;settings.interval=view.interval;settings.Save();
    }
    void Layout() {
        RECT r{};GetClientRect(window,&r);
        for(int i=0;i<4;++i)if(buttons[i]) {
            int slot=i==2?3:i==3?2:i;
            SetWindowPos(buttons[i],nullptr,r.right-Px(HeaderCluster-slot*HeaderSlot),Px(ControlTop),Px(ControlWidth),Px(ControlHeight),SWP_NOZORDER|SWP_NOACTIVATE);
            InvalidateRect(buttons[i],nullptr,FALSE);
        }
        if(buttons[1])EnableWindow(buttons[1],!launcher);
        if(stopButton) {
            if(launcher)ShowWindow(stopButton,SW_HIDE);
            else {
                ShowWindow(stopButton,SW_SHOWNA);
                if(view.compact)SetWindowPos(stopButton,nullptr,r.right-Px(HeaderStop),Px(ControlTop),Px(ControlWidth),Px(ControlHeight),SWP_NOZORDER|SWP_NOACTIVATE);
                else {
                    const auto action=Arrange(r.right*96.0f/static_cast<float>(dpi),r.bottom*96.0f/static_cast<float>(dpi)).action;
                    SetWindowPos(stopButton,nullptr,Px(static_cast<int>(action.x)),Px(static_cast<int>(action.y)),
                        Px(static_cast<int>(action.w)),Px(static_cast<int>(action.h)),SWP_NOZORDER|SWP_NOACTIVATE);
                }
            }
            InvalidateRect(stopButton,nullptr,FALSE);
        }
        const int ids[]={PresetXXS,PresetS,ServiceAction,LogAction,MonitorAction};
        for(int i=0;i<5;++i)if(launchButtons[i]) {
            ShowWindow(launchButtons[i],launcher?SW_SHOWNA:SW_HIDE);
            int x=24,y=164,w=MulDiv(r.right,96,static_cast<int>(dpi))-48,h=76;
            if(i==1)y=250;
            if(i==2){y=MulDiv(r.bottom,96,static_cast<int>(dpi))-116;w-=132;h=48;}
            if(i==3){x+=w-120;y=MulDiv(r.bottom,96,static_cast<int>(dpi))-116;w=120;h=48;}
            if(i==4){x+=w-132;y=338;w=132;h=36;ShowWindow(launchButtons[i],launcher && launch.ready && launch.owned?SW_SHOWNA:SW_HIDE);}
            SetWindowPos(launchButtons[i],nullptr,Px(x),Px(y),Px(w),Px(h),SWP_NOZORDER|SWP_NOACTIVATE);
        }
        (void)ids;
    }
    void Clamp() {
        if(IsZoomed(window))return;
        RECT r{};GetWindowRect(window,&r);
        MONITORINFO info{sizeof(info)};
        if(!GetMonitorInfoW(MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST),&info))return;
        int width=r.right-r.left,height=r.bottom-r.top;
        int x=(std::max)(info.rcWork.left,(std::min)(r.left,info.rcWork.right-width));
        int y=(std::max)(info.rcWork.top,(std::min)(r.top,info.rcWork.bottom-height));
        SetWindowPos(window,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    void Resize() {
        SetWindowPos(window,view.topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,Px(launcher?settings.launchWidth:view.compact?settings.miniWidth:settings.width),Px(launcher?settings.launchHeight:view.compact?settings.miniHeight:settings.height),SWP_NOMOVE|SWP_NOACTIVATE);
        Clamp();Layout();InvalidateRect(window,nullptr,FALSE);
    }
    NOTIFYICONDATAW TrayData() {
        NOTIFYICONDATAW n{sizeof(n)};n.hWnd=window;n.uID=1;return n;
    }
    void TrayTip() {
        if(!tray)return;
        std::wstring tip=service && service->Snapshot().owned && !launch.ready?L"Strata · "+launch.title:TrayText(view);
        trayTips.Update(tip,[&](const std::wstring& tip) {
            auto n=TrayData();n.uFlags=NIF_TIP|NIF_SHOWTIP;
            wcsncpy_s(n.szTip,tip.c_str(),_TRUNCATE);
            return Shell_NotifyIconW(NIM_MODIFY,&n)!=FALSE;
        });
    }
    void AddTray() {
        trayTips.Reset();
        auto n=TrayData();n.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP|NIF_SHOWTIP;
        n.uCallbackMessage=WM_TRAY;n.hIcon=icon;
        wcscpy_s(n.szTip,L"Strata Monitor");tray=Shell_NotifyIconW(NIM_ADD,&n)!=FALSE;
        if(tray){n.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&n);TrayTip();}
    }
    void Reveal() {
        ShowWindow(window,IsIconic(window)?SW_RESTORE:SW_SHOW);Clamp();SetForegroundWindow(window);
        if(client)client->Configure(view.interval,false);
    }
    void HideWindow() {
        if(!tray)AddTray();
        if(!tray){MessageBoxW(window,L"系统托盘暂不可用，窗口将保持显示。",L"Strata Monitor",MB_OK|MB_ICONINFORMATION);return;}
        Save();ShowWindow(window,SW_HIDE);
        if(client)client->Configure(view.interval,true);
    }
    void CommandId(int id);
    void Popup(POINT point);
    void CreateControls();
    void SetPage(bool value,bool manual=false);
    void UpdateLifecycle(bool packet=false);
    void StopManaged();
    void StopRequested();
    void Open(const std::wstring& target);
};

LRESULT CALLBACK ButtonProc(HWND window,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref) {
    auto* app=reinterpret_cast<App*>(ref);
    if(msg==WM_MOUSEMOVE) {
        int id=GetDlgCtrlID(window);
        if(app->hot!=id){app->hot=id;InvalidateRect(window,nullptr,FALSE);}
        TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);
    } else if(msg==WM_MOUSELEAVE) {
        app->hot=0;InvalidateRect(window,nullptr,FALSE);
    } else if(msg==WM_ERASEBKGND)return 1;
    return DefSubclassProc(window,msg,wp,lp);
}
void App::CreateControls() {
    const int ids[]={Pin,Compact,Hide,Menu};
    const wchar_t* names[]={L"置顶（Ctrl+T）",L"切换紧凑视图（Ctrl+M）",L"收起到托盘（Esc）",L"更多选项"};
    tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,
        CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(tooltip)SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,300);
    for(int i=0;i<4;++i) {
        buttons[i]=CreateWindowExW(0,L"BUTTON",names[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[i])),GetModuleHandleW(nullptr),nullptr);
        SetWindowSubclass(buttons[i],ButtonProc,1,reinterpret_cast<DWORD_PTR>(this));
        TOOLINFOW tool{sizeof(tool)};tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tool.hwnd=window;
        tool.uId=reinterpret_cast<UINT_PTR>(buttons[i]);tool.lpszText=const_cast<LPWSTR>(names[i]);
        if(tooltip)SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
    }
    stopButton=CreateWindowExW(0,L"BUTTON",L"停止服务",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(StopControl)),GetModuleHandleW(nullptr),nullptr);
    if(stopButton) {
        SetWindowSubclass(stopButton,ButtonProc,1,reinterpret_cast<DWORD_PTR>(this));
        TOOLINFOW tool{sizeof(tool)};tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tool.hwnd=window;
        tool.uId=reinterpret_cast<UINT_PTR>(stopButton);tool.lpszText=const_cast<LPWSTR>(L"停止本机 Strata 服务（含终端启动的实例）");
        if(tooltip)SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
    }
    const int launchIds[]={PresetXXS,PresetS,ServiceAction,LogAction,MonitorAction};
    const wchar_t* launchNames[]={L"IQ3_XXS",L"IQ3_S",L"启动服务",L"查看日志",L"返回监控"};
    for(int i=0;i<5;++i){
        launchButtons[i]=CreateWindowExW(0,L"BUTTON",launchNames[i],WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(launchIds[i])),GetModuleHandleW(nullptr),nullptr);
        SetWindowSubclass(launchButtons[i],ButtonProc,1,reinterpret_cast<DWORD_PTR>(this));
    }
    for(int j=0;j<2;++j){auto name=std::wstring(j?L"IQ3_S · ":L"IQ3_XXS · ")+launch.summary[j]+(launch.preset==j?L" · 已选择":L"");SetWindowTextW(launchButtons[j],name.c_str());}
    EnableWindow(launchButtons[3],FALSE);
    Layout();
}
void App::Open(const std::wstring& target) {
    if(target.empty())return;
    auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",target.c_str(),nullptr,nullptr,SW_SHOWNORMAL));
    if(result<=32)MessageBoxW(window,L"无法打开目标，请检查默认应用或日志路径。",L"Strata",MB_OK|MB_ICONWARNING);
}
void App::SetPage(bool value,bool manual) {
    if(manual)manualLauncher=value;
    if(launcher==value)return;
    Save();if(IsZoomed(window))ShowWindow(window,SW_RESTORE);
    launcher=value;
    int x=launcher?settings.launchX:settings.x,y=launcher?settings.launchY:settings.y;
    if(x==-32000 || y==-32000){RECT r{};GetWindowRect(window,&r);MONITORINFO m{sizeof(m)};GetMonitorInfoW(MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST),&m);x=m.rcWork.left+(m.rcWork.right-m.rcWork.left-Px(launcher?settings.launchWidth:view.compact?settings.miniWidth:settings.width))/2;y=m.rcWork.top+(m.rcWork.bottom-m.rcWork.top-Px(launcher?settings.launchHeight:view.compact?settings.miniHeight:settings.height))/2;}
    SetWindowPos(window,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);Resize();Save();
    if(launcher && launchButtons[2])SetFocus(launchButtons[2]);else if(buttons[0])SetFocus(buttons[0]);
}
void App::UpdateLifecycle(bool packet) {
    if(!service)return;
    const auto i=service->Snapshot();
    if(wasOwned && !i.owned){blockAttach=true;readyValidated=false;manualLauncher=false;SetPage(true);}
    wasOwned=i.owned;
    if(packet && !view.packet.connected)blockAttach=false;
    Packet state=view.packet;
    if(blockAttach || (i.owned && state.received<readyAfter))state.connected=false;
    auto next=LaunchState(i,state,settings.preset,launch.summary[0],launch.summary[1]);
    if(next.ready && i.owned && !readyValidated){readyValidated=service->OwnsListener();next.ready=readyValidated;}
    if(!(next==launch)){
        launch=std::move(next);
        EnableWindow(launchButtons[0],launch.selectable);EnableWindow(launchButtons[1],launch.selectable);
        EnableWindow(launchButtons[2],launch.actionEnabled);EnableWindow(launchButtons[3],!launch.log.empty());
        SetWindowTextW(launchButtons[2],launch.action.c_str());
        for(int j=0;j<2;++j){auto name=std::wstring(j?L"IQ3_S · ":L"IQ3_XXS · ")+launch.summary[j]+(launch.preset==j?L" · 已选择":L"");SetWindowTextW(launchButtons[j],name.c_str());InvalidateRect(launchButtons[j],nullptr,FALSE);}
        if(launcher){Layout();for(auto button:launchButtons)InvalidateRect(button,nullptr,FALSE);InvalidateRect(window,nullptr,FALSE);}
    }
    if(!manualLauncher && i.phase!=ServicePhase::Stopping){
        if(launch.ready){hasMonitored=true;SetPage(false);}
        else if(packet && !view.packet.connected){
            bool missing=!hasMonitored;
            if(hasMonitored && !i.owned){auto port=ListeningPort(settings.port);missing=port.ok && !port.Busy();}
            if(missing){hasMonitored=false;SetPage(true);}
        }
    }
    if(pendingBrowser && launch.ready){pendingBrowser=false;Open(L"http://127.0.0.1:"+std::to_wstring(settings.port)+L"/");}
    // A live /metrics answer proves the service; only a quiet port re-checks the listener table.
    const bool stopping=i.phase==ServicePhase::Stopping;
    if(!i.owned) {
        if(view.packet.connected)externalLive=true;
        else if(GetTickCount64()-portChecked>=1000) {
            portChecked=GetTickCount64();
            const auto port=ListeningPort(settings.port);externalLive=port.ok && port.Busy();
        }
    }
    const bool enable=i.owned || externalLive;   // stays paintable so 正在停止 is visible; StopRequested() refuses re-entry
    if(stopButton) {
        if(enable!=stopEnabled){stopEnabled=enable;EnableWindow(stopButton,enable?TRUE:FALSE);InvalidateRect(stopButton,nullptr,FALSE);}
        if(stopping!=stopArmed){stopArmed=stopping;SetWindowTextW(stopButton,stopping?L"正在停止服务":L"停止服务");InvalidateRect(stopButton,nullptr,FALSE);}
    } else stopEnabled=enable,stopArmed=stopping;
    TrayTip();
}
void App::StopManaged() {
    if(!service || !service->Snapshot().owned)return;
    if(MessageBoxW(window,L"停止服务会中断当前请求，并释放模型、内存和显存；提示词缓存也会丢失。\n\n确定停止？",L"停止 Strata 服务",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
    manualLauncher=false;if(service->Stop()){SetPage(true);UpdateLifecycle();}
}
void App::StopRequested() {
    if(!service)return;
    const auto info=service->Snapshot();
    if(info.phase==ServicePhase::Stopping)return;
    if(info.owned){StopManaged();return;}
    std::wstring targets;
    if(!service->PrepareExternalStop(targets)) {
        MessageBoxW(window,(L"没有找到可验证的 Strata 进程，或端口被无法确认的进程占用，监控不会结束任何进程。\n\n"+targets).c_str(),
            L"停止 Strata 服务",MB_OK|MB_ICONWARNING);
        return;
    }
    std::wstring listed;
    for(wchar_t c:targets){listed+=c;if(c==L'\n')listed+=L"  ";}
    const std::wstring warning=L"本机只支持一个 Strata 服务。停止会结束下列已验证的进程（包含从终端启动的实例）：\n\n  "+listed
        +L"\n\n监控仅在终端没有无关进程时发送 Ctrl+C；否则先请求释放模型，再定向结束已验证的进程。"+
        L"\n当前请求会中断，模型、内存、显存和提示词缓存都会丢失。\n\n确定停止？";
    if(MessageBoxW(window,warning.c_str(),L"停止本机 Strata 服务",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
    manualLauncher=false;pendingBrowser=false;blockAttach=false;hasMonitored=false;readyValidated=false;
    if(service->StopExternal()){SetPage(true);UpdateLifecycle();}
}
void App::CommandId(int id) {
    if(id==Show)Reveal();
    else if(id==LauncherPage){SetPage(true,true);UpdateLifecycle();}
    else if(id==MonitorAction){if(launch.ready)SetPage(false,true);}
    else if(id==PresetXXS || id==PresetS){if(launch.selectable){settings.preset=id==PresetS?1:0;Save();UpdateLifecycle();}}
    else if(id==ServiceAction){
        auto info=service?service->Snapshot():ServiceInfo{};
        if(service && info.owned)StopManaged();
        else if(launch.ready)SetPage(false,true);
        else if(service && info.foreign && info.phase==ServicePhase::Failed)StopRequested();
        else if(launch.actionEnabled && service){manualLauncher=false;readyValidated=false;pendingBrowser=false;blockAttach=false;readyAfter=GetTickCount64();service->Start(settings.preset);SetPage(true);UpdateLifecycle();}
    }
    else if(id==StopService || id==StopControl)StopRequested();
    else if(id==LogAction || id==OpenLog)Open(launch.log);
    else if(id==OpenWeb && ModelReady(view.packet))Open(L"http://127.0.0.1:"+std::to_wstring(settings.port)+L"/");
    else if(id==ForceService && service){auto i=service->Snapshot();if(i.owned && i.phase==ServicePhase::Stopping && GetTickCount64()-i.stopAt>=65000 && MessageBoxW(window,L"正常停止仍未完成。强制结束仅作用于本启动器的已验证进程组，未保存的请求和缓存将丢失。\n\n确定强制结束？",L"强制结束托管服务",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)==IDYES)service->ForceStop();}
    else if(id==Pin || id==ToggleTop) {
        view.topmost=!view.topmost;
        SetWindowPos(window,view.topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        Layout();Save();
    } else if(id==Compact || id==ToggleCompact) {
        if(launcher)return;
        RememberSize();if(IsZoomed(window))ShowWindow(window,SW_RESTORE);
        view.compact=!view.compact;Resize();Save();
    } else if(id==Hide || id==ToTray)HideWindow();
    else if(id==RefreshNow){if(client)client->Refresh();}
    else if(id==Rate1 || id==Rate2 || id==Rate5) {
        view.interval=id==Rate1?1000u:id==Rate2?2000u:5000u;
        if(client)client->Configure(view.interval,IsWindowVisible(window)==FALSE);
        Save();InvalidateRect(window,nullptr,FALSE);
    } else if(id==ResetSize) {
        if(IsZoomed(window))ShowWindow(window,SW_RESTORE);
        if(launcher){settings.launchWidth=LaunchWidth;settings.launchHeight=LaunchHeight;}
        else if(view.compact){settings.miniWidth=WindowWidth;settings.miniHeight=MiniHeight;}
        else {settings.width=WindowWidth;settings.height=FullHeight;}
        Resize();Save();
    } else if(id==Quit){quitting=true;DestroyWindow(window);}
    else if(id==Menu) {
        RECT r{};GetWindowRect(buttons[3],&r);Popup(POINT{r.right,r.bottom});
    }
}
void App::Popup(POINT point) {
    HMENU menu=CreatePopupMenu(),rates=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,Show,L"显示窗口");
    AppendMenuW(menu,MF_STRING,LauncherPage,L"启动与服务管理");
    if(launch.ready)AppendMenuW(menu,MF_STRING,MonitorAction,L"返回监控");
    AppendMenuW(menu,MF_STRING|(ModelReady(view.packet)?0:MF_GRAYED),OpenWeb,L"打开网页");
    AppendMenuW(menu,MF_STRING|(launch.log.empty()?MF_GRAYED:0),OpenLog,L"查看启动日志");
    auto managed=service?service->Snapshot():ServiceInfo{};
    AppendMenuW(menu,MF_STRING|(managed.phase==ServicePhase::Stopping?MF_GRAYED:0),StopService,managed.owned?L"停止服务…":L"停止本机 Strata 服务…");
    if(managed.owned && managed.phase==ServicePhase::Stopping && GetTickCount64()-managed.stopAt>=65000)AppendMenuW(menu,MF_STRING,ForceService,L"强制结束托管服务…");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|(view.topmost?MF_CHECKED:0),ToggleTop,L"窗口置顶\tCtrl+T");
    AppendMenuW(menu,MF_STRING|(view.compact?MF_CHECKED:0)|(launcher?MF_GRAYED:0),ToggleCompact,L"紧凑视图\tCtrl+M");
    AppendMenuW(menu,MF_STRING,ResetSize,L"恢复默认大小");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(rates,MF_STRING|(view.interval==1000?MF_CHECKED:0),Rate1,L"1 秒");
    AppendMenuW(rates,MF_STRING|(view.interval==2000?MF_CHECKED:0),Rate2,L"2 秒");
    AppendMenuW(rates,MF_STRING|(view.interval==5000?MF_CHECKED:0),Rate5,L"5 秒");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(rates),L"刷新间隔");
    AppendMenuW(menu,MF_STRING,RefreshNow,L"立即刷新\tF5");
    AppendMenuW(menu,MF_STRING,ToTray,L"收起到托盘\tEsc");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,Quit,L"退出界面，后台继续\tCtrl+Q");
    SetForegroundWindow(window);
    UINT choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);
    DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);
    if(choice)CommandId(static_cast<int>(choice));
    if(tray){auto n=TrayData();Shell_NotifyIconW(NIM_SETFOCUS,&n);}
}
LRESULT CALLBACK WindowProc(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(msg==WM_NCCREATE) {
        auto* create=reinterpret_cast<CREATESTRUCTW*>(lp);
        app=static_cast<App*>(create->lpCreateParams);app->window=window;
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
    }
    if(!app)return DefWindowProcW(window,msg,wp,lp);
    if(msg==app->taskbarCreated && msg){app->tray=false;app->AddTray();return 0;}
    switch(msg) {
    case WM_NCCALCSIZE:return 0;
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize={app->Px(app->launcher?MinLaunchWidth:MinWindowWidth),app->Px(app->launcher?MinLaunchHeight:app->view.compact?MinMiniHeight:MinFullHeight)};
        MONITORINFO monitor{sizeof(monitor)};
        if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) {
            info->ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};
            info->ptMaxSize={monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};
        }
        return 0;
    }
    case WM_CREATE:app->CreateControls();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};HDC dc=BeginPaint(window,&ps);RECT r{};GetClientRect(window,&r);
        if(app->buffer.Ensure(dc,r.right,r.bottom)) {
            {
                Gdiplus::Graphics g(app->buffer.dc);g.ScaleTransform(app->dpi/96.0f,app->dpi/96.0f);
                const float width=r.right*96.0f/app->dpi,height=r.bottom*96.0f/app->dpi;
                if(app->launcher)RenderLauncher(g,app->launch,width,height,*app->text);
                else Render(g,app->view,width,height,*app->text);
                Gdiplus::Pen edge(Gdiplus::Color(255,218,226,237),1);
                g.DrawRectangle(&edge,0.5f,0.5f,width-1,height-1);
            }
            BitBlt(dc,0,0,r.right,r.bottom,app->buffer.dc,0,0,SRCCOPY);
        }
        EndPaint(window,&ps);return 0;
    }
    case WM_DRAWITEM: {
        const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(item->CtlID>=PresetXXS && item->CtlID<=MonitorAction){DrawLauncherControl(*item,app->dpi,app->launch,app->hot==static_cast<int>(item->CtlID)||(item->itemState&ODS_SELECTED),*app->text);return TRUE;}
        if(item->CtlID==StopControl) {
            const int id=static_cast<int>(item->CtlID);
            DrawStopControl(item->hDC,item->rcItem,app->dpi,*app->text,!app->view.compact,app->stopArmed,
                app->hot==id,!app->stopEnabled,(item->itemState&ODS_FOCUS)!=0,(item->itemState&ODS_SELECTED)!=0);
            return TRUE;
        }
        bool selected=item->CtlID==Pin?app->view.topmost:item->CtlID==Compact?app->view.compact:false;
        DrawControl(item->hDC,item->rcItem,app->dpi,static_cast<int>(item->CtlID),selected,
            app->hot==static_cast<int>(item->CtlID)||(item->itemState&ODS_SELECTED),(item->itemState&ODS_FOCUS)!=0);
        return TRUE;
    }
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window,&p);
        RECT r{};GetClientRect(window,&r);const int edge=app->Px(7);
        if(!IsZoomed(window)) {
            bool left=p.x<edge,right=p.x>=r.right-edge,top=p.y<edge,bottom=p.y>=r.bottom-edge;
            if(top && left)return HTTOPLEFT;if(top && right)return HTTOPRIGHT;
            if(bottom && left)return HTBOTTOMLEFT;if(bottom && right)return HTBOTTOMRIGHT;
            if(left)return HTLEFT;if(right)return HTRIGHT;if(top)return HTTOP;if(bottom)return HTBOTTOM;
        }
        const int reserve=(!app->launcher && app->view.compact)?HeaderStop:HeaderCluster;
        if(p.y<app->Px(55) && p.x<r.right-app->Px(reserve))return HTCAPTION;
        return HTCLIENT;
    }
    case WM_NCLBUTTONDBLCLK:if(wp==HTCAPTION){app->CommandId(ToggleCompact);return 0;}break;
    case WM_CONTEXTMENU: {
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if(p.x==-1 && p.y==-1){RECT r{};GetWindowRect(window,&r);p={r.right-app->Px(20),r.top+app->Px(50)};}
        app->Popup(p);return 0;
    }
    case WM_COMMAND:app->CommandId(LOWORD(wp));return 0;
    case WM_METRICS: {
        if(app->client) {
            ViewState next;next.compact=app->view.compact;next.packet=app->client->Snapshot();
            bool repaint=false;
            if(IsWindowVisible(window)) {
                RECT r{};GetClientRect(window,&r);
                repaint=!SameDisplay(app->view,next,r.right*96.0f/app->dpi,r.bottom*96.0f/app->dpi);
            }
            app->view.packet=std::move(next.packet); // Never discard a fresh snapshot.
            if(repaint && !app->launcher)InvalidateRect(window,nullptr,FALSE);
        }
        app->UpdateLifecycle(true);return 0;
    }
    case WM_SERVICE:app->UpdateLifecycle();return 0;
    case WM_SERVICE_BROWSER:app->pendingBrowser=true;app->UpdateLifecycle();return 0;
    case WM_TRAY: {
        UINT event=LOWORD(lp);
        if(event==NIN_SELECT || event==NIN_KEYSELECT)app->Reveal();
        else if(event==WM_CONTEXTMENU){POINT p{};GetCursorPos(&p);app->Popup(p);}
        return 0;
    }
    case WM_RESTORE:if(wp)app->CommandId(LauncherPage);app->Reveal();return 0;
    case WM_DPICHANGED: {
        app->dpi=HIWORD(wp);auto* r=reinterpret_cast<RECT*>(lp);
        SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        app->Layout();InvalidateRect(window,nullptr,FALSE);return 0;
    }
    case WM_EXITSIZEMOVE:app->Clamp();app->Save();return 0;
    case WM_SIZE:
        if(wp==SIZE_MINIMIZED)app->HideWindow();
        else {app->Layout();app->RememberSize();InvalidateRect(window,nullptr,FALSE);}
        return 0;
    case WM_CLOSE:app->HideWindow();return 0;
    case WM_QUERYENDSESSION:app->Save();return TRUE;
    case WM_ENDSESSION:if(wp){app->quitting=true;DestroyWindow(window);}return 0;
    case WM_DESTROY:
        app->Save();if(app->service)app->service->BeginDetach();if(app->client)app->client->Stop();
        if(app->tray){auto n=app->TrayData();Shell_NotifyIconW(NIM_DELETE,&n);app->tray=false;}
        PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,msg,wp,lp);
}
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    using namespace strata;
    App app;
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv)return 1;
    for(int i=1;i<argc;++i)if(wcscmp(argv[i],L"--no-settings")==0)app.settings.persist=false;
    app.settings.Load();
    app.launcher=true;app.manualLauncher=true;   // 0.5: Monitor always opens on the launcher page
    bool valid=true;
    for(int i=1;i<argc;++i) {
        if(wcscmp(argv[i],L"--no-settings")==0)continue;
        if(wcscmp(argv[i],L"--launcher")==0){app.launcher=true;app.manualLauncher=true;continue;}
        if(wcscmp(argv[i],L"--compact")==0){app.settings.compact=true;continue;}
        if(wcscmp(argv[i],L"--port")==0 && i+1<argc) {
            wchar_t* end=nullptr;unsigned long port=wcstoul(argv[++i],&end,10);
            if(!*argv[i] || *end || port==0 || port>65535){valid=false;break;}
            app.settings.port=static_cast<unsigned>(port);continue;
        }
        valid=false;break;
    }
    LocalFree(argv);
    if(!valid){MessageBoxW(nullptr,L"用法：StrataMonitor.exe [--port 517] [--compact] [--launcher] [--no-settings]",L"Strata Monitor",MB_OK);return 2;}
    std::wstring className=L"StrataMonitorWindow-v1-"+std::to_wstring(app.settings.port);
    std::wstring lockName=std::wstring(L"Local")+wchar_t(92)+className;
    HANDLE singleton=CreateMutexW(nullptr,FALSE,lockName.c_str());
    if(!singleton)return 3;
    if(GetLastError()==ERROR_ALREADY_EXISTS) {
        HWND other=FindWindowW(className.c_str(),nullptr);
        if(other)PostMessageW(other,WM_RESTORE,app.launcher?1:0,0);
        CloseHandle(singleton);return 0;
    }
    Gdiplus::GdiplusStartupInput gdiplusInput;ULONG_PTR gdiplusToken=0;
    if(Gdiplus::GdiplusStartup(&gdiplusToken,&gdiplusInput,nullptr)!=Gdiplus::Ok){CloseHandle(singleton);return 4;}
    try {app.text=std::make_unique<TextResources>();}
    catch(...) {Gdiplus::GdiplusShutdown(gdiplusToken);CloseHandle(singleton);return 8;}
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    app.view.compact=app.settings.compact;app.view.topmost=app.settings.topmost;
    app.view.interval=app.settings.interval;app.view.port=app.settings.port;
    wchar_t program[32768]{};GetModuleFileNameW(nullptr,program,32768);
    app.spec=ServiceSpec::ForRoot(std::filesystem::path(program).parent_path().parent_path(),app.settings.port);
    app.launch.preset=app.settings.preset;
    app.launch.summary[0]=PresetSummary(app.spec,0);app.launch.summary[1]=PresetSummary(app.spec,1);
    app.dpi=GetDpiForSystem();app.icon=MakeIcon();
    app.taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=WindowProc;wc.hInstance=instance;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=app.icon;wc.hIconSm=app.icon;
    wc.lpszClassName=className.c_str();RegisterClassExW(&wc);
    int x=app.launcher?app.settings.launchX:app.settings.x,y=app.launcher?app.settings.launchY:app.settings.y;
    if(x==-32000 || y==-32000) {
        RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        x=work.right-app.Px((app.launcher?app.settings.launchWidth:app.view.compact?app.settings.miniWidth:app.settings.width)+32);y=work.top+app.Px(48);
    }
    HWND window=CreateWindowExW(WS_EX_APPWINDOW|(app.view.topmost?WS_EX_TOPMOST:0),className.c_str(),L"Strata Monitor",
        WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_CLIPCHILDREN,x,y,app.Px(app.launcher?app.settings.launchWidth:app.view.compact?app.settings.miniWidth:app.settings.width),
        app.Px(app.launcher?app.settings.launchHeight:app.view.compact?app.settings.miniHeight:app.settings.height),nullptr,nullptr,instance,&app);
    int result=0;
    if(!window)result=5;
    else {
        app.dpi=GetDpiForWindow(window);app.Resize();

        BOOL dark=FALSE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
        DWORD rounded=2;DwmSetWindowAttribute(window,33,&rounded,sizeof(rounded));
        app.AddTray();
        try {
            app.service=std::make_unique<Service>(window,app.spec);app.service->Recover();app.UpdateLifecycle();
            app.client=std::make_unique<Client>(window,app.settings.port);
            app.client->Configure(app.view.interval,false);app.client->Start();
        } catch(...) {
            MessageBoxW(window,L"无法启动监控工作线程。",L"Strata Monitor",MB_OK|MB_ICONERROR);
            DestroyWindow(window);result=6;
        }
        if(!result) {
            ShowWindow(window,show==SW_SHOWMINIMIZED?SW_SHOWNORMAL:show);UpdateWindow(window);

            ACCEL keys[]={
                {FVIRTKEY|FCONTROL,'T',ToggleTop},{FVIRTKEY|FCONTROL,'M',ToggleCompact},
                {FVIRTKEY|FCONTROL,'Q',Quit},{FVIRTKEY,VK_ESCAPE,ToTray},{FVIRTKEY,VK_F5,RefreshNow}};
            HACCEL accelerators=CreateAcceleratorTableW(keys,static_cast<int>(std::size(keys)));
            MSG message{};BOOL got=0;
            while((got=GetMessageW(&message,nullptr,0,0))>0) {
                if(!TranslateAcceleratorW(window,accelerators,&message) && !IsDialogMessageW(window,&message)) {
                    TranslateMessage(&message);DispatchMessageW(&message);
                }
            }
            if(got==-1)result=7;
            if(accelerators)DestroyAcceleratorTable(accelerators);
        }
    }
    app.service.reset();app.client.reset();app.buffer.Clear();app.text.reset();
    if(app.icon)DestroyIcon(app.icon);
    UnregisterClassW(className.c_str(),instance);
    Gdiplus::GdiplusShutdown(gdiplusToken);CloseHandle(singleton);
    return result;
}
