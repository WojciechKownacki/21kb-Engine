#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/input/InputRecording.hpp"
#include "engine/platform/win32/Win32MotionSensors.hpp"
#include "engine/assets/AssetManager.hpp"
#include <array>
#include <map>
#include <cmath>
#include <limits>
#include <fstream>
#include <stdexcept>
extern "C" void ecs_os_set_api_defaults(void);
static std::map<std::pair<std::uint64_t,std::uint64_t>,int> frames;
static void Require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
static void Log(kb::script::ScriptExecutionContext* c,const std::string& text){Require(c->CallFunction("Log",std::array{kb::script::ScriptFunctionArgument{"message",kb::script::ScriptValue{text}}}).Succeeded(),"sensor log failed");}
static constexpr const char* Asset="/Game/Input/Motion.21kbinputrecording";
static void Ready(kb::script::ScriptExecutionContext* c){
 using namespace kb::input;auto& scene=c->GetScene();frames[{scene.Id(),c->Self().Id()}]=0;
 Win32MotionSensors provider;auto actual=provider.Poll(true);Require(IsInputSensorFrameValid(actual)&&actual.source==InputSensorSource::WindowsSensors&&!actual.replayed,"Windows sensor provider returned malformed or replay data");
 Log(c,std::string{"native-sensor-provider accel="}+ToString(actual.accelerometer.status)+" gyro="+ToString(actual.gyroscope.status)+" measured="+std::to_string(actual.accelerometer.Available()&&actual.gyroscope.Available())+" errors="+std::to_string(actual.accelerometer.nativeError)+"/"+std::to_string(actual.gyroscope.nativeError)+" accelerationSI="+std::to_string(actual.accelerometer.value[0])+","+std::to_string(actual.accelerometer.value[1])+","+std::to_string(actual.accelerometer.value[2])+" gyroSI="+std::to_string(actual.gyroscope.value[0])+","+std::to_string(actual.gyroscope.value[1])+","+std::to_string(actual.gyroscope.value[2])+" timesNs="+std::to_string(actual.accelerometer.sampleTimeNanoseconds)+"/"+std::to_string(actual.gyroscope.sampleTimeNanoseconds));
 const auto paused=provider.Poll(false);Require(IsInputSensorFrameValid(paused)&&paused.accelerometer.status==InputSensorStatus::Suspended&&paused.gyroscope.status==InputSensorStatus::Suspended,"sensor pause did not clear samples");const auto resumed=provider.Poll(true);Require(IsInputSensorFrameValid(resumed)&&resumed.accelerometer.status!=InputSensorStatus::Suspended&&resumed.gyroscope.status!=InputSensorStatus::Suspended,"sensor provider did not resume");
 if(scene.Assets().Manager().Registry().FindByPath(Asset))return;
 const auto path=scene.Assets().Manager().Mounts().Resolve(Asset);Require(path.has_value(),"recording authoring mount missing");std::filesystem::create_directories(path->parent_path());
 InputDeviceState device;device.SetHasFocus(true);InputSensorFrame sample;sample.source=InputSensorSource::Replay;sample.accelerometer={InputSensorStatus::Active,{0,9.80665F,0},1000000000,0};sample.gyroscope={InputSensorStatus::Active,{0,0,1.57079632679F},1000000000,0};Require(device.SetSensors(sample),"SI replay sample rejected");
 InputRecording recording{CaptureInputFrame(device,.016F)};sample.accelerometer={InputSensorStatus::Suspended};sample.gyroscope={InputSensorStatus::Suspended};Require(device.SetSensors(sample),"paused replay sample rejected");recording.push_back(CaptureInputFrame(device,.016F));
 Require(WriteInputRecording(*path,recording),"recording authoring failed");const auto before=ReadInputRecording(*path);Require(before.succeeded&&before.asset.size()==2,"saved sensor recording lost frames");recording[0].sensors.accelerometer.value[0]=std::numeric_limits<float>::quiet_NaN();Require(!WriteInputRecording(*path,recording),"invalid sensor recording replaced a valid file");const auto after=ReadInputRecording(*path);Require(after.succeeded&&after.asset[0].sensors==before.asset[0].sensors,"invalid recording write changed saved samples");Log(c,"sensor-recording-author PASS");
}
static void Tick(kb::script::ScriptExecutionContext* c){
 using namespace kb::input;auto& scene=c->GetScene();auto& frame=frames[{scene.Id(),c->Self().Id()}];++frame;std::string error;
 if(frame==10){Require(ApplyInputRecordingFrame(scene,Asset,0,error),error);const auto sensor=scene.Input().SensorState();Require(sensor.replayed&&sensor.source==InputSensorSource::Replay&&sensor.accelerometer.Available()&&std::abs(sensor.accelerometer.value[1]-9.80665F)<.00001F&&std::abs(sensor.gyroscope.value[2]-1.57079632679F)<.00001F&&sensor.accelerometer.sampleTimeNanoseconds==1000000000,"sensor replay changed SI units/timestamp/provenance");
  const auto result=c->CallFunction("Input.Sensor",std::array{kb::script::ScriptFunctionArgument{"kind",kb::script::ScriptValue{std::string{"Gyroscope"}}}});Require(result.Succeeded()&&result.Output("available")->AsBool()&&result.Output("unit")->AsString()=="rad/s"&&std::abs(result.Output("z")->AsFloat()-sensor.gyroscope.value[2])<.00001F,"sensor binding bypassed evaluated input");
  const auto before=scene.Input().DeviceState().Sensors();Require(!ApplyInputRecordingFrame(scene,Asset,99,error)&&scene.Input().DeviceState().Sensors()==before,"invalid recording index mutated input");auto invalid=before;invalid.gyroscope.value[0]=std::numeric_limits<float>::infinity();Require(!scene.Input().MutableDeviceState().SetSensors(invalid)&&scene.Input().DeviceState().Sensors()==before,"invalid sensor state replaced live input");
  scene.Input().SetGameplayInputBlocked(true);scene.Input().Evaluate(.016F);Require(!scene.Input().SensorState().accelerometer.Available()&&scene.Input().SensorState().accelerometer.status==InputSensorStatus::Suspended&&scene.Input().DeviceState().Sensors().accelerometer.Available(),"modal gameplay filtering leaked or destroyed physical sensor data");scene.Input().SetGameplayInputBlocked(false);
 }
 if(frame==20){Require(ApplyInputRecordingFrame(scene,Asset,1,error),error);const auto paused=scene.Input().SensorState();Require(!paused.accelerometer.Available()&&paused.accelerometer.value==std::array<float,3>{}&&paused.accelerometer.sampleTimeNanoseconds==0,"paused replay retained a valid sample");InputDeviceState scratch;auto sample=paused;sample.source=InputSensorSource::Replay;Require(scratch.SetSensors(sample),"scratch sample failed");scratch.Reset();Require(scratch.Sensors()==InputSensorFrame{},"raw reset retained sensor state");}
 if(frame==70)Log(c,"native-input-sensors PASS SI timestamp replay invalid lifecycle provider");
}
KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api){ecs_os_set_api_defaults();return api&&api->version==kb::script::kNativeScriptPluginApiVersion&&api->registerLifecycle(api->user,"project.NativeSensorProbe",kb::script::ScriptLifecycleEvent::Ready,&Ready)&&api->registerLifecycle(api->user,"project.NativeSensorProbe",kb::script::ScriptLifecycleEvent::Tick,&Tick);}
