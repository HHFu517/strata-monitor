#pragma once
#include "model.hpp"
#include <algorithm>
#include <array>
namespace strata {
constexpr size_t HistorySlots=60;
using HistoryTrace=std::array<Number,HistorySlots>;
struct SpeedTimeline {
    HistoryTrace decode{},prefill{};
    double decodeTop=1,prefillTop=1;
};
inline HistoryTrace AlignHistory(const std::vector<Number>& values) {
    HistoryTrace out{};
    const size_t count=(std::min)(values.size(),HistorySlots),start=values.size()-count;
    for(size_t i=0;i<count;++i) {
        auto v=values[start+i];
        if(v && std::isfinite(*v) && *v>=0 && *v<=1e7)out[HistorySlots-count+i]=v;
    }
    return out;
}
inline double HistoryTop(const HistoryTrace& values) {
    double top=1;for(const auto& v:values)if(v)top=(std::max)(top,*v);return top;
}
inline float HistoryX(size_t slot) {return static_cast<float>(slot)/static_cast<float>(HistorySlots-1);}
inline SpeedTimeline Timeline(const Metrics& m) {
    SpeedTimeline t;t.decode=AlignHistory(m.speedHistory);t.prefill=AlignHistory(m.prefillHistory);
    t.decodeTop=HistoryTop(t.decode);t.prefillTop=HistoryTop(t.prefill);return t;
}
}
