#pragma once
#include <windows.h>
#include "layout.hpp"
#include <string>
namespace strata {
struct Settings {
    unsigned port=517, interval=1000;
    bool topmost=true, compact=false, persist=true;
    int x=-32000,y=-32000;
    int width=WindowWidth,height=FullHeight,miniWidth=WindowWidth,miniHeight=MiniHeight;
    int launchWidth=520,launchHeight=600,launchX=-32000,launchY=-32000,preset=0;
    std::wstring path;
    void Load();
    bool Save() const;
};
}
