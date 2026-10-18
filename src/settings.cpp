#include "settings.hpp"
namespace strata {
void Settings::Load() {
    wchar_t file[32768]{};
    DWORD n=GetModuleFileNameW(nullptr,file,32768);
    if(!n || n>=32768)return;
    path=file;
    path=path.substr(0,path.find_last_of(L"\\/")+1)+L"monitor.ini";
    if(!persist)return;
    auto get=[&](const wchar_t* section,const wchar_t* key,int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(section,key,fallback,path.c_str()));
    };
    int value=get(L"connection",L"port",517);
    if(value>0 && value<=65535)port=static_cast<unsigned>(value);
    value=get(L"connection",L"interval_ms",1000);
    if(value==1000 || value==2000 || value==5000)interval=static_cast<unsigned>(value);
    topmost=get(L"window",L"topmost",1)!=0;
    compact=get(L"window",L"compact",0)!=0;
    x=get(L"window",L"x",-32000); y=get(L"window",L"y",-32000);
    auto dimension=[&](const wchar_t* key,int fallback,int minimum) {
        int n=get(L"window",key,fallback);return n>=minimum && n<=8192?n:fallback;
    };
    width=dimension(L"width",WindowWidth,MinWindowWidth);height=dimension(L"height",FullHeight,MinFullHeight);
    const int legacyHeight=get(L"window",L"height",FullHeight);
    if(legacyHeight>=520 && legacyHeight<MinFullHeight)height=MinFullHeight;
    miniWidth=dimension(L"mini_width",WindowWidth,MinWindowWidth);miniHeight=dimension(L"mini_height",MiniHeight,MinMiniHeight);
    auto launch=[&](const wchar_t* key,int fallback,int minimum){int n=get(L"launcher",key,fallback);return n>=minimum && n<=8192?n:fallback;};
    launchWidth=launch(L"width",520,420);launchHeight=launch(L"height",600,560);
    launchX=get(L"launcher",L"x",-32000);launchY=get(L"launcher",L"y",-32000);
    preset=get(L"launcher",L"preset",0)==1?1:0;
}
bool Settings::Save() const {
    if(!persist || path.empty())return true;
    bool ok=true;
    auto put=[&](const wchar_t* section,const wchar_t* key,int value) {
        std::wstring text=std::to_wstring(value);
        if(!WritePrivateProfileStringW(section,key,text.c_str(),path.c_str()))ok=false;
    };
    put(L"connection",L"port",static_cast<int>(port));
    put(L"connection",L"interval_ms",static_cast<int>(interval));
    put(L"window",L"topmost",topmost?1:0); put(L"window",L"compact",compact?1:0);
    put(L"window",L"x",x);put(L"window",L"y",y);
    put(L"window",L"width",width);put(L"window",L"height",height);
    put(L"window",L"mini_width",miniWidth);put(L"window",L"mini_height",miniHeight);
    put(L"launcher",L"width",launchWidth);put(L"launcher",L"height",launchHeight);
    put(L"launcher",L"x",launchX);put(L"launcher",L"y",launchY);put(L"launcher",L"preset",preset);
    return ok;
}
}
