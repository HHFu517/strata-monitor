#pragma once
#include "view.hpp"
#include "service.hpp"
namespace strata {
constexpr int LaunchWidth=520,LaunchHeight=600,MinLaunchWidth=420,MinLaunchHeight=560;
enum LauncherControl { PresetXXS=301,PresetS,ServiceAction,LogAction,MonitorAction };
struct LauncherState {
    int preset=0;
    bool selectable=true,actionEnabled=true,ready=false,owned=false;
    std::wstring summary[2],title=L"准备启动",detail=L"选择模型后启动。服务将在后台运行，不显示终端窗口。";
    std::wstring action=L"启动服务",log;
    bool operator==(const LauncherState& b)const {
        return preset==b.preset && selectable==b.selectable && actionEnabled==b.actionEnabled && ready==b.ready && owned==b.owned &&
            summary[0]==b.summary[0] && summary[1]==b.summary[1] && title==b.title && detail==b.detail && action==b.action && log==b.log;
    }
};
inline bool ModelReady(const Packet& p) {
    if(!p.connected || p.metrics.model.empty() || !p.metrics.context)return false;
    const auto stage=StageOf(p.metrics);
    return stage!=Stage::Loading && stage!=Stage::Unloaded && stage!=Stage::Unknown;
}
LauncherState LaunchState(const ServiceInfo&,const Packet&,int preset,const std::wstring& xxs,const std::wstring& s);
void RenderLauncher(Gdiplus::Graphics&,const LauncherState&,float width,float height,TextResources&);
void DrawLauncherControl(const DRAWITEMSTRUCT&,unsigned dpi,const LauncherState&,bool hot,TextResources&);
}
