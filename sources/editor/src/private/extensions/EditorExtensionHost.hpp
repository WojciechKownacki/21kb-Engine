#pragma once
#include "kb/editor/docking/DockTypes.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif
namespace kb::editor {
class EditorDockModel;class EditorSceneContext;struct EditorTheme;
class EditorExtensionHost final {
public:
    EditorExtensionHost(EditorSceneContext&,EditorDockModel&);
    ~EditorExtensionHost();
    EditorExtensionHost(const EditorExtensionHost&)=delete;
    EditorExtensionHost& operator=(const EditorExtensionHost&)=delete;
    [[nodiscard]] bool Install(const std::filesystem::path& manifest,std::string& error);
    [[nodiscard]] bool Uninstall(std::string_view id,std::string& error);
    [[nodiscard]] bool Invoke(std::string_view action,std::string& error);
    [[nodiscard]] bool LoadProject(std::string& error);
    [[nodiscard]] std::size_t ModuleCount() const;
    [[nodiscard]] std::size_t ActionCount() const;
    [[nodiscard]] std::size_t PanelCount() const;
    [[nodiscard]] std::uint32_t PanelId(std::string_view id) const;
    [[nodiscard]] std::string PanelText(std::string_view id) const;
    [[nodiscard]] static EditorExtensionHost* Find(EditorSceneContext&) noexcept;
#if defined(_WIN32)
    [[nodiscard]] bool Paint(HDC,const RECT&,std::uint32_t,const EditorTheme&);
    [[nodiscard]] bool PointerDown(const RECT&,std::uint32_t,int x,int y,std::string& error);
    [[nodiscard]] static RECT ButtonRect(const RECT&) noexcept;
#endif
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
