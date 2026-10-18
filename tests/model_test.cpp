#include "../src/model.hpp"
#include "../src/layout.hpp"
#include "../src/timeline.hpp"
#include <iostream>
#include <limits>
using namespace strata;
int checks=0;
void Check(bool value,const char* name){++checks;if(!value)throw std::runtime_error(name);}
void Reject(const std::string& body){bool rejected=false;try{Parse(body);}catch(...){rejected=true;}Check(rejected,"reject invalid body");}
int main(){
    try {
        Json j={{"live",{{"state","idle"}}},{"hardware",Json::object()},{"engine",{{"model","Qwen3.8_FlashNext"},{"images",true}}}};
        auto m=Parse(j.dump());
        Check(m.state=="idle" && m.model=="Qwen3.8_FlashNext","basic fields");Check(m.images,"vision flag");
        Check(!m.decode && !m.gpu && m.speedHistory.empty(),"absent not zero");
        j["live"]["tok_s"]=0;Check(Parse(j.dump()).decode==0,"valid zero");
        j["live"]["tok_s"]=-1;Check(!Parse(j.dump()).decode,"negative speed");
        j["live"]["tok_s"]=1e8;Check(!Parse(j.dump()).decode,"speed range");
        j["live"]["tok_s"]="66";Check(!Parse(j.dump()).decode,"string not numeric");
        j["live"]["tok_s"]=66.5;Check(Parse(j.dump()).decode==66.5,"real speed");
        j["hardware"]={{"gpu_util",101},{"cpu",-1},{"gpu_temp",201},{"gpu_power",2001}};
        m=Parse(j.dump());Check(!m.gpu&&!m.cpu&&!m.temp&&!m.power,"sensor ranges");
        j["hardware"]={{"gpu_mem_used",200},{"gpu_mem_total",100},{"ram_used",20},{"ram_total",10}};
        m=Parse(j.dump());Check(!m.vram&&!m.ram,"impossible capacity");
        j["hardware"]={{"gpu_mem_used",100},{"gpu_mem_total",100},{"ram_used",0},{"ram_total",10}};
        m=Parse(j.dump());Check(m.vram==100 && m.ram==0,"capacity boundaries");
        j["requests"]={{{"decode_tok_s",71}},{{"decode_tok_s",60}}};Check(Parse(j.dump()).lastDecode==71,"latest request");
        j["history"]["tok_s"]=Json::array();for(int i=0;i<100;++i)j["history"]["tok_s"].push_back(i);
        m=Parse(j.dump());Check(m.speedHistory.size()==60 && m.speedHistory.front()==40 && m.speedHistory.back()==99,"last 60 samples");
        j["history"]["tok_s"]={1,nullptr,-1,"bad",4};m=Parse(j.dump());
        Check(m.speedHistory.size()==5&&!m.speedHistory[1]&&!m.speedHistory[2]&&!m.speedHistory[3]&&m.speedHistory[4]==4,"history gaps");
        j["engine"]["model"]=std::string(513,'x');Check(Parse(j.dump()).model.empty(),"text bound");
        Json f={{"x",std::numeric_limits<double>::infinity()}};Check(!Num(f,"x"),"infinity");
        f["x"]=std::numeric_limits<double>::quiet_NaN();Check(!Num(f,"x"),"nan");
        Reject("");Reject("{}");Reject("[]");Reject("broken");Reject(std::string(256*1024+1,' '));
        j["live"]["state"]=17;Reject(j.dump());j["live"]["state"]="";Reject(j.dump());
        j["live"]["state"]="idle";j["hardware"]=Json::array();Reject(j.dump());
        Json u={{"live",{{"state","generating"},{"phase","thinking"},{"prompt_tokens",162000},{"generated",600}}},
            {"hardware",{{"ram_used",40},{"ram_total",64}}},{"engine",{{"max_context",256000}}}};
        m=Parse(u.dump());Check(StageOf(m)==Stage::Thinking,"thinking phase");
        Check(m.contextLive && m.contextUsed==162600,"current context input plus generation");
        Check(Percent(m.contextUsed,m.context).value()==162600.0/256000*100,"context ratio");
        const std::pair<const char*,Stage> phases[]={{"answering",Stage::Answering},{"writing a tool call: functions.test",Stage::Writing},
            {"writing",Stage::Writing},{"tool call complete",Stage::ToolComplete},{"unknown phase",Stage::Generating},{"",Stage::Generating}};
        for(const auto& row:phases){u["live"]["phase"]=row.first;Check(StageOf(Parse(u.dump()))==row.second,"phase mapping");}
        u["live"]["state"]="reading";u["live"]["phase"]="reading the prompt";u["live"]["generated"]=0;u["live"]["prompt_total"]=164000;
        m=Parse(u.dump());Check(StageOf(m)==Stage::Reading && m.contextUsed==164000,"reading progress total");
        u["live"]["state"]="idle";u["live"]["phase"]="thinking";
        u["requests"]={{{"prompt_total",160000},{"prompt_tokens",159000},{"engine_generated",2000},{"output_tokens",1800}}};
        m=Parse(u.dump());Check(StageOf(m)==Stage::Idle && !m.contextLive && m.contextUsed==162000,"idle clearly uses last native counts");
        u["requests"]={{{"prompt_tokens",1000},{"output_tokens",200}}};Check(Parse(u.dump()).contextUsed==1200,"history fallback");
        u["live"]["state"]="unloaded";Check(!Parse(u.dump()).contextUsed,"unloaded clears context");
        u["live"]["state"]="idle";u["requests"]={{{"prompt_tokens",1000}}};Check(!Parse(u.dump()).contextUsed,"missing output not invented");
        u["live"]["state"]="generating";u["live"]["prompt_total"]=nullptr;u["live"]["prompt_tokens"]=255999;u["live"]["generated"]=2;
        Check(!Parse(u.dump()).contextUsed,"over-limit context omitted");
        u["live"]["prompt_tokens"]=12.5;Check(!Parse(u.dump()).contextUsed,"fractional token count rejected");
        Check(!Percent(1.0,0.0) && !Percent(101.0,100.0) && Percent(0.0,100.0)==0,"capacity boundaries");
        Metrics h;h.speedHistory={Number{},0,60,70};h.prefillHistory={1200,Number{},1400};
        auto timeline=Timeline(h);
        Check(timeline.decode.size()==60 && timeline.prefill.size()==60,"shared history slots");
        Check(timeline.decode[57]==0 && timeline.decode[58]==60 && timeline.decode[59]==70,"decode aligned to latest slot");
        Check(timeline.prefill[57]==1200 && !timeline.prefill[58] && timeline.prefill[59]==1400,"prefill aligned without invented zero");
        Check(timeline.decodeTop==70 && timeline.prefillTop==1400,"independent true speed scales");
        Check(HistoryX(0)==0 && HistoryX(59)==1,"common time axis endpoints");
        for(size_t i=0;i<56;++i)Check(!timeline.decode[i] && !timeline.prefill[i],"unknown history remains a gap");
        for(size_t i=1;i<60;++i)Check(HistoryX(i)>HistoryX(i-1),"shared time slots strictly increasing");
        h.speedHistory.clear();for(int i=0;i<100;++i)h.speedHistory.push_back(i);
        timeline=Timeline(h);Check(timeline.decode.front()==40 && timeline.decode.back()==99,"long history trimmed from left");
        h.speedHistory={-1,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),1e8,0};
        timeline=Timeline(h);Check(!timeline.decode[55]&&!timeline.decode[56]&&!timeline.decode[57]&&!timeline.decode[58]&&timeline.decode[59]==0,"invalid values omitted and real zero kept");
        h.speedHistory.clear();h.prefillHistory.clear();timeline=Timeline(h);
        Check(timeline.decodeTop==1 && timeline.prefillTop==1,"empty scales finite nonzero");
        const float widths[]={360,380,480,720,760,820,1200,2400};const float heights[]={552,592,631,632,672,692,932,1232};
        for(float width:widths)for(float height:heights) {
            auto l=Arrange(width,height);Check(l.wide==(width>=760),"responsive breakpoint");
            Box boxes[]={l.inference,l.context,l.memory,l.gpu,l.host,l.action};
            for(const auto& b:boxes)Check(b.w>0 && b.h>0 && b.x>=l.margin-0.1f && b.x+b.w<=width-l.margin+0.1f && b.y>=108 && b.Bottom()<=height-l.margin+0.1f,"layout bounds");
            Check(l.inference.h>=128,"inference minimum height");
            Check(l.action.h==ActionBand && l.action.w==StopWidth && l.action.y==l.footer &&
                l.action.x==width-24-StopWidth && l.action.Bottom()<=height-20,"stop band sits under the host row");
            Check(l.footer==height-20-ActionBand,"the action band is reserved out of the panel stack");
            for(int a=0;a<6;++a)for(int b=a+1;b<6;++b) {
                bool overlap=boxes[a].x<boxes[b].x+boxes[b].w-0.1f && boxes[a].x+boxes[a].w>boxes[b].x+0.1f && boxes[a].y<boxes[b].Bottom()-0.1f && boxes[a].Bottom()>boxes[b].y+0.1f;
                Check(!overlap,"semantic regions do not overlap");
            }
        }
        std::cout<<checks<<" model/phase/layout checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
