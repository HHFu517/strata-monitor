#include "../src/view.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>
using namespace strata;
using namespace Gdiplus;
int checks=0,fitFailures=0;
void Check(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);}
void Fits(Graphics& g,const wchar_t* text,const wchar_t* family,float size,float width,float height,bool bold=false) {
    Font font(family,size,bold?FontStyleBold:FontStyleRegular,UnitPixel);StringFormat f;f.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF b;g.MeasureString(text,-1,&font,PointF(0,0),&f,&b);
    if(b.Width>width+.1f || b.Height>height+.1f) {
        std::wcerr<<L"Text does not fit: "<<text<<L" measured "<<b.Width<<L"x"<<b.Height<<L" available "<<width<<L"x"<<height<<std::endl;
        ++fitFailures;return;
    }
    Check(true,"text fits at readable size");
}
int Pixels(Bitmap& b,Box area,int r,int g,int blue) {
    int count=0;
    for(int y=(std::max)(0,static_cast<int>(area.y));y<(std::min)(static_cast<int>(b.GetHeight()),static_cast<int>(area.Bottom()));++y)
        for(int x=(std::max)(0,static_cast<int>(area.x));x<(std::min)(static_cast<int>(b.GetWidth()),static_cast<int>(area.x+area.w));++x) {
            Color c;b.GetPixel(x,y,&c);
            if(std::abs(static_cast<int>(c.GetR())-r)<12 && std::abs(static_cast<int>(c.GetG())-g)<12 && std::abs(static_cast<int>(c.GetB())-blue)<12)++count;
        }
    return count;
}
int wmain() {
    GdiplusStartupInput input;ULONG_PTR token=0;if(GdiplusStartup(&token,&input,nullptr)!=Ok)return 2;
    int result=0;
    try {
        TextResources resources;
        Bitmap canvas(480,692,PixelFormat32bppARGB);Graphics measure(&canvas);
        Check(LabelSize>=16 && MetricSize>=32 && RateSize>=42,"readable typography floor");
        Fits(measure,L"Reading prompt",L"Microsoft YaHei UI",LabelSize,144,32,true);
        Fits(measure,L"Decode · tok/s",L"Microsoft YaHei UI",LabelSize,150,24,true);
        Fits(measure,L"Prefill · tok/s",L"Microsoft YaHei UI",LabelSize,150,24,true);
        Fits(measure,L"Context fill",L"Microsoft YaHei UI",LabelSize,130,24,true);
        Fits(measure,L"100.0%",L"Segoe UI",MetricSize,130,51);
        Fits(measure,L"999.9",L"Segoe UI",RateSize,150,66);
        Fits(measure,L"9999",L"Segoe UI",RateSize,150,66);
        Fits(measure,L"Context fill",L"Microsoft YaHei UI",LabelSize,94,24);
        Fits(measure,L"100%",L"Microsoft YaHei UI",LabelSize,60,24,true);
        Fits(measure,L"Decode",L"Microsoft YaHei UI",LabelSize,68,24,true);
        Fits(measure,L"2001",L"Segoe UI",26,68,40);
        Fits(measure,L"1240",L"Segoe UI",26,68,40);
        Fits(measure,L"1.0M",L"Segoe UI",26,68,40);
        Fits(measure,L"2001",L"Segoe UI",RateSize,150,66);
        Check(fitFailures==0,"all declared text fits");
        Check(Rate(2001,true,false)==L"2001","full view prefill shows plain digits");
        Check(Rate(2001,true,true)==L"2001","compact prefill shows plain digits");
        Check(Rate(1240.7,true,true)==L"1241","compact prefill stays whole");
        Check(Rate(1043.6,false,false)==L"1044","decode from 1000 up shows plain digits");
        Check(Rate(67.31,false,true)==L"67.3","small decode keeps one decimal");
        Check(Rate(1500000,true,false)==L"1.5M","millions stay short");
        Check(Rate(999999,true,true)==L"1.0M","the compact million guard still applies");
        ViewState v;v.packet.connected=true;auto& m=v.packet.metrics;
        m.model="Qwen3.8_FlashNext";m.state="generating";m.phase="answering";m.decode=67.3;m.prefill=1240;
        m.context=256000;m.contextUsed=205723;m.contextLive=true;m.ram=53.5;m.ramTotal=63.9;
        m.vram=22.8;m.vramTotal=24;m.gpu=97;m.temp=58;m.power=355;m.cpu=59;m.disk=1.25;
        for(int i=0;i<60;++i) {
            m.prefillHistory.push_back(i>=4 && i<=19?Number(900+(i%6)*60):Number{});
            m.speedHistory.push_back(i>=20?Number(55+(i%8)*2):Number{});
        }
        auto paint=[&](int width,int height,float dpi=1) {
            Bitmap image(static_cast<INT>(width*dpi),static_cast<INT>(height*dpi),PixelFormat32bppARGB);
            {Graphics g(&image);g.ScaleTransform(dpi,dpi);Render(g,v,static_cast<float>(width),static_cast<float>(height),resources);}
            Check(image.GetLastStatus()==Ok,"render succeeds");
            if(!v.compact && v.packet.connected && width==480 && height==692 && dpi==1) {
                auto b=Arrange(480,692).inference;Box plot{b.x,b.y+114,b.w,b.h-114};
                Check(Pixels(image,plot,48,98,174)>20,"decode trace remains visible");
                Check(Pixels(image,plot,149,99,20)>20,"prefill trace remains visible");
            }
        };
        for(const auto& row:std::vector<std::pair<const char*,const wchar_t*>>{{"reading the prompt",L"Reading prompt"},{"thinking",L"Thinking"},{"answering",L"Answering"},{"writing a tool call: functions.example",L"Tool calling"}}) {
            m.phase=row.first;m.state=m.phase=="reading the prompt"?"reading":"generating";
            Check(StateLabel(v)==row.second,"exact English stage label");paint(480,692);
        }
        m.phase="answering";m.state="generating";paint(360,552);paint(820,592);paint(480,692,2);
        v.compact=true;paint(480,156);paint(360,144);paint(480,260);v.compact=false;
        m.model=std::string(512,'W');m.phase="writing a tool call: "+std::string(470,'W');paint(360,552);
        v.packet.connected=false;v.packet.error=L"接口需要授权 · 本版仅支持无密钥本机监控";paint(360,552);
        v.packet.connected=true;m=Metrics{};m.state="idle";Check(StateLabel(v)==L"Idle","idle state");paint(360,552);
        auto light=[](HICON icon) {
            Check(icon!=nullptr,"icon created");{Bitmap image(icon);Color c;image.GetPixel(16,3,&c);Check(c.GetR()>230 && c.GetG()>230 && c.GetB()>230,"icon uses light background");}DestroyIcon(icon);
        };
        light(MakeIcon());wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);
        auto path=std::filesystem::path(exe).parent_path().parent_path()/L"assets"/L"monitor.ico";
        light(static_cast<HICON>(LoadImageW(nullptr,path.c_str(),IMAGE_ICON,32,32,LR_LOADFROMFILE)));
        std::cout<<checks<<" typography/stage/plot/icon checks passed"<<std::endl;
    } catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<std::endl;result=1;}
    GdiplusShutdown(token);return result;
}
