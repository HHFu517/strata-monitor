#include "view.hpp"
#include "timeline.hpp"
#include <algorithm>
#include <cwchar>
#include <cassert>
#pragma comment(lib,"gdiplus.lib")
namespace strata {
using namespace Gdiplus;
namespace {
const Color Canvas(255,246,248,252), Surface(255,255,255,255), Edge(255,218,226,237);
const Color Ink(255,29,43,64), Muted(255,83,99,121), Dim(255,99,114,135);
const Color Blue(255,48,98,174), Violet(255,109,80,164), Teal(255,20,119,104), Amber(255,149,99,20);
void Label(Graphics& g,TextResources& resources,const std::wstring& text,float x,float y,float w,float h,float size=LabelSize,
    Color color=Ink,bool bold=false,StringAlignment align=StringAlignmentNear) {
    if(w<=0 || h<=0)return;
    SolidBrush brush(color);
    g.DrawString(text.c_str(),static_cast<INT>(text.size()),resources.LabelFont(size,bold),RectF(x,y,w,h),resources.LineFormat(align),&brush);
}
void NumberText(Graphics& g,TextResources& resources,const std::wstring& text,float x,float y,float w,float h,float size,Color color,
    StringAlignment align=StringAlignmentNear) {
    if(w<=0 || h<=0)return;
    SolidBrush brush(color);
    g.DrawString(text.c_str(),-1,resources.NumberFont(size),RectF(x,y,w,h),resources.LineFormat(align),&brush);
}
void Line(Graphics& g,float x,float y,float x2,float y2,Color color=Edge,float width=1) {
    Pen pen(color,width);pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);g.DrawLine(&pen,x,y,x2,y2);
}
void Round(Graphics& g,RectF b,float radius,Color color) {
    if(b.Width<=0 || b.Height<=0)return;
    GraphicsPath p;float d=radius*2;
    p.AddArc(b.X,b.Y,d,d,180,90);p.AddArc(b.GetRight()-d,b.Y,d,d,270,90);
    p.AddArc(b.GetRight()-d,b.GetBottom()-d,d,d,0,90);p.AddArc(b.X,b.GetBottom()-d,d,d,90,90);
    p.CloseFigure();SolidBrush brush(color);g.FillPath(&brush,&p);
}
void Dot(Graphics& g,float x,float y,Color color,float radius=3) {
    SolidBrush brush(color);g.FillEllipse(&brush,x-radius,y-radius,radius*2,radius*2);
}
float BarLength(float width,Number used,Number total) {
    auto percent=Percent(used,total);
    return percent && *percent>0?(std::max)(4.0f,width*static_cast<float>(*percent)/100):0;
}
void Bar(Graphics& g,float x,float y,float width,Number used,Number total,Color color) {
    Round(g,RectF(x,y,width,4),2,Edge);const float filled=BarLength(width,used,total);
    if(filled>0)Round(g,RectF(x,y,filled,4),2,color);
}
Color StateColor(const ViewState& s) {
    if(!s.packet.connected)return Amber;
    switch(StageOf(s.packet.metrics)) {
    case Stage::Reading:case Stage::Loading:return Amber;
    case Stage::Thinking:return Violet;
    case Stage::Writing:case Stage::ToolComplete:case Stage::Idle:return Teal;
    default:return Blue;
    }
}
Color StateTint(const ViewState& s) {
    auto stage=StageOf(s.packet.metrics);
    if(!s.packet.connected || stage==Stage::Reading || stage==Stage::Loading)return Color(255,252,240,214);
    if(stage==Stage::Thinking)return Color(255,239,232,250);
    if(stage==Stage::Writing || stage==Stage::ToolComplete || stage==Stage::Idle)return Color(255,225,244,237);
    return Color(255,227,236,251);
}
void Status(Graphics& g,TextResources& resources,const ViewState& s,float x,float y) {
    Round(g,RectF(x,y,164,32),6,StateTint(s));Dot(g,x+11,y+16,StateColor(s),2.5f);
    Label(g,resources,StateLabel(s),x+18,y,144,32,LabelSize,StateColor(s),true);
}
void Trace(Graphics& g,const HistoryTrace& values,double top,Color color,Box b) {
    PointF prev,last;bool was=false,any=false;
    for(size_t i=0;i<HistorySlots;++i) {
        if(!values[i]){was=false;continue;}
        PointF point(b.x+b.w*HistoryX(i),b.Bottom()-3-static_cast<float>(*values[i]/top)*(b.h-6));
        if(was)Line(g,prev.X,prev.Y,point.X,point.Y,color,2.1f);
        prev=last=point;was=any=true;
    }
    if(any)Dot(g,last.X,last.Y,color,2.7f);
}
void Chart(Graphics& g,TextResources& resources,const Metrics& m,Box b) {
    if(b.h<30 || b.w<=0)return;
    const auto t=Timeline(m);
    auto any=[](const HistoryTrace& a){return std::any_of(a.begin(),a.end(),[](Number n){return n.has_value();});};
    const bool decode=any(t.decode),prefill=any(t.prefill);
    Label(g,resources,decode?Rate(t.decodeTop,true,true):L"—",b.x,b.y,b.w/2,22,LabelSize,Blue);
    Label(g,resources,prefill?Rate(t.prefillTop,true,true):L"—",b.x+b.w/2,b.y,b.w/2,22,LabelSize,Amber,false,StringAlignmentFar);
    Box plot{b.x,b.y+24,b.w,b.h-24};
    Line(g,plot.x,plot.Bottom(),plot.x+plot.w,plot.Bottom());
    Line(g,plot.x,plot.y+plot.h*.5f,plot.x+plot.w,plot.y+plot.h*.5f,Color(255,230,235,243));
    Trace(g,t.prefill,t.prefillTop,Amber,plot);Trace(g,t.decode,t.decodeTop,Blue,plot);
    if(!decode && !prefill && plot.h>=26)Label(g,resources,L"No speed samples",plot.x,plot.y,plot.w,plot.h,LabelSize,Muted);
}
void Error(Graphics& g,TextResources& resources,const ViewState& s,Box b) {
    SolidBrush brush(Muted);
    g.DrawString(s.packet.error.c_str(),-1,resources.LabelFont(LabelSize,false),RectF(b.x,b.y,b.w,b.h),resources.ParagraphFormat(),&brush);
}
void Inference(Graphics& g,TextResources& resources,const ViewState& s,Box b) {
    const auto& m=s.packet.metrics;const float col=(b.w-24)/2,right=b.x+col+24;
    const bool active=s.packet.connected && (IsReading(m) || IsDecoding(m));
    Label(g,resources,L"Decode · tok/s",b.x,b.y,col,24,LabelSize,Blue,true);
    Label(g,resources,L"Prefill · tok/s",right,b.y,col,24,LabelSize,Amber,true,StringAlignmentFar);
    NumberText(g,resources,Rate(s.packet.connected && IsDecoding(m)?m.decode:Number{}),b.x-2,b.y+20,col+2,66,RateSize,Blue);
    NumberText(g,resources,Rate(active?m.prefill:Number{},true),right,b.y+20,col,66,RateSize,Amber,StringAlignmentFar);
    Box chart{b.x,b.y+90,b.w,b.h-90};
    if(s.packet.connected)Chart(g,resources,m,chart);else Error(g,resources,s,chart);
}
void Usage(Graphics& g,TextResources& resources,const ViewState& s,Box b,bool memory) {
    const auto& m=s.packet.metrics;const Color color=memory?Violet:Blue;
    const Number used=memory?m.ram:m.contextUsed,total=memory?m.ramTotal:m.context;
    const float p=12;Round(g,RectF(b.x,b.y,b.w,b.h),12,Surface);
    Label(g,resources,memory?L"系统内存":L"Context fill",b.x+p,b.y+5,b.w-p*2,24,LabelSize,Muted,true);
    NumberText(g,resources,Fixed(Percent(used,total),memory?0:1)+L"%",b.x+p,b.y+27,b.w-p*2,51,MetricSize,color);
    Bar(g,b.x+p,b.Bottom()-9,b.w-p*2,used,total,color);
}
void Gpu(Graphics& g,TextResources& resources,const ViewState& s,Box b) {
    const auto& m=s.packet.metrics;const float p=12,w=b.w-24;
    Round(g,RectF(b.x,b.y,b.w,b.h),12,Surface);
    if(b.h>=200 && b.w<420) {
        const float row=(b.h-32)/2;
        for(int i=0;i<2;++i) {
            const float y=b.y+8+i*row;
            Label(g,resources,i?L"VRAM":L"GPU load",b.x+p,y,w,24,LabelSize,Muted,true);
            NumberText(g,resources,Fixed(i?Percent(m.vram,m.vramTotal):m.gpu)+L"%",b.x+p,y+21,w,51,MetricSize,i?Blue:Teal);
            Bar(g,b.x+p,b.y+(i+1)*row-4,w,i?m.vram:m.gpu,i?m.vramTotal:Number(100),i?Blue:Teal);
        }
    } else {
        const float col=(w-16)/2,x=b.x+p,right=x+col+16,y=b.y+(b.h<120?5.0f:11.0f);
        Label(g,resources,L"GPU load",x,y,col,24,LabelSize,Muted,true);Label(g,resources,L"VRAM",right,y,col,24,LabelSize,Muted,true);
        NumberText(g,resources,Fixed(m.gpu)+L"%",x,y+21,col,51,MetricSize,Teal);
        NumberText(g,resources,Fixed(Percent(m.vram,m.vramTotal))+L"%",right,y+21,col,51,MetricSize,Blue);
        Bar(g,x,b.Bottom()-35,col,m.gpu,100.0,Teal);Bar(g,right,b.Bottom()-35,col,m.vram,m.vramTotal,Blue);
    }
    Label(g,resources,L"温度  "+Fixed(m.temp)+L" °C",b.x+p,b.Bottom()-27,w/2,24,LabelSize,Amber);
    Label(g,resources,L"功耗  "+Fixed(m.power)+L" W",b.x+p+w/2,b.Bottom()-27,w/2,24,LabelSize,Ink,false,StringAlignmentFar);
}
void Host(Graphics& g,TextResources& resources,const ViewState& s,Box b) {
    const auto& m=s.packet.metrics;const float half=(b.w-16)/2;
    const std::wstring disk=Fixed(m.disk,m.disk&&*m.disk<10?1:0)+L" MB/s";
    if(b.h<46) {
        Label(g,resources,L"CPU  "+Fixed(m.cpu)+L"%",b.x,b.y,half,b.h,LabelSize,Blue);
        Label(g,resources,L"读取  "+disk,b.x+half+16,b.y,half,b.h,LabelSize,Muted,false,StringAlignmentFar);
    } else {
        Label(g,resources,L"CPU",b.x,b.y,half,22,LabelSize,Muted);Label(g,resources,L"磁盘读取",b.x+half+16,b.y,half,22,LabelSize,Muted);
        NumberText(g,resources,Fixed(m.cpu)+L"%",b.x,b.y+18,half,30,20,Blue);
        NumberText(g,resources,disk,b.x+half+16,b.y+18,half,30,20,Ink);
    }
}
}
std::wstring Rate(Number value,bool prefill,bool compact) {
    if(!value)return L"—";
    if(*value>=1e6 || (compact && *value>=999500))return Fixed(*value/1e6,compact && *value/1e6>=9.95?0:1)+L"M";
    // Plain digits, no thousands abbreviation: 2001 rather than 2.0k. Whole numbers from 1000 up,
    // so the narrowest compact column cannot overflow.
    return Fixed(value,prefill || (compact && *value>=100) || *value>=1000?0:1);
}
TextResources::TextResources():
    label_(L"Microsoft YaHei UI",LabelSize,FontStyleRegular,UnitPixel),
    labelBold_(L"Microsoft YaHei UI",LabelSize,FontStyleBold,UnitPixel),
    title_(L"Microsoft YaHei UI",24,FontStyleBold,UnitPixel),
    metric_(L"Segoe UI",MetricSize,FontStyleRegular,UnitPixel),
    rate_(L"Segoe UI",RateSize,FontStyleRegular,UnitPixel),
    miniRate_(L"Segoe UI",26,FontStyleRegular,UnitPixel),
    host_(L"Segoe UI",20,FontStyleRegular,UnitPixel) {
    for(int i=0;i<3;++i) {
        lines_[i].SetAlignment(static_cast<StringAlignment>(i));
        lines_[i].SetLineAlignment(StringAlignmentCenter);
        lines_[i].SetTrimming(StringTrimmingEllipsisCharacter);
        lines_[i].SetFormatFlags(StringFormatFlagsNoWrap);
    }
    paragraph_.SetTrimming(StringTrimmingEllipsisWord);
}
const Font* TextResources::LabelFont(float size,bool bold)const {
    assert(size==LabelSize || (size==24 && bold));
    return size==24?&title_:bold?&labelBold_:&label_;
}
const Font* TextResources::NumberFont(float size)const {
    assert(size==MetricSize || size==RateSize || size==26 || size==20);
    return size==MetricSize?&metric_:size==RateSize?&rate_:size==26?&miniRate_:&host_;
}
const StringFormat* TextResources::LineFormat(StringAlignment align)const {
    assert(align>=StringAlignmentNear && align<=StringAlignmentFar);
    return &lines_[static_cast<int>(align)];
}
std::wstring Wide(const std::string& s) {
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n)return L"无效文本";
    std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);return w;
}
std::wstring Fixed(Number value,int precision) {
    if(!value)return L"—";wchar_t buf[64];
    if(*value>=1e8)swprintf_s(buf,L"%.1e",*value);else swprintf_s(buf,L"%.*f",precision,*value);
    return buf;
}
std::wstring StateLabel(const ViewState& s) {
    if(!s.packet.connected)return s.packet.error==L"正在连接本机服务"?L"Connecting":L"Disconnected";
    switch(StageOf(s.packet.metrics)) {
    case Stage::Reading:return L"Reading prompt";
    case Stage::Thinking:return L"Thinking";
    case Stage::Answering:return L"Answering";
    case Stage::Writing:return L"Tool calling";
    case Stage::ToolComplete:return L"Tool complete";
    case Stage::Generating:return L"Generating";
    case Stage::Idle:return L"Idle";
    case Stage::Loading:return L"Loading";
    case Stage::Unloaded:return L"Not loaded";
    default:return L"Unknown";
    }
}
std::wstring TrayText(const ViewState& s) {
    std::wstring tip=L"Strata Monitor · "+StateLabel(s);const auto& m=s.packet.metrics;
    if(s.packet.connected) {
        if(IsDecoding(m))tip+=L" "+Fixed(m.decode,1)+L" tok/s";
        else if(IsReading(m))tip+=L" "+Fixed(m.prefill,1)+L" tok/s";
        if(m.gpu)tip+=L" · GPU "+Fixed(m.gpu)+L"%";
    }
    if(tip.size()>127)tip.resize(127);return tip;
}
bool SameDisplay(const ViewState& a,const ViewState& b,float width,float height) {
    if(a.compact!=b.compact || a.packet.connected!=b.packet.connected || StateLabel(a)!=StateLabel(b) ||
        StateColor(a).GetValue()!=StateColor(b).GetValue() || StateTint(a).GetValue()!=StateTint(b).GetValue())return false;
    const auto& x=a.packet.metrics;const auto& y=b.packet.metrics;const bool compact=a.compact;
    auto decode=[&](const ViewState& v){return Rate(v.packet.connected && IsDecoding(v.packet.metrics)?v.packet.metrics.decode:Number{},false,compact);};
    auto prefill=[&](const ViewState& v){const auto& m=v.packet.metrics;return Rate(v.packet.connected && (IsReading(m)||IsDecoding(m))?m.prefill:Number{},true,compact);};
    if(decode(a)!=decode(b) || prefill(a)!=prefill(b))return false;
    auto capacity=[&](Number used,Number total,Number used2,Number total2,int precision,float barWidth) {
        return Fixed(Percent(used,total),precision)==Fixed(Percent(used2,total2),precision) &&
            BarLength(barWidth,used,total)==BarLength(barWidth,used2,total2);
    };
    auto history=[&](Box chart) {
        if(!a.packet.connected || chart.h<30 || chart.w<=0)return true;
        const auto p=Timeline(x),q=Timeline(y);
        auto trace=[&](const HistoryTrace& u,double ut,const HistoryTrace& v,double vt) {
            bool any=false;
            for(size_t i=0;i<HistorySlots;++i) {
                if(u[i].has_value()!=v[i].has_value())return false;
                if(u[i]) {
                    any=true;
                    if(static_cast<float>(*u[i]/ut)*((chart.h-24)-6)!=static_cast<float>(*v[i]/vt)*((chart.h-24)-6))return false;
                }
            }
            return !any || Rate(ut,true,true)==Rate(vt,true,true);
        };
        return trace(p.decode,p.decodeTop,q.decode,q.decodeTop) && trace(p.prefill,p.prefillTop,q.prefill,q.prefillTop);
    };
    const float margin=width<400?18.0f:20.0f;
    if(compact) {
        const float half=(width-margin*2-16)/2;
        if(!capacity(x.contextUsed,x.context,y.contextUsed,y.context,0,half) ||
            !capacity(x.ram,x.ramTotal,y.ram,y.ramTotal,0,half))return false;
        if(height>=156 && Fixed(x.gpu)!=Fixed(y.gpu))return false;
        return height<230 || history(Box{margin,172,width-margin*2,height-190});
    }
    auto name=[](const Metrics& m){return Starts(m.phase,"writing a tool call: ")?Wide(m.phase.substr(21)):Wide(m.model);};
    if(name(x)!=name(y) || (!a.packet.connected && a.packet.error!=b.packet.error))return false;
    const auto l=Arrange(width,height);
    if(!capacity(x.contextUsed,x.context,y.contextUsed,y.context,1,l.context.w-24) ||
        !capacity(x.ram,x.ramTotal,y.ram,y.ramTotal,0,l.memory.w-24))return false;
    const float gpuInner=l.gpu.w-24,gpuWidth=l.gpu.h>=200 && l.gpu.w<420?gpuInner:(gpuInner-16)/2;
    if(Fixed(x.gpu)!=Fixed(y.gpu) ||
        !capacity(x.gpu,100.0,y.gpu,100.0,0,gpuWidth) ||
        !capacity(x.vram,x.vramTotal,y.vram,y.vramTotal,0,gpuWidth) ||
        Fixed(x.temp)!=Fixed(y.temp) || Fixed(x.power)!=Fixed(y.power) || Fixed(x.cpu)!=Fixed(y.cpu) ||
        Fixed(x.disk,x.disk && *x.disk<10?1:0)!=Fixed(y.disk,y.disk && *y.disk<10?1:0))return false;
    return history(Box{l.inference.x,l.inference.y+90,l.inference.w,l.inference.h-90});
}
void Render(Graphics& g,const ViewState& s,float width,float height,TextResources& resources) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);g.Clear(Canvas);
    const float m=width<400?18.0f:20.0f;const auto& data=s.packet.metrics;
    Line(g,m,29,m+5,29,Teal,2);Line(g,m+5,29,m+8,20,Teal,2);
    Line(g,m+8,20,m+13,37,Teal,2);Line(g,m+13,37,m+17,26,Teal,2);Line(g,m+17,26,m+22,26,Teal,2);
    Label(g,resources,L"Strata",m+31,10,112,36,24,Ink,true);
    if(s.compact) {
        Status(g,resources,s,m,60);
        const float start=m+176,col=(width-m-start-12)/2,right=start+col+12;
        const bool active=s.packet.connected && (IsReading(data) || IsDecoding(data));
        Label(g,resources,L"Decode",start,56,col,24,LabelSize,Blue,true);
        Label(g,resources,L"Prefill",right,56,col,24,LabelSize,Amber,true,StringAlignmentFar);
        NumberText(g,resources,Rate(s.packet.connected && IsDecoding(data)?data.decode:Number{},false,true),start,77,col,40,26,Blue);
        NumberText(g,resources,Rate(active?data.prefill:Number{},true,true),right,77,col,40,26,Amber,StringAlignmentFar);
        if(height>=156)Label(g,resources,L"GPU "+Fixed(data.gpu)+L"%",m,96,164,24,LabelSize,Teal);
        const float capacityY=(std::min)(126.0f,height-30),half=(width-m*2-16)/2,ramX=m+half+16;
        Label(g,resources,L"Context fill",m,capacityY,half-60,24,LabelSize,Muted);
        Label(g,resources,Fixed(Percent(data.contextUsed,data.context))+L"%",m+half-60,capacityY,60,24,LabelSize,Blue,true,StringAlignmentFar);
        Label(g,resources,L"系统内存",ramX,capacityY,half-60,24,LabelSize,Muted);
        Label(g,resources,Fixed(Percent(data.ram,data.ramTotal))+L"%",ramX+half-60,capacityY,60,24,LabelSize,Violet,true,StringAlignmentFar);
        Bar(g,m,capacityY+25,half,data.contextUsed,data.context,Blue);Bar(g,ramX,capacityY+25,half,data.ram,data.ramTotal,Violet);
        if(height>=230 && s.packet.connected)Chart(g,resources,data,Box{m,172,width-m*2,height-190});
    } else {
        Line(g,m,55,width-m,55);Status(g,resources,s,m,66);
        const std::wstring name=Starts(data.phase,"writing a tool call: ")?Wide(data.phase.substr(21)):Wide(data.model);
        Label(g,resources,name,m+176,65,width-m*2-176,34,LabelSize,Ink,true);
        const auto l=Arrange(width,height);
        Inference(g,resources,s,l.inference);Usage(g,resources,s,l.context,false);Usage(g,resources,s,l.memory,true);Gpu(g,resources,s,l.gpu);Host(g,resources,s,l.host);
    }
    Line(g,width-14,height-7,width-7,height-14,Dim);Line(g,width-9,height-7,width-7,height-9,Dim);
}
void DrawControl(HDC dc,const RECT& bounds,unsigned dpi,int id,bool selected,bool hot,bool focus) {
    Graphics g(dc);g.ScaleTransform(dpi/96.0f,dpi/96.0f);g.SetSmoothingMode(SmoothingModeAntiAlias);
    const float w=(bounds.right-bounds.left)*96.0f/dpi,h=(bounds.bottom-bounds.top)*96.0f/dpi;
    SolidBrush bg(Canvas);g.FillRectangle(&bg,0.0f,0.0f,w,h);
    if(hot || selected)Round(g,RectF(1,1,w-2,h-2),6,selected?Color(255,226,236,252):Color(255,232,237,245));
    auto color=selected?Blue:Muted;float x=(w-24)/2,y=(h-24)/2;
    auto ln=[&](float a,float b,float c,float d){Line(g,x+a,y+b,x+c,y+d,color,1.55f);};
    if(id==Pin) {
        ln(16,3,16,9);ln(8,3,8,9);ln(7,3,17,3);ln(8,9,5,12);ln(5,12,5,15);ln(5,15,19,15);
        ln(19,15,19,12);ln(19,12,16,9);ln(12,15,12,22);
    } else if(id==Compact) {
        if(selected){ln(15,3,21,3);ln(21,3,21,9);ln(21,3,14,10);ln(9,21,3,21);ln(3,21,3,15);ln(3,21,10,14);}
        else {ln(4,14,10,14);ln(10,14,10,20);ln(10,14,3,21);ln(20,10,14,10);ln(14,10,14,4);ln(14,10,21,3);}
    } else if(id==Hide){ln(6,6,18,18);ln(18,6,6,18);}
    else {Dot(g,x+5,y+12,color,1.2f);Dot(g,x+12,y+12,color,1.2f);Dot(g,x+19,y+12,color,1.2f);}
    if(focus){Pen pen(Blue,1);g.DrawRectangle(&pen,2.0f,2.0f,w-4,h-4);}
}
void DrawStopControl(HDC dc,const RECT& bounds,unsigned dpi,TextResources& resources,bool labeled,bool stopping,bool hot,bool disabled,bool focus,bool pressed) {
    Graphics g(dc);g.ScaleTransform(dpi/96.0f,dpi/96.0f);g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    const float w=(bounds.right-bounds.left)*96.0f/dpi,h=(bounds.bottom-bounds.top)*96.0f/dpi;
    // Clear fully, including edge pixels; antialiased FillRectangle can retain the native button rim.
    g.Clear(Canvas);
    // Quiet at rest like the header controls; destructive colour appears only on intent.
    auto fill=Canvas,ink=Muted;
    if(disabled)ink=Color(255,148,160,178);
    else if(stopping){fill=Color(255,252,240,214);ink=Amber;}
    else if(pressed){fill=Color(255,248,219,222);ink=Color(255,145,38,47);}
    else if(hot){fill=Color(255,253,236,237);ink=Color(255,166,49,57);}
    if(!disabled && (stopping || hot || pressed))Round(g,RectF(1,1,w-2,h-2),6,fill);
    // Compact 20 DIP Lucide power: one open ring, round caps, optically centred.
    const float x=labeled?14.0f:(w-20)/2,y=(h-20)/2;
    GraphicsPath ring;ring.AddArc(x+3.0f,y+4.0f,14.0f,14.0f,315.0f,270.0f);
    Pen stroke(ink,1.55f);stroke.SetStartCap(LineCapRound);stroke.SetEndCap(LineCapRound);g.DrawPath(&stroke,&ring);
    Line(g,x+10,y+1,x+10,y+8.5f,ink,1.55f);
    if(labeled && w>=120)Label(g,resources,stopping?L"正在停止":L"停止服务",40,4,w-52,24,LabelSize,ink,false);
    if(focus && !disabled) {
        const float left=1.5f,top=1.5f,right=w-1.5f,bottom=h-1.5f,d=12;
        GraphicsPath outline;
        outline.AddArc(left,top,d,d,180,90);outline.AddArc(right-d,top,d,d,270,90);
        outline.AddArc(right-d,bottom-d,d,d,0,90);outline.AddArc(left,bottom-d,d,d,90,90);outline.CloseFigure();
        Pen mark(Blue,2.0f);g.DrawPath(&mark,&outline);
    }
}
HICON MakeIcon() {
    Bitmap image(32,32,PixelFormat32bppARGB);Graphics g(&image);g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(0,0,0,0));Round(g,RectF(0,0,32,32),7,Edge);Round(g,RectF(1,1,30,30),6,Canvas);
    Line(g,4,17,9,17,Teal,2.2f);Line(g,9,17,13,7,Teal,2.2f);Line(g,13,7,18,25,Teal,2.2f);
    Line(g,18,25,22,14,Teal,2.2f);Line(g,22,14,28,14,Teal,2.2f);
    HICON icon=nullptr;image.GetHICON(&icon);return icon;
}
}
