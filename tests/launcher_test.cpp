#include "../src/launcher.hpp"
#include <iostream>
#include <stdexcept>
using namespace strata;
int main(){int checks=0;auto check=[&](bool yes){++checks;if(!yes)throw std::runtime_error("launcher state assertion "+std::to_string(checks));};
    Packet p;ServiceInfo i;auto state=[&]{return LaunchState(i,p,1,L"256000 context · HIP vision",L"235000 context · text only");};
    check(!ModelReady(p));check(state().selectable && state().actionEnabled && state().preset==1);
    p.connected=true;p.metrics.model="Qwen3.8_FlashNext";p.metrics.context=256000;
    for(auto s:{"loading","unloaded","unknown"}){p.metrics.state=s;check(!ModelReady(p));check(!state().actionEnabled);}
    for(auto s:{"idle","reading","thinking","answering","writing","generating"}){p.metrics.state=s;check(ModelReady(p));check(state().ready && !state().selectable && state().action==L"进入监控");}
    i.owned=true;i.preset=0;i.phase=ServicePhase::Starting;check(!state().ready && !state().selectable && state().action==L"停止服务");
    i.phase=ServicePhase::Active;check(state().ready && state().owned);
    i.phase=ServicePhase::Stopping;check(!state().ready && !state().actionEnabled);
    i.owned=false;i.phase=ServicePhase::Failed;i.error=L"isolated failure";p.connected=false;check(state().detail==i.error && state().actionEnabled);
    i.foreign=true;i.phase=ServicePhase::Stopping;p.connected=true;p.metrics.state="idle";
    check(!state().ready && !state().selectable && !state().actionEnabled && state().action==L"正在停止…");
    i.phase=ServicePhase::Failed;p.connected=false;
    check(state().title==L"停止未完成" && state().action==L"再次尝试停止" && state().actionEnabled);
    p.connected=true;check(!state().ready && !state().selectable && state().title==L"停止未完成" && state().action==L"再次尝试停止");
    check(QuoteArgument(L"a b\\")==L"\"a b\\\\\"");check(QuoteArgument(L"a\"b")==L"\"a\\\"b\"");check(!ListeningPort(0).ok);
    std::cout<<checks<<" launcher/readiness/quoting checks passed\n";
}
