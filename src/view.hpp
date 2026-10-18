#pragma once
#include "client.hpp"
#include "layout.hpp"
#include <objidl.h>
#include <gdiplus.h>
#include <memory>
namespace strata {
constexpr float LabelSize=16.0f, MetricSize=34.0f, RateSize=44.0f;
enum ControlId { Pin=101, Compact=102, Hide=103, Menu=104, StopControl=105 };
struct ViewState {
    Packet packet;
    bool compact=false, topmost=true;
    unsigned interval=1000, port=517;
};
// Window-owned, fixed-size GDI+ cache. Destroy before GdiplusShutdown.
class TextResources {
public:
    TextResources();
    const Gdiplus::Font* LabelFont(float size,bool bold)const;
    const Gdiplus::Font* NumberFont(float size)const;
    const Gdiplus::StringFormat* LineFormat(Gdiplus::StringAlignment align)const;
    const Gdiplus::StringFormat* ParagraphFormat()const{return &paragraph_;}
private:
    Gdiplus::Font label_,labelBold_,title_,metric_,rate_,miniRate_,host_;
    Gdiplus::StringFormat lines_[3],paragraph_;
};
std::wstring Wide(const std::string& s);
std::wstring Fixed(Number value,int precision=0);
// Speed text: plain digits from 1000 up (2001, not 2.0k); millions keep the short form.
std::wstring Rate(Number value,bool prefill=false,bool compact=false);
std::wstring StateLabel(const ViewState& s);
std::wstring TrayText(const ViewState& s);
bool SameDisplay(const ViewState& a,const ViewState& b,float width,float height);
void Render(Gdiplus::Graphics& g,const ViewState& s,float width,float height,TextResources& resources);
void DrawControl(HDC dc,const RECT& bounds,unsigned dpi,int id,bool selected,bool hot,bool focus);
// Labeled in the full view's action band, icon-only in the compact view's header row.
void DrawStopControl(HDC dc,const RECT& bounds,unsigned dpi,TextResources& resources,bool labeled,bool stopping,bool hot,bool disabled,bool focus,bool pressed=false);
HICON MakeIcon();
}
