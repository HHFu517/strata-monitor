#include "../src/view.hpp"
#include "../src/updates.hpp"
#include <cstdint>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>
using namespace strata;
using namespace Gdiplus;
int checks=0;
void Check(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
uint64_t Hash(const ViewState& v,float w,float h,TextResources& resources,float dpi=1) {
    Bitmap image(static_cast<INT>(w*dpi),static_cast<INT>(h*dpi),PixelFormat32bppARGB);
    {Graphics g(&image);g.ScaleTransform(dpi,dpi);Render(g,v,w,h,resources);}
    BitmapData data{};Rect r(0,0,image.GetWidth(),image.GetHeight());
    Check(image.LockBits(&r,ImageLockModeRead,PixelFormat32bppARGB,&data)==Ok,"bitmap readable");
    uint64_t hash=14695981039346656037ull;
    for(UINT y=0;y<image.GetHeight();++y)for(UINT x=0;x<image.GetWidth()*4;++x) {
        hash^=static_cast<BYTE*>(data.Scan0)[static_cast<ptrdiff_t>(y)*data.Stride+x];hash*=1099511628211ull;
    }
    image.UnlockBits(&data);return hash;
}
ViewState Fixture() {
    ViewState v;v.packet.connected=true;auto& m=v.packet.metrics;
    m.model="Qwen3.8_FlashNext";m.state="generating";m.phase="answering";m.decode=67.3;m.prefill=1240;
    m.context=256000;m.contextUsed=128000;m.ram=53.5;m.ramTotal=63.9;m.vram=22.8;m.vramTotal=24;
    m.gpu=97;m.temp=58;m.power=355;m.cpu=59;m.disk=1.25;
    for(int i=0;i<60;++i){m.speedHistory.push_back(55+i%8*2);m.prefillHistory.push_back(i<20?Number(900+i%6*60):Number(0));}
    return v;
}
void Resources(TextResources& r) {
    std::vector<const Font*> fonts{r.LabelFont(LabelSize,false),r.LabelFont(LabelSize,true),r.LabelFont(24,true),r.NumberFont(MetricSize),r.NumberFont(RateSize),r.NumberFont(26),r.NumberFont(20)};
    for(auto* font:fonts){Check(font->GetLastStatus()==Ok,"cached font valid");Check(font->GetUnit()==UnitPixel,"font units unchanged");}
    for(int i=0;i<100;++i)Check(r.LabelFont(LabelSize,false)==fonts[0] && r.NumberFont(RateSize)==fonts[4],"cache identity stable");
    for(int i=0;i<3;++i) {
        const auto align=static_cast<StringAlignment>(i);const auto* f=r.LineFormat(align);
        Check(f->GetAlignment()==align && f->GetLineAlignment()==StringAlignmentCenter,"alignment preserved");
        Check(f->GetTrimming()==StringTrimmingEllipsisCharacter && f->GetFormatFlags()==StringFormatFlagsNoWrap,"single-line format preserved");
        Check(f==r.LineFormat(align),"format identity stable");
    }
    const auto* f=r.ParagraphFormat();
    Check(f->GetTrimming()==StringTrimmingEllipsisWord && f->GetFormatFlags()==0 && f->GetLineAlignment()==StringAlignmentNear,"error paragraph format preserved");
    const DWORD gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int i=0;i<50;++i){TextResources temporary;Check(temporary.NumberFont(20)->GetLastStatus()==Ok,"cache lifecycle");}
    Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi+1,"cache destruction releases GDI resources");
}
void Tips() {
    TrayTipCache cache;int calls=0;
    auto ok=[&](const std::wstring&){++calls;return true;};auto fail=[&](const std::wstring&){++calls;return false;};
    Check(cache.Update(L"A",ok) && calls==1,"initial tray delivery");
    Check(!cache.Update(L"A",ok) && calls==1,"identical tip skipped");
    Check(!cache.Update(L"B",fail) && calls==2,"failed delivery not committed");
    Check(!cache.Update(L"B",fail) && calls==3,"failure retry remains possible");
    Check(cache.Update(L"B",ok) && calls==4,"retry succeeds");
    Check(!cache.Update(L"B",ok) && calls==4,"successful retry cached");
    cache.Reset();Check(cache.Update(L"B",ok) && calls==5,"tray recreation invalidates cache");
    auto a=Fixture(),b=a;b.packet.received=123;b.packet.metrics.cpu=40;
    Check(TrayText(a)==TrayText(b),"unshown tray fields ignored");
    b=a;b.packet.metrics.gpu=97.01;Check(TrayText(a)==TrayText(b),"tray GPU rounding");
    b=a;b.packet.metrics.decode=67.31;Check(TrayText(a)==TrayText(b),"tray speed rounding");
    b=a;b.packet.metrics.decode=68;Check(TrayText(a)!=TrayText(b),"tray speed change");
    a.packet.metrics.state="idle";b=a;b.packet.metrics.decode=999;Check(TrayText(a)==TrayText(b),"idle speed not shown");
    a.packet.metrics.state="reading";a.packet.metrics.phase="reading the prompt";
    Check(TrayText(a).find(L"1240.0 tok/s")!=std::wstring::npos,"reading tip uses prefill");
    a.packet.metrics.gpu=0;Check(TrayText(a).find(L"GPU 0%")!=std::wstring::npos,"real zero in tray");
    a.packet.metrics.gpu.reset();Check(TrayText(a).find(L"GPU")==std::wstring::npos,"missing GPU in tray");
    Check(TrayText(a).size()<=127,"native tray capacity");
}
struct Pair {const char* name;ViewState a,b;std::function<bool(bool,float)> same;};
void Updates(TextResources& resources) {
    std::vector<Pair> cases;const auto base=Fixture();
    auto add=[&](const char* name,std::function<void(ViewState&)> mutate,std::function<bool(bool,float)> same) {
        auto b=base;mutate(b);cases.push_back({name,base,b,same});
    };
    auto yes=[](bool,float){return true;};auto no=[](bool,float){return false;};
    auto compact=[](bool c,float){return c;};
    auto hiddenHistory=[](bool c,float h){return c && h<230;};
    add("identical packet",[](ViewState&){},yes);
    add("timestamp and hidden metadata",[](ViewState& v){v.packet.received=123;v.packet.metrics.lastDecode=90;v.packet.metrics.images=false;v.packet.metrics.gpuName="other GPU";v.interval=5000;v.topmost=false;v.port=1234;},yes);
    add("full model label",[](ViewState& v){v.packet.metrics.model="another model";},compact);
    add("phase and status tint",[](ViewState& v){v.packet.metrics.phase="thinking";},no);
    add("connection failure",[](ViewState& v){v.packet.connected=false;v.packet.error=L"failed";},no);
    add("live decode",[](ViewState& v){v.packet.metrics.decode=68;},no);
    add("rounded decode unchanged",[](ViewState& v){v.packet.metrics.decode=67.31;},yes);
    add("live prefill",[](ViewState& v){v.packet.metrics.prefill=1300;},no);
    add("context bar below text precision",[](ViewState& v){v.packet.metrics.contextUsed=128001;},no);
    add("RAM bar below text precision",[](ViewState& v){v.packet.metrics.ram=53.5001;},no);
    add("same RAM ratio",[](ViewState& v){*v.packet.metrics.ram*=2;*v.packet.metrics.ramTotal*=2;},yes);
    add("GPU visibility threshold",[](ViewState& v){v.packet.metrics.gpu=80;},[](bool c,float h){return c && h<156;});
    add("GPU bar below text precision",[](ViewState& v){v.packet.metrics.gpu=97.01;},compact);
    add("VRAM ratio",[](ViewState& v){v.packet.metrics.vram=23;},compact);
    add("temperature",[](ViewState& v){v.packet.metrics.temp=60;},compact);
    add("rounded temperature",[](ViewState& v){v.packet.metrics.temp=58.01;},yes);
    add("power",[](ViewState& v){v.packet.metrics.power=300;},compact);
    add("CPU",[](ViewState& v){v.packet.metrics.cpu=20;},compact);
    add("disk",[](ViewState& v){v.packet.metrics.disk=8;},compact);
    add("rounded disk",[](ViewState& v){v.packet.metrics.disk=1.249;},yes);
    add("decode history",[](ViewState& v){v.packet.metrics.speedHistory[30]=0;},hiddenHistory);
    add("prefill history",[](ViewState& v){v.packet.metrics.prefillHistory[30]=1000;},hiddenHistory);
    add("null is not zero",[](ViewState& v){v.packet.metrics.prefillHistory[30].reset();},hiddenHistory);
    add("trimmed old history",[](ViewState& v){v.packet.metrics.speedHistory.insert(v.packet.metrics.speedHistory.begin(),9999);},yes);
    auto pair=[&](const char* name,ViewState a,ViewState b,std::function<bool(bool,float)> same){cases.push_back({name,std::move(a),std::move(b),same});};
    auto a=base,b=base;a.packet.metrics.state="idle";b=a;b.packet.metrics.decode=999;b.packet.metrics.prefill=5000;pair("inactive speed",a,b,yes);
    a=base;a.packet.metrics.phase="writing a tool call: functions.one";b=a;b.packet.metrics.phase="writing a tool call: functions.two";pair("tool name",a,b,compact);
    a=base;a.packet.connected=false;a.packet.error=L"one error";b=a;b.packet.error=L"another error";pair("error text",a,b,compact);
    a=base;a.packet.metrics.contextUsed=1;b=a;b.packet.metrics.contextUsed=2;pair("minimum bar width",a,b,yes);
    a=base;a.packet.metrics.speedHistory={50.001};b=a;b.packet.metrics.speedHistory={50.002};pair("same normalized trace and limit",a,b,yes);
    a=base;a.packet.metrics.speedHistory={Number{},0,Number{},1,0};b=a;b.packet.metrics.speedHistory[1].reset();pair("zero and gap",a,b,hiddenHistory);
    a=base;a.packet.metrics.state="reading";a.packet.metrics.phase="reading the prompt";b=a;b.packet.metrics.decode=999;pair("decode hidden during prefill",a,b,yes);
    a=base;a.packet.metrics.gpu=6.5;b=a;b.packet.metrics.gpu=std::nextafter(6.5,7.0);
    Check(Fixed(a.packet.metrics.gpu)!=Fixed(b.packet.metrics.gpu),"GPU rounding boundary fixture");
    pair("GPU raw text rounding boundary",a,b,[](bool c,float h){return c && h<156;});
    struct Size{bool compact;float w,h,dpi;};
    for(auto size:std::vector<Size>{{false,360,552,1},{false,480,692,1},{false,820,592,1},{false,480,692,2},{false,383.05f,575.21f,101/96.0f},{true,360,144,1},{true,480,155.99f,1},{true,480,156,1},{true,382,185,101/96.0f},{true,480,229.99f,1},{true,480,230,1},{true,480,260,2}}) {
        for(auto row:cases) {
            row.a.compact=row.b.compact=size.compact;
            const bool same=SameDisplay(row.a,row.b,size.w,size.h);
            Check(same==row.same(size.compact,size.h),row.name);
            Check(same==SameDisplay(row.b,row.a,size.w,size.h),"symmetric display comparison");
            if(same)Check(Hash(row.a,size.w,size.h,resources,size.dpi)==Hash(row.b,size.w,size.h,resources,size.dpi),"suppressed packet is pixel-identical");
        }
    }
    a=base;b=a;b.compact=true;Check(!SameDisplay(a,b,480,692),"mode change is not suppressed");
}
int wmain() {
    GdiplusStartupInput input;ULONG_PTR token=0;if(GdiplusStartup(&token,&input,nullptr)!=Ok)return 2;
    int result=0;
    try {TextResources resources;Resources(resources);Tips();Updates(resources);std::cout<<checks<<" resource/cache/display/tray checks passed"<<std::endl;}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<std::endl;result=1;}
    GdiplusShutdown(token);return result;
}
