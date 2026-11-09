#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/input/InputRecording.hpp"
#include "engine/input/InputSensors.hpp"
#include "engine/scene/Scene.hpp"
namespace kb::script {
namespace {
const ScriptValue& Arg(std::span<const ScriptFunctionArgument> args,std::string_view name){static const ScriptValue empty;for(const auto& a:args)if(a.name==name)return a.value;return empty;}
ScriptFunctionCallResult Error(std::string error){return {.executed=false,.outputs={},.errors={std::move(error)}};}
}
bool RegisterInputSensorApi(ScriptRuntimeHost& host) {
    ScriptFunctionDesc sensor;sensor.signature.name="Input.Sensor";sensor.signature.description="Read Accelerometer (m/s^2, includes gravity) or Gyroscope (rad/s) in native device axes. available=false and explicit status distinguish missing, paused or failed hardware. Evaluated state observes replay/gameplay blocking; raw=true reads the physical snapshot. See SensorInput.";
    sensor.signature.inputs={{"kind",ScriptValueType::String,true},{"raw",ScriptValueType::Bool,false}};
    sensor.signature.outputs={{"available",ScriptValueType::Bool,true},{"status",ScriptValueType::String,true},{"x",ScriptValueType::Float,true},{"y",ScriptValueType::Float,true},{"z",ScriptValueType::Float,true},{"timeNanoseconds",ScriptValueType::Int64,true},{"unit",ScriptValueType::String,true},{"source",ScriptValueType::String,true},{"replayed",ScriptValueType::Bool,true},{"nativeError",ScriptValueType::UInt32,true}};
    sensor.callback=[](const ScriptFunctionCallContext& c,std::span<const ScriptFunctionArgument> a)->ScriptFunctionCallResult{
        if(!c.scene)return Error("sensor query requires an active scene");kb::input::InputSensorKind kind{};if(!kb::input::ParseInputSensorKind(Arg(a,"kind").AsString(),kind))return Error("sensor kind must be Accelerometer or Gyroscope");
        const auto& frame=Arg(a,"raw").AsBool()?c.scene->Input().DeviceState().Sensors():c.scene->Input().SensorState();const auto* sample=kb::input::FindInputSensorSample(frame,kind);if(!sample)return Error("sensor kind is invalid");
        return {.executed=true,.outputs={{"available",ScriptValue{sample->Available()}},{"status",ScriptValue{std::string{kb::input::ToString(sample->status)}}},{"x",ScriptValue{sample->value[0]}},{"y",ScriptValue{sample->value[1]}},{"z",ScriptValue{sample->value[2]}},{"timeNanoseconds",ScriptValue{static_cast<std::int64_t>(sample->sampleTimeNanoseconds)}},{"unit",ScriptValue{std::string{kind==kb::input::InputSensorKind::Accelerometer?"m/s^2":"rad/s"}}},{"source",ScriptValue{std::string{kb::input::ToString(frame.source)}}},{"replayed",ScriptValue{frame.replayed}},{"nativeError",ScriptValue{sample->nativeError}}}};
    };
    bool ok=host.RegisterFunction(std::move(sensor));
    ScriptFunctionDesc replay;replay.signature.name="Input.ReplayFrame";replay.signature.description="Apply and evaluate one validated frame from a discovered/cooked InputRecording asset to primary input. Replay provenance remains explicit and does not qualify a physical sensor.";replay.signature.inputs={{"asset",ScriptValueType::String,true},{"frame",ScriptValueType::UInt32,true}};replay.signature.outputs={{"applied",ScriptValueType::Bool,true}};
    replay.callback=[](const ScriptFunctionCallContext& c,std::span<const ScriptFunctionArgument> a)->ScriptFunctionCallResult{if(!c.scene)return Error("replay requires an active scene");std::string error;if(!kb::input::ApplyInputRecordingFrame(*c.scene,Arg(a,"asset").AsString(),Arg(a,"frame").AsUInt32(),error))return Error(std::move(error));return {.executed=true,.outputs={{"applied",ScriptValue{true}}}};};return host.RegisterFunction(std::move(replay))&&ok;
}
}
