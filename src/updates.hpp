#pragma once
#include <optional>
#include <string>
namespace strata {
class TrayTipCache {
public:
    void Reset(){delivered_.reset();}
    template<class Send> bool Update(const std::wstring& tip,Send send) {
        if(delivered_ && *delivered_==tip)return false;
        if(!send(tip))return false;
        delivered_=tip;return true;
    }
private:
    std::optional<std::wstring> delivered_;
};
}
