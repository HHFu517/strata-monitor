#pragma once
#include "../vendor/json.hpp"
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace strata {
using Number = std::optional<double>;
using Json = nlohmann::json;
inline const Json& Field(const Json& obj, const char* key) {
    static const Json empty;
    if (!obj.is_object()) return empty;
    auto it = obj.find(key);
    return it == obj.end() ? empty : *it;
}
inline Number Num(const Json& obj, const char* key, double max = 1e15) {
    const auto& v = Field(obj, key);
    if (!v.is_number()) return {};
    double n = v.get<double>();
    return std::isfinite(n) && n >= 0 && n <= max ? Number(n) : Number{};
}
inline std::string Text(const Json& obj, const char* key) {
    const auto& v = Field(obj, key);
    if (!v.is_string()) return {};
    const auto& s = v.get_ref<const std::string&>();
    return s.size() <= 512 ? s : std::string{};
}
inline std::vector<Number> Series(const Json& h, const char* key) {
    std::vector<Number> result;
    const auto& a = Field(h, key);
    if (!a.is_array()) return result;
    const size_t start = a.size() > 60 ? a.size() - 60 : 0;
    for (size_t i = start; i < a.size(); ++i) {
        if (!a[i].is_number()) { result.push_back({}); continue; }
        double n = a[i].get<double>();
        result.push_back(std::isfinite(n) && n >= 0 && n <= 1e7 ? Number(n) : Number{});
    }
    return result;
}
struct Metrics {
    std::string model, gpuName, state, phase;
    Number decode, prefill, lastDecode, gpu, vram, vramTotal, temp, power;
    Number cpu, ram, ramTotal, disk, generated, promptRead, promptTotal, context;
    std::vector<Number> speedHistory, prefillHistory;
    Number promptTokens, contextUsed;
    bool images = false, contextLive = false;
};
enum class Stage { Idle, Reading, Thinking, Answering, Writing, ToolComplete, Generating, Unloaded, Loading, Unknown };
inline bool Starts(const std::string& s,const char* prefix) { return s.rfind(prefix,0)==0; }
inline Stage StageOf(const Metrics& m) {
    if(m.state=="idle")return Stage::Idle;
    if(m.state=="unloaded")return Stage::Unloaded;
    if(m.state=="loading")return Stage::Loading;
    if(m.state=="reading" || Starts(m.phase,"reading"))return Stage::Reading;
    if(m.phase=="thinking" || m.state=="thinking")return Stage::Thinking;
    if(m.phase=="answering" || m.state=="answering")return Stage::Answering;
    if(Starts(m.phase,"writing") || m.state=="writing")return Stage::Writing;
    if(m.phase=="tool call complete")return Stage::ToolComplete;
    return m.state=="generating" ? Stage::Generating:Stage::Unknown;
}
inline bool IsReading(const Metrics& m) { return StageOf(m)==Stage::Reading; }
inline bool IsDecoding(const Metrics& m) {
    const auto s=StageOf(m);
    return s==Stage::Thinking || s==Stage::Answering || s==Stage::Writing || s==Stage::ToolComplete || s==Stage::Generating;
}
inline Number Count(const Json& obj,const char* key) {
    auto n=Num(obj,key,1e12);return n && std::floor(*n)==*n ? n:Number{};
}
inline Number Percent(Number used,Number total) {
    return used && total && *total>0 && *used<=*total ? Number(*used / *total*100):Number{};
}
inline Metrics Parse(const std::string& body) {
    if (body.empty() || body.size() > 256 * 1024) throw std::runtime_error("response size");
    Json d = Json::parse(body);
    const auto& live = Field(d,"live"); const auto& hw = Field(d,"hardware");
    const auto& e = Field(d,"engine"); const auto& st = Field(d,"hardware_static");
    if (!d.is_object() || !live.is_object() || !hw.is_object() || !e.is_object())
        throw std::runtime_error("not Strata metrics");
    Metrics m;
    m.phase = Text(live,"phase"); m.state = Text(live,"state"); m.model = Text(e,"model"); m.gpuName = Text(st,"gpu_name");
    if (m.state.empty()) throw std::runtime_error("missing state");
    m.decode = Num(live,"tok_s",1e7); m.prefill = Num(live,"prefill_tok_s_mean",1e7);
    m.generated = Count(live,"generated"); m.promptRead = Count(live,"prompt_read");
    m.promptTotal = Count(live,"prompt_total"); m.context = Count(e,"max_context");
    m.promptTokens = Count(live,"prompt_tokens");
    if(m.context && *m.context==0)m.context.reset();
    m.gpu = Num(hw,"gpu_util",100); m.vram = Num(hw,"gpu_mem_used");
    m.vramTotal = Num(hw,"gpu_mem_total"); m.temp = Num(hw,"gpu_temp",200);
    m.power = Num(hw,"gpu_power",2000); m.cpu = Num(hw,"cpu",100);
    m.ram = Num(hw,"ram_used"); m.ramTotal = Num(hw,"ram_total"); m.disk = Num(hw,"disk_read_mb",1e7);
    if (m.vram && m.vramTotal && *m.vram > *m.vramTotal) m.vram.reset();
    if (m.ram && m.ramTotal && *m.ram > *m.ramTotal) m.ram.reset();
    const auto& history = Field(d,"history");
    m.speedHistory = Series(history,"tok_s"); m.prefillHistory = Series(history,"prefill_tok_s_mean");
    const auto& requests = Field(d,"requests");
    if (requests.is_array() && !requests.empty())m.lastDecode = Num(requests[0],"decode_tok_s",1e7);
    if(IsReading(m) || IsDecoding(m)) {
        auto input=m.promptTotal ? m.promptTotal:m.promptTokens;
        auto output=m.generated ? m.generated:(IsReading(m)?Number(0):Number{});
        if(input && output){m.contextUsed=*input+*output;m.contextLive=true;}
    } else if(StageOf(m)==Stage::Idle && requests.is_array() && !requests.empty()) {
        const auto& last=requests[0];
        auto input=Count(last,"prompt_total");if(!input)input=Count(last,"prompt_tokens");
        auto output=Count(last,"engine_generated");if(!output)output=Count(last,"output_tokens");
        if(input && output)m.contextUsed=*input+*output;
    }
    if(m.contextUsed && m.context && *m.contextUsed>*m.context)m.contextUsed.reset();
    const auto& images = Field(e,"images"); m.images = images.is_boolean() && images.get<bool>();
    return m;
}
}
