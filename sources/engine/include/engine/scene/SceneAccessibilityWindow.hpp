#pragma once
#include <string>
namespace kb::scene {
class Scene;
// One full-client-area UI tree per native window. Attach/detach on its owner thread.
class SceneAccessibilityWindow final {
public:
    [[nodiscard]] static bool Attach(Scene&,void* nativeWindow,std::string& error);
    static void Detach(Scene&);
};
}
