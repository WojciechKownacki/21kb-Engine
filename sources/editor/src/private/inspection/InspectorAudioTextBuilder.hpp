#pragma once

#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"

#include <string>

namespace kb::editor {

class InspectorAudioSourceTextBuilder {
public:
    void Append(std::string& text, const kb::scene::AudioSourceComponent& audioSource) const;
};

class InspectorAudioListenerTextBuilder {
public:
    void Append(std::string& text, const kb::scene::AudioListenerComponent& audioListener) const;
};

} // namespace kb::editor
