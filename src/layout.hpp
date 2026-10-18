#pragma once
#include <algorithm>
namespace strata {
// Full view carries a 32 DIP action band under the host row, so the full heights are 32 DIP
// taller than the panel-only layout used to be; every panel keeps its previous size at the
// equivalent window height (old H <-> new H+32).
constexpr int WindowWidth=480, FullHeight=692, MiniHeight=156;
constexpr int MinWindowWidth=360, MinFullHeight=552, MinMiniHeight=144;
constexpr float ActionBand=32, StopWidth=132;
// Header geometry: the four original controls keep their 172 DIP cluster, the compact Stop icon
// takes one more slot to the left, so the drag band has to stop 216 DIP from the right edge.
constexpr int HeaderCluster=172, HeaderSlot=38, HeaderStop=216, ControlTop=13, ControlWidth=34, ControlHeight=32;
struct Box { float x,y,w,h; float Bottom()const{return y+h;} };
struct Layout {
    float width,height,margin,footer;bool wide,dense;
    Box inference,context,memory,gpu,host,action;
};
inline Layout Arrange(float width,float height) {
    Layout l{};l.width=width;l.height=height;l.margin=width<400?18.0f:20.0f;
    const float t=(std::max)(0.0f,(std::min)(1.0f,(height-MinFullHeight)/140));
    l.footer=height-20-ActionBand;l.wide=width>=760;l.dense=height<MinFullHeight+80;
    const float m=l.margin,w=width-m*2;
    l.action={width-24-StopWidth,l.footer,StopWidth,ActionBand};
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
}
