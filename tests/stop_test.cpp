// 0.5 checks: the 32 DIP action band and the Stop control.
//
// Two claims are checked rather than asserted:
//   * the deployed 0.4 layout is reproduced verbatim below, and the new layout must return the
//     same panel stack at height + 32 for every width, so the band only takes its own 32 DIP;
//   * the Stop control is painted through a real 32 bpp device context, the way WM_DRAWITEM
//     hands one to it, and read back pixel by pixel.
#include <windows.h>
#include "../src/view.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
using namespace strata;
using namespace Gdiplus;
int checks=0;
void Check(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);}

struct OldLayout { float margin,footer;bool wide,dense;Box inference,context,memory,gpu,host; };
OldLayout Arrange04(float width,float height) {
    OldLayout l{};l.margin=width<400?18.0f:20.0f;
    const float t=(std::max)(0.0f,(std::min)(1.0f,(height-520)/140));
    l.footer=height-20;l.wide=width>=760;l.dense=height<600;
    const float m=l.margin,w=width-m*2;
    if(l.wide) {
        const float left=(w-24)*0.54f,right=w-24-left,x=m+left+24;
        l.context={m,l.footer-96,left,96};
        l.inference={m,108,left,l.context.y-124};
        l.memory={x,108,right,96};
        l.host={x,l.footer-48,right,48};
        l.gpu={x,220,right,l.host.y-236};
    } else {
        const float hostH=28+20*t,gap=12+4*t;
        l.host={m,l.footer-hostH,w,hostH};
        const float gpuH=112+20*t;
        l.gpu={m,l.host.y-gap-gpuH,w,gpuH};
        const float useH=88+8*t,useY=l.gpu.y-gap-useH;
        l.context={m,useY,(w-16)/2,useH};l.memory={m+(w+16)/2,useY,(w-16)/2,useH};
        l.inference={m,108,w,useY-120};
    }
    return l;
}
bool Same(const Box&a,const Box&b) {
    return std::abs(a.x-b.x)<0.0005f && std::abs(a.y-b.y)<0.0005f && std::abs(a.w-b.w)<0.0005f && std::abs(a.h-b.h)<0.0005f;
}

// BGRA bytes, top-down, exactly as the window buffer holds them.
struct Surface {
    int width=0,height=0;
    std::vector<unsigned char> bytes;
    void Pixel(int x,int y,int& r,int& g,int& b)const {
        if(x<0 || x>=width || y<0 || y>=height)throw std::runtime_error("pixel bounds");
        const unsigned char* p=bytes.data()+4*(y*width+x);
        b=p[0];g=p[1];r=p[2];
    }
};

Surface Paint(TextResources& resources,int width,int height,bool labeled,bool stopping,bool hot,bool disabled,bool pressed=false,bool focus=false,unsigned dpi=96) {
    Surface out;out.width=width;out.height=height;
    HDC screen=GetDC(nullptr);
    HDC dc=CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    HBITMAP old=static_cast<HBITMAP>(SelectObject(dc,bitmap));
    {   // Native button painting may leave a grey rim; the control must clear it, not blend it.
        Graphics seed(dc);SolidBrush canvas(Color(255,160,160,160));
        seed.FillRectangle(&canvas,0.0f,0.0f,(float)width,(float)height);
        Check(seed.GetLastStatus()==Ok,"canvas seed paints");
    }
    RECT rc{0,0,width,height};
    DrawStopControl(dc,rc,dpi,resources,labeled,stopping,hot,disabled,focus,pressed);
    GdiFlush();
    out.bytes.assign(static_cast<unsigned char*>(bits),static_cast<unsigned char*>(bits)+4*size_t(width)*height);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
    return out;
}
uint64_t Hash(const Surface& s) {
    uint64_t h=14695981039346656037ull;
    for(size_t i=0;i<s.bytes.size();i+=3){h^=s.bytes[i];h*=1099511628211ull;}
    return h;
}
int Near(const Surface& s,int r,int g,int b,int x0=0,int x1=-1,int tol=14) {
    const int right=x1<0?s.width:(std::min)(s.width,x1);
    int count=0;
    for(int y=0;y<s.height;++y)for(int x=(std::max)(0,x0);x<right;++x) {
        int r0,g0,b0;s.Pixel(x,y,r0,g0,b0);
        if(std::abs(r0-r)<=tol && std::abs(g0-g)<=tol && std::abs(b0-b)<=tol)++count;
    }
    return count;
}
int Ink(const Surface& s,int x0=0,int x1=-1) {
    const int right=x1<0?s.width:(std::min)(s.width,x1);
    int count=0;
    for(int y=0;y<s.height;++y)for(int x=(std::max)(0,x0);x<right;++x) {
        int r,g,b;s.Pixel(x,y,r,g,b);
        if(r<140 && g<140 && b<160)++count;
    }
    return count;
}

