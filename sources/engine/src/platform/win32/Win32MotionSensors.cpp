#include "engine/platform/win32/Win32MotionSensors.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <sensorsapi.h>
#include <sensors.h>
#include <wrl/client.h>
#include <chrono>
#include <numbers>
#include <thread>
#include <cassert>
namespace kb::input {
namespace {
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
InputSensorSample Failure(InputSensorStatus status,HRESULT error=S_OK){InputSensorSample sample;sample.status=status;sample.nativeError=static_cast<std::uint32_t>(error);return sample;}
InputSensorStatus FailureStatus(HRESULT hr){if(hr==E_ACCESSDENIED)return InputSensorStatus::PermissionDenied;if(hr==HRESULT_FROM_WIN32(ERROR_NOT_FOUND)||hr==HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS))return InputSensorStatus::Unavailable;return InputSensorStatus::Error;}
std::uint64_t Ticks(FILETIME time){ULARGE_INTEGER value{};value.LowPart=time.dwLowDateTime;value.HighPart=time.dwHighDateTime;return value.QuadPart;}
}
struct Win32MotionSensors::Impl {
    struct Slot {ComPtr<ISensor> sensor;std::uint64_t reportTime=0,sampleTime=0;InputSensorSample last=Failure(InputSensorStatus::Unavailable);};
    std::thread::id owner=std::this_thread::get_id();
    bool comReady=false,comOwned=false;
    ComPtr<ISensorManager> manager;
    std::array<Slot,2> slots;
    Clock::time_point nextProbe{};
    ~Impl(){for(auto& slot:slots)slot.sensor.Reset();manager.Reset();if(comOwned)CoUninitialize();}
    void Probe() {
        if(!comReady){const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE){for(auto& slot:slots)slot.last=Failure(FailureStatus(hr),hr);return;}comReady=true;comOwned=SUCCEEDED(hr);}
        if(!manager){const auto hr=CoCreateInstance(__uuidof(SensorManager),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager));if(FAILED(hr)){for(auto& slot:slots)slot.last=Failure(FailureStatus(hr),hr);return;}}
        for(std::size_t i=0;i<slots.size();++i){auto& slot=slots[i];if(slot.sensor)continue;ComPtr<ISensorCollection> collection;const auto hr=manager->GetSensorsByType(i==0?SENSOR_TYPE_ACCELEROMETER_3D:SENSOR_TYPE_GYROMETER_3D,&collection);if(FAILED(hr)){slot.last=Failure(FailureStatus(hr),hr);continue;}ULONG count=0;if(!collection){slot.last=Failure(InputSensorStatus::Error,E_UNEXPECTED);continue;}const auto counted=collection->GetCount(&count);if(FAILED(counted)){slot.last=Failure(FailureStatus(counted),counted);continue;}if(count==0){slot.last=Failure(InputSensorStatus::Unavailable);continue;}const auto selected=collection->GetAt(0,&slot.sensor);if(FAILED(selected))slot.last=Failure(FailureStatus(selected),selected);else{slot.reportTime=slot.sampleTime=0;slot.last=Failure(InputSensorStatus::Starting);}}
    }
    InputSensorSample Read(std::size_t i) {
        auto& slot=slots[i];if(!slot.sensor)return slot.last;
        SensorState state=SENSOR_STATE_ERROR;auto hr=slot.sensor->GetState(&state);if(FAILED(hr)){slot.sensor.Reset();return slot.last=Failure(FailureStatus(hr),hr);}
        if(state!=SENSOR_STATE_READY){InputSensorStatus status=InputSensorStatus::Error;switch(state){case SENSOR_STATE_NOT_AVAILABLE:status=InputSensorStatus::Unavailable;slot.sensor.Reset();break;case SENSOR_STATE_NO_DATA:case SENSOR_STATE_INITIALIZING:status=InputSensorStatus::Starting;break;case SENSOR_STATE_ACCESS_DENIED:status=InputSensorStatus::PermissionDenied;break;default:break;}return slot.last=Failure(status);}
        ComPtr<ISensorDataReport> report;hr=slot.sensor->GetData(&report);if(FAILED(hr)||!report)return slot.last=Failure(FailureStatus(FAILED(hr)?hr:E_UNEXPECTED),FAILED(hr)?hr:E_UNEXPECTED);
        SYSTEMTIME timestamp{};hr=report->GetTimestamp(&timestamp);FILETIME sampleTime{};if(FAILED(hr)||!SystemTimeToFileTime(&timestamp,&sampleTime))return slot.last=Failure(InputSensorStatus::Error,FAILED(hr)?hr:E_INVALIDARG);
        FILETIME now{};GetSystemTimePreciseAsFileTime(&now);const auto reportTicks=Ticks(sampleTime),nowTicks=Ticks(now);
        if(reportTicks>nowTicks&&reportTicks-nowTicks>10000000ULL)return slot.last=Failure(InputSensorStatus::Error,E_INVALIDARG);
        const auto age=nowTicks>=reportTicks?nowTicks-reportTicks:0;
        if(age>5000000ULL)return slot.last=Failure(InputSensorStatus::Stale);
        const PROPERTYKEY* keys[2][3]{{&SENSOR_DATA_TYPE_ACCELERATION_X_G,&SENSOR_DATA_TYPE_ACCELERATION_Y_G,&SENSOR_DATA_TYPE_ACCELERATION_Z_G},{&SENSOR_DATA_TYPE_ANGULAR_VELOCITY_X_DEGREES_PER_SECOND,&SENSOR_DATA_TYPE_ANGULAR_VELOCITY_Y_DEGREES_PER_SECOND,&SENSOR_DATA_TYPE_ANGULAR_VELOCITY_Z_DEGREES_PER_SECOND}};
        InputSensorSample output;output.status=InputSensorStatus::Active;
        for(std::size_t axis=0;axis<3;++axis){PROPVARIANT value{};hr=report->GetSensorValue(*keys[i][axis],&value);double raw=0;if(SUCCEEDED(hr)){if(value.vt==VT_R8)raw=value.dblVal;else if(value.vt==VT_R4)raw=value.fltVal;else hr=E_UNEXPECTED;}PropVariantClear(&value);if(FAILED(hr))return slot.last=Failure(FailureStatus(hr),hr);const double normalized=raw*(i==0?9.80665:std::numbers::pi_v<double>/180.0);output.value[axis]=static_cast<float>(normalized);if(!std::isfinite(output.value[axis]))return slot.last=Failure(InputSensorStatus::Error,E_INVALIDARG);}
        if(reportTicks!=slot.reportTime){const auto monotonic=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();const auto ageNs=age*100ULL;if(monotonic<=0||ageNs>=static_cast<std::uint64_t>(monotonic))return slot.last=Failure(InputSensorStatus::Error,E_INVALIDARG);const auto mapped=static_cast<std::uint64_t>(monotonic)-ageNs;if(mapped<slot.sampleTime)return slot.last=Failure(InputSensorStatus::Error,E_INVALIDARG);slot.sampleTime=mapped;slot.reportTime=reportTicks;}
        output.sampleTimeNanoseconds=slot.sampleTime;return slot.last=output;
    }
    InputSensorFrame Poll(bool active) {
        assert(owner==std::this_thread::get_id());InputSensorFrame frame;frame.source=InputSensorSource::WindowsSensors;
        if(!active){for(auto& slot:slots){slot.sensor.Reset();slot.reportTime=slot.sampleTime=0;slot.last=Failure(InputSensorStatus::Suspended);}nextProbe={};frame.accelerometer=frame.gyroscope=Failure(InputSensorStatus::Suspended);return frame;}
        const auto now=Clock::now();if(now>=nextProbe){Probe();nextProbe=now+std::chrono::seconds(1);}
        frame.accelerometer=Read(0);frame.gyroscope=Read(1);return frame;
    }
};
Win32MotionSensors::Win32MotionSensors():impl_(std::make_unique<Impl>()){}
Win32MotionSensors::~Win32MotionSensors()=default;
InputSensorFrame Win32MotionSensors::Poll(bool active) noexcept{return impl_->Poll(active);}
}
#endif
