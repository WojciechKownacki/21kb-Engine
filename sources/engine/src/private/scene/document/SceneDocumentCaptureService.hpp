#pragma once

#include "engine/scene/SceneDocument.hpp"

#include <string>

namespace kb::scene {

class Scene;

class SceneDocumentCaptureService {
public:
    SceneDocumentCaptureService() = delete;

    [[nodiscard]] static SceneDocument Capture(Scene& scene, std::string name);
};

} // namespace kb::scene
