#include "engine/input/InputRecording.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetFileName.hpp"
namespace kb::input {
bool ApplyInputRecordingFrame(kb::scene::Scene& scene,std::string_view path,std::uint32_t index,std::string& error) {
    error.clear();auto& assets=scene.Assets().Manager();const auto* metadata=assets.Registry().FindByPath(kb::assets::Utf8Path(path));
    if(!metadata||metadata->type!="InputRecording"){error="input recording asset is unavailable";return false;}
    const auto recording=assets.Load<InputRecording>(metadata->id);if(!recording.IsLoaded()){error=assets.LastError();return false;}
    if(index>=recording->size()){error="input recording frame index is out of range";return false;}
    const auto& frame=(*recording)[index];ApplyInputFrame(scene.Input().MutableDeviceState(),frame);scene.Input().Evaluate(frame.deltaSeconds);return true;
}
}