void Save(const Surface& s,const std::filesystem::path& path) {
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+(DWORD)s.bytes.size();
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=s.width;info.biHeight=-s.height;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&file),sizeof(file));out.write(reinterpret_cast<const char*>(&info),sizeof(info));
    out.write(reinterpret_cast<const char*>(s.bytes.data()),s.bytes.size());if(!out)throw std::runtime_error("preview write");
}
int main(int argc,char** argv) {
    GdiplusStartupInput input;ULONG_PTR token=0;
    if(GdiplusStartup(&token,&input,nullptr)!=Ok)return 2;
    int result=0;
    try {
        TextResources resources;
        // 1) the band is the only change in the full-view panel stack
        const float widths[]={360,380,480,720,760,820,1200};
        const float heights[]={520,552,599,600,640,660,900,1200};
        for(float width:widths)for(float height:heights) {
            const auto l=Arrange(width,height+ActionBand);
            const auto o=Arrange04(width,height);
            Check(l.margin==o.margin && l.wide==o.wide && l.dense==o.dense && l.footer==o.footer,"band reserves exactly its own 32 DIP");
            Check(Same(l.inference,o.inference) && Same(l.context,o.context) && Same(l.memory,o.memory) && Same(l.gpu,o.gpu) && Same(l.host,o.host),
                "every panel keeps its 0.4 size at height + 32");
            Check(l.action.w==StopWidth && l.action.h==ActionBand && l.action.x==width-24-StopWidth && l.action.y==o.footer,
                "the stop block is right-aligned in the band under the host row");
            Check(l.action.y>=o.host.Bottom()-0.0005f && l.action.Bottom()<=height+ActionBand-20,"the band never covers a panel");
        }
        Check(FullHeight==660+32 && MinFullHeight==520+32,"full-view default and minimum both grew by the action band");
        Check(WindowWidth==480 && MiniHeight==156 && MinWindowWidth==360 && MinMiniHeight==144,"compact view keeps its 0.4 geometry");

        // 2) compact header row: the icon joins the cluster without moving it
        Check(HeaderCluster-3*HeaderSlot-ControlWidth==24,"the four original controls keep their right margin");
        Check(HeaderStop-ControlWidth>HeaderCluster,"the compact stop icon clears the four header controls");
        Check(HeaderStop-ControlWidth-HeaderCluster==10,"the compact stop icon keeps a visible gap");
        Check(ControlTop+ControlHeight<=55,"header controls stay inside the drag band");
        Check(HeaderStop<=MinWindowWidth-120,"the compact header row fits the narrowest legal window");

        // 3) the control as the window paints it
        auto icon=Paint(resources,34,32,false,false,false,false);
        auto iconHot=Paint(resources,34,32,false,false,true,false);
        auto iconOff=Paint(resources,34,32,false,false,false,true);
        auto full=Paint(resources,132,32,true,false,false,false);
        auto fullHot=Paint(resources,132,32,true,false,true,false);
        auto stopping=Paint(resources,132,32,true,true,false,false);
        auto stopOff=Paint(resources,132,32,true,true,false,true);
        auto iconWide=Paint(resources,132,32,false,false,false,false);

        int r,g,b;
        full.Pixel(0,0,r,g,b);
        Check(r==246 && g==248 && b==252,"the control sits on the monitor canvas, not a system button face");
        full.Pixel(66,16,r,g,b);
        Check(r==246 && g==248 && b==252,"the quiet control has no separate white tile");
        Check(Ink(icon,0,34)>=15,"the compact control carries the power glyph");
        Check(Ink(icon,26,34)==0,"the compact control draws no label text");
        Check(Ink(full,42,132)>=60,"the full-view block names the action in text");
        Check(Ink(full,0,132)>Ink(iconWide,0,132)+40,"the text label is the extra affordance, not a decoration");
        Check(Near(stopping,149,99,20)>20 || Near(stopping,252,240,214)>200,"the stopping state carries the amber progress tint");
        Check(Near(full,149,99,20)==0 && Near(icon,149,99,20)==0,"an idle stop control carries no warning tint");
        Check(Near(stopOff,149,99,20)==0,"a disabled control never fakes progress");
        stopOff.Pixel(66,16,r,g,b);
        Check(r==246 && g==248 && b==252,"an unavailable block recedes into the canvas");
        Check(Hash(full)!=Hash(stopping),"the progress state repaints the block");
        Check(Hash(full)!=Hash(fullHot) && Hash(icon)!=Hash(iconHot),"hover feedback is visible in both sizes");
        Check(Hash(full)!=Hash(stopOff) && Hash(icon)!=Hash(iconOff),"an unavailable stop reads as unavailable");
        stopOff.Pixel(130,30,r,g,b);
        Check(r==246 && g==248 && b==252,"the disabled block has no hard frame");
        iconOff.Pixel(32,30,r,g,b);
        Check(r==246 && g==248 && b==252,"the disabled compact icon has no hard frame");
        Check(Near(iconOff,148,160,178)>15,"disabled glyph stays perceptible without implying availability");
        for(bool labeled:{false,true}) {
            const int w=labeled?132:34;
            const auto idle=Paint(resources,w,32,labeled,false,false,false);
            const auto hot=Paint(resources,w,32,labeled,false,true,false);
            const auto down=Paint(resources,w,32,labeled,false,true,false,true);
            const auto focus=Paint(resources,w,32,labeled,false,false,false,false,true);
            const auto unavailable=Paint(resources,w,32,labeled,false,true,true,true,true);
            const auto off=Paint(resources,w,32,labeled,false,false,true);
            idle.Pixel(1,16,r,g,b);Check(r==246 && g==248 && b==252,"idle has no tile edge");
            Check(Near(hot,253,236,237,0,-1,0)>150,"hover gets a restrained destructive tint");
            Check(Near(hot,166,49,57)>15,"hover communicates destructive intent in the icon and label");
            Check(Near(down,248,219,222,0,-1,0)>150 && Hash(down)!=Hash(hot),"pressed is distinct from hover");
            Check(Near(focus,48,98,174)>30 && Hash(focus)!=Hash(idle),"keyboard focus has a visible rounded outline");
            Check(Hash(unavailable)==Hash(off),"disabled overrides hover pressed and focus");
            const auto large=Paint(resources,w*2,64,labeled,false,true,false,false,false,192);
            Check(Near(large,166,49,57)>70,"200 percent glyph and label keep their semantic colour");
            large.Pixel(0,0,r,g,b);Check(r==246 && g==248 && b==252,"200 percent rounded corners stay on canvas");
            if(argc==2) {
                std::filesystem::path dir(argv[1]);std::filesystem::create_directories(dir);
                const std::string name=labeled?"full-":"compact-";
                Save(idle,dir/(name+"idle.bmp"));Save(hot,dir/(name+"hover.bmp"));Save(down,dir/(name+"pressed.bmp"));Save(focus,dir/(name+"focus.bmp"));Save(off,dir/(name+"disabled.bmp"));
                Save(Paint(resources,w,32,labeled,true,false,false),dir/(name+"stopping.bmp"));Save(large,dir/(name+"dpi200.bmp"));
            }
        }

        // 4) the label has to fit the reserved block at the readable size
        {
            HDC screen=GetDC(nullptr);HDC dc=CreateCompatibleDC(screen);
            {
            Graphics measure(dc);measure.SetSmoothingMode(SmoothingModeAntiAlias);
            StringFormat format;format.SetFormatFlags(StringFormatFlagsNoWrap);
            for(const wchar_t* text:{L"停止服务",L"正在停止"}) {
                Font font(L"Microsoft YaHei UI",LabelSize,FontStyleRegular,UnitPixel);
                RectF box;measure.MeasureString(text,-1,&font,PointF(0,0),&format,&box);
                Check(box.Width<=StopWidth-12-42+0.1f && box.Height<=24.1f,"the stop label fits the reserved block at 16 DIP");
            }
            }
            DeleteDC(dc);ReleaseDC(nullptr,screen);
        }
        std::cout<<checks<<" 0.5 stop band and stop control checks passed"<<std::endl;
    } catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<std::endl;result=1;}
    GdiplusShutdown(token);return result;
}
