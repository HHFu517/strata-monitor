#include "launcher.hpp"
#include <algorithm>
namespace strata {
using namespace Gdiplus;
namespace {
const Color Canvas(255,246,248,252),Surface(255,255,255,255),Ink(255,29,43,64),Muted(255,83,99,121),Edge(255,218,226,237),Teal(255,20,119,104),Tint(255,225,244,237);
void Round(Graphics& g,RectF r,Color fill,Color edge) {
    GraphicsPath p;float d=16;
    p.AddArc(r.X,r.Y,d,d,180,90);p.AddArc(r.GetRight()-d,r.Y,d,d,270,90);
    p.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);p.AddArc(r.X,r.GetBottom()-d,d,d,90,90);p.CloseFigure();
    SolidBrush b(fill);Pen pen(edge,1);g.FillPath(&b,&p);g.DrawPath(&pen,&p);
}
void Text(Graphics& g,TextResources& r,const std::wstring& text,RectF box,Color color=Ink,bool bold=false,float size=16,bool paragraph=false,StringAlignment align=StringAlignmentNear) {
    SolidBrush b(color);g.DrawString(text.c_str(),static_cast<INT>(text.size()),r.LabelFont(size,bold),box,paragraph?r.ParagraphFormat():r.LineFormat(align),&b);
}
}
LauncherState LaunchState(const ServiceInfo& i,const Packet& p,int preset,const std::wstring& xxs,const std::wstring& s) {
    LauncherState v;v.preset=i.owned?i.preset:preset;v.summary[0]=xxs;v.summary[1]=s;v.log=i.log;v.owned=i.owned;
    v.ready=ModelReady(p) && (!i.owned || i.phase==ServicePhase::Active) && !(i.foreign && i.phase==ServicePhase::Failed);
    if(i.owned) {
        v.selectable=false;v.action=L"停止服务";
        if(i.phase==ServicePhase::Stopping){v.title=L"正在停止服务";v.detail=L"正在等待正常清理并释放模型资源，请勿重复启动。";v.action=L"正在停止…";v.actionEnabled=false;v.ready=false;}
        else if(v.ready){v.title=L"服务运行中";v.detail=L"由启动器托管，可返回监控；停止服务会清空本次模型缓存。";}
        else {v.title=i.phase==ServicePhase::Starting?L"正在启动后台服务":L"正在加载模型";v.detail=L"正在读取权重并初始化引擎。就绪后自动进入监控，可随时请求停止。";}
    } else if(i.phase==ServicePhase::Stopping) {
        v.title=L"正在停止本机 Strata";v.detail=L"正在尝试正常退出或释放模型，必要时定向清理已验证的进程。";v.action=L"正在停止…";v.actionEnabled=false;v.selectable=false;v.ready=false;
    } else if(v.ready) {
        v.title=L"已连接外部实例";v.detail=L"此服务从终端启动；这里只读取监控数据，但停止会结束本机已验证的 Strata 进程。";v.action=L"进入监控";v.selectable=false;
    } else if(i.phase==ServicePhase::Failed) {
        v.selectable=!i.foreign;v.title=i.foreign?L"停止未完成":L"启动未完成";
        v.detail=i.error.empty()?(i.foreign?L"仍有 Strata 进程未退出，请查看 monitor/logs/stop.log。":L"后台已退出，请查看启动日志后重试。"):i.error.substr(0,320);
        v.action=i.foreign?L"再次尝试停止":L"重新启动";
    } else if(p.connected) {
        v.title=L"服务尚未就绪";v.detail=L"本机服务已响应，但模型尚未加载。不会另启第二个服务。";v.action=L"等待模型就绪";v.actionEnabled=false;v.selectable=false;
    }
    return v;
}
void RenderLauncher(Graphics& g,const LauncherState& v,float w,float h,TextResources& r) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);g.Clear(Canvas);
    const float x=24,width=w-48;
    Text(g,r,L"Strata",RectF(x,12,160,34),Ink,true);
    Text(g,r,L"启动 Strata",RectF(x,72,width,42),Ink,true,24);
    Text(g,r,L"Qwen3.8_FlashNext · 本机服务",RectF(x,118,width,28),Muted);
    Text(g,r,v.title,RectF(x,340,width-(v.ready && v.owned?144:0),32),v.owned?Teal:Ink,true);
    Text(g,r,v.detail,RectF(x,382,width,(std::max)(40.0f,h-514)),Muted,false,16,true);
    Text(g,r,L"× 只收起到托盘，后台继续运行。",RectF(x,h-48,width,28),Muted);
}
void DrawLauncherControl(const DRAWITEMSTRUCT& item,unsigned dpi,const LauncherState& v,bool hot,TextResources& r) {
    Graphics g(item.hDC);g.ScaleTransform(dpi/96.0f,dpi/96.0f);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    const float w=(item.rcItem.right-item.rcItem.left)*96.0f/dpi,h=(item.rcItem.bottom-item.rcItem.top)*96.0f/dpi;
    const int id=static_cast<int>(item.CtlID);const bool disabled=(item.itemState&ODS_DISABLED)!=0;
    g.Clear(Canvas);Color edge=Edge,fill=Surface,ink=Ink;
    bool preset=id==PresetXXS || id==PresetS;
    if(preset) {
        bool chosen=v.preset==(id==PresetS?1:0);fill=chosen?Tint:Surface;edge=chosen?Teal:Edge;
        if(hot && !disabled)edge=Teal;Round(g,RectF(.5f,.5f,w-1,h-1),fill,edge);
        Text(g,r,id==PresetS?L"IQ3_S":L"IQ3_XXS",RectF(16,8,w-56,28),Ink,true);
        Text(g,r,v.summary[id==PresetS?1:0],RectF(16,38,w-32,26),Muted);
        Pen pen(chosen?Teal:Muted,1.6f);g.DrawEllipse(&pen,w-34,16.0f,16.0f,16.0f);
        if(chosen){SolidBrush b(Teal);g.FillEllipse(&b,w-30,20.0f,8.0f,8.0f);}
    } else {
        const bool primary=id==ServiceAction;
        fill=primary?(disabled?Edge:Teal):Surface;ink=primary && !disabled?Surface:Muted;
        if(hot && !disabled)fill=primary?Color(255,15,99,86):Tint;
        Round(g,RectF(.5f,.5f,w-1,h-1),fill,primary?fill:Edge);
        std::wstring label=id==ServiceAction?v.action:id==LogAction?L"查看日志":L"返回监控";
        Text(g,r,label,RectF(8,0,w-16,h),ink,primary,16,false,StringAlignmentCenter);
    }
    if(item.itemState&ODS_FOCUS){Pen p(Teal,1.5f);p.SetDashStyle(DashStyleDot);g.DrawRectangle(&p,4.0f,4.0f,w-8,h-8);}
}
}
