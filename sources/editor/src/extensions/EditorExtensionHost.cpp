#include "extensions/EditorExtensionHost.hpp"
#include "authoring/EditorAuthoringHost.hpp"
#include "console/EditorConsoleState.hpp"
#include "docking/EditorDockModel.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/core/JsonValue.hpp"
#include "kb/editor/extensions/EditorExtension.h"
#include "kb/editor/theme/EditorTheme.hpp"
#include "project/EditorProjectPaths.hpp"
#include "scene/EditorSceneContext.hpp"
#if defined(_WIN32)
#include "platform/win32/Utf8Gdi.hpp"
#endif
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <unordered_map>
namespace kb::editor {
namespace {
auto& Hosts() {
    static std::unordered_map<EditorSceneContext*, EditorExtensionHost*> hosts;
    return hosts;
}
bool Name(std::string_view s) {
    return !s.empty() && s.size() <= 96 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
               c == '-';
    });
}
bool Copy(const char* text, std::string& out, std::size_t max) {
    if (!text)
        return false;
    std::size_t n = 0;
    while (n <= max && text[n])
        ++n;
    if (n > max)
        return false;
    out.assign(text, n);
    return true;
}
} // namespace
struct EditorExtensionHost::Impl {
    struct Owner;
    struct Action {
        std::string id, label;
        Owner* owner = nullptr;
        void* user = nullptr;
        KbEditorActionCallback callback = nullptr;
    };
    struct Panel {
        std::string id, title, text, action, button;
        Owner* owner = nullptr;
        std::uint32_t dockId = 0;
    };
    struct Owner {
        Impl* host = nullptr;
        std::thread::id thread;
        std::string id, response, error;
        bool closing = false;
        bool installing = true;
        std::size_t activeCalls = 0;
        KbEditorExtensionHost api{};
        KbEditorExtensionDesc desc{};
#if defined(_WIN32)
        HMODULE module = nullptr;
#endif
    };
    EditorSceneContext& context;
    EditorDockModel& dock;
    EditorAuthoringHost authoring;
    std::thread::id thread = std::this_thread::get_id();
    std::vector<std::unique_ptr<Owner>> modules;
    std::vector<Action> actions;
    std::vector<Panel> panels;
    std::uint32_t nextPanel = kFirstToolPanelId;
    Impl(EditorSceneContext& c, EditorDockModel& d) : context(c), dock(d), authoring(c) {}
    static bool Check(Owner& o, const char** error, bool registration = false) {
        if (error)
            *error = nullptr;
        if (std::this_thread::get_id() != o.thread) {
            if (error)
                *error = "editor extension requires its owner thread";
            return false;
        }
        if (o.closing || (registration && !o.installing)) {
            if (error)
                *error = "editor extension registration is closed";
            return false;
        }
        return true;
    }
    static int Fail(Owner& o, const char** error, std::string text) {
        o.error = std::move(text);
        if (error)
            *error = o.error.c_str();
        return 0;
    }
    static int AddAction(void* user, const KbEditorActionDesc* desc, const char** error) {
        auto& o = *static_cast<Owner*>(user);
        if (!Check(o, error, true))
            return 0;
        Action a;
        a.owner = &o;
        if (!desc || !desc->invoke || !Copy(desc->id, a.id, 96) || !Name(a.id) || !Copy(desc->label, a.label, 128) ||
            a.label.empty())
            return Fail(o, error, "invalid extension action descriptor");
        if (o.host->actions.size() >= 128 || std::count_if(o.host->actions.begin(), o.host->actions.end(),
                                                           [&](const auto& v) { return v.owner == &o; }) >= 32)
            return Fail(o, error, "extension action limit exceeded");
        if (std::any_of(o.host->actions.begin(), o.host->actions.end(), [&](const auto& v) { return v.id == a.id; }))
            return Fail(o, error, "extension action id already exists");
        a.user = desc->user;
        a.callback = desc->invoke;
        o.host->actions.push_back(std::move(a));
        return 1;
    }
    static int AddPanel(void* user, const KbEditorPanelDesc* desc, const char** error) {
        auto& o = *static_cast<Owner*>(user);
        if (!Check(o, error, true))
            return 0;
        Panel p;
        p.owner = &o;
        if (!desc || !Copy(desc->id, p.id, 96) || !Name(p.id) || !Copy(desc->title, p.title, 128) || p.title.empty() ||
            !Copy(desc->text, p.text, 8192) || !Copy(desc->actionId, p.action, 96) ||
            !Copy(desc->buttonLabel, p.button, 128) || p.button.empty())
            return Fail(o, error, "invalid extension panel descriptor");
        if (o.host->panels.size() >= 32 || std::count_if(o.host->panels.begin(), o.host->panels.end(),
                                                         [&](const auto& v) { return v.owner == &o; }) >= 8)
            return Fail(o, error, "extension panel limit exceeded");
        if (std::any_of(o.host->panels.begin(), o.host->panels.end(), [&](const auto& v) { return v.id == p.id; }))
            return Fail(o, error, "extension panel id already exists");
        if (!std::any_of(o.host->actions.begin(), o.host->actions.end(),
                         [&](const auto& v) { return v.owner == &o && v.id == p.action; }))
            return Fail(o, error, "panel button needs an action owned by this extension");
        p.dockId = o.host->nextPanel++;
        if (!o.host->dock.Commands().RegisterToolPanel({.id = p.dockId,
                                                        .kind = DockPanelKind::Generic,
                                                        .title = p.title,
                                                        .area = DockArea::Right,
                                                        .visible = true,
                                                        .detachable = false}))
            return Fail(o, error, "could not register extension dock panel");
        o.host->panels.push_back(std::move(p));
        return 1;
    }
    static int SetText(void* user, const char* id, const char* text, const char** error) {
        auto& o = *static_cast<Owner*>(user);
        if (!Check(o, error))
            return 0;
        std::string key, value;
        if (!Copy(id, key, 96) || !Copy(text, value, 8192))
            return Fail(o, error, "invalid panel text");
        for (auto& p : o.host->panels)
            if (p.owner == &o && p.id == key) {
                p.text = std::move(value);
                return 1;
            }
        return Fail(o, error, "extension does not own this panel");
    }
    static int Request(void* user, const char* json, const char** response, const char** error) {
        auto& o = *static_cast<Owner*>(user);
        if (response)
            *response = nullptr;
        if (!Check(o, error))
            return 0;
        if (o.installing)
            return Fail(o, error, "authoring requests require a successfully installed extension");
        std::string input;
        if (!Copy(json, input, 1024 * 1024))
            return Fail(o, error, "invalid authoring JSON");
        try {
            if (!o.host->authoring.Request(input, o.response, o.error)) {
                if (error)
                    *error = o.error.c_str();
                return 0;
            }
            if (response)
                *response = o.response.c_str();
            return 1;
        } catch (const std::exception& e) {
            return Fail(o, error, e.what());
        } catch (...) {
            return Fail(o, error, "authoring callback raised an unknown exception");
        }
    }
    static void Log(void* user, const char* message) {
        auto& o = *static_cast<Owner*>(user);
        if (std::this_thread::get_id() != o.thread)
            return;
        std::string text;
        if (Copy(message, text, 4096))
            o.host->context.Console().Info("Extension", std::move(text));
    }
    bool Remove(Owner& owner, std::string& error) {
        owner.closing = true;
        std::vector<std::uint32_t> ids;
        for (const auto& p : panels)
            if (p.owner == &owner)
                ids.push_back(p.dockId);
        std::erase_if(actions, [&](const auto& a) { return a.owner == &owner; });
        std::erase_if(panels, [&](const auto& p) { return p.owner == &owner; });
        for (auto id : ids)
            static_cast<void>(dock.Commands().UnregisterToolPanel(id));
        bool okay = true;
        if (owner.desc.version == KB_EDITOR_EXTENSION_API_VERSION && owner.desc.size >= sizeof(KbEditorExtensionDesc) &&
            owner.desc.shutdown) {
            try {
                owner.desc.shutdown(owner.desc.user);
            } catch (...) {
                error = "extension shutdown raised an exception";
                okay = false;
            }
        }
#if defined(_WIN32)
        if (owner.module && !FreeLibrary(owner.module)) {
            error = "could not unload editor extension DLL";
            okay = false;
        }
        owner.module = nullptr;
#endif
        return okay;
    }
};
EditorExtensionHost::EditorExtensionHost(EditorSceneContext& c, EditorDockModel& d)
    : impl_(std::make_unique<Impl>(c, d)) {
    if (!Hosts().emplace(&c, this).second)
        throw std::logic_error("editor document already has an extension host");
}
EditorExtensionHost::~EditorExtensionHost() {
    if (std::this_thread::get_id() == impl_->thread) {
        while (!impl_->modules.empty()) {
            std::string error;
            static_cast<void>(impl_->Remove(*impl_->modules.back(), error));
            impl_->modules.pop_back();
        }
        Hosts().erase(&impl_->context);
    }
}
EditorExtensionHost* EditorExtensionHost::Find(EditorSceneContext& c) noexcept {
    auto i = Hosts().find(&c);
    return i == Hosts().end() ? nullptr : i->second;
}
bool EditorExtensionHost::Install(const std::filesystem::path& manifest, std::string& error) {
    error.clear();
    if (std::this_thread::get_id() != impl_->thread) {
        error = "extension install requires its owner thread";
        return false;
    }
#if !defined(_WIN32)
    static_cast<void>(manifest);
    error = "editor extension DLL loading is unavailable on this host";
    return false;
#else
    using J = kb::core::JsonValue;
    std::error_code ec;
    auto bytes = std::filesystem::file_size(manifest, ec);
    if (ec || bytes > 65536) {
        error = "cannot read extension manifest or manifest exceeds 64 KiB";
        return false;
    }
    std::ifstream input(manifest, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>{input}, {}};
    J j;
    if (!input || !J::Parse(text, j, error))
        return false;
    if (j.GetKind() != J::Kind::Object || j.Size() != 3) {
        error = "extension manifest needs schema, id and module";
        return false;
    }
    auto *schema = j.Find("schema"), *id = j.Find("id"), *module = j.Find("module");
    if (!schema || schema->GetKind() != J::Kind::Number || schema->AsNumber() != 1 || !id ||
        id->GetKind() != J::Kind::String || !Name(id->AsString()) || !module || module->GetKind() != J::Kind::String ||
        module->AsString().find('\0') != std::string::npos) {
        error = "invalid extension manifest schema or fields";
        return false;
    }
    if (impl_->modules.size() >= 8 || std::any_of(impl_->modules.begin(), impl_->modules.end(),
                                                  [&](const auto& o) { return o->id == id->AsString(); })) {
        error = "extension is already installed or module limit reached";
        return false;
    }
    auto relative = kb::assets::Utf8Path(module->AsString()).lexically_normal();
    if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." || relative.extension() != L".dll") {
        error = "extension module must be a project-relative DLL path";
        return false;
    }
    auto path = std::filesystem::weakly_canonical(EditorProjectPaths::ProjectRoot() / relative, ec);
    if (ec || !std::filesystem::is_regular_file(path)) {
        error = "extension module was not found";
        return false;
    }
    auto owner = std::make_unique<Impl::Owner>();
    owner->host = impl_.get();
    owner->thread = impl_->thread;
    owner->id = id->AsString();
    owner->api = {KB_EDITOR_EXTENSION_API_VERSION,
                  sizeof(KbEditorExtensionHost),
                  owner.get(),
                  &Impl::AddAction,
                  &Impl::AddPanel,
                  &Impl::SetText,
                  &Impl::Request,
                  &Impl::Log};
    owner->module =
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!owner->module) {
        error = "editor extension DLL loader error " + std::to_string(GetLastError());
        return false;
    }
    const auto entry =
        reinterpret_cast<KbEditorExtensionEntry>(GetProcAddress(owner->module, "kb_editor_extension_entry"));
    bool accepted = false;
    if (entry) {
        try {
            accepted = entry(&owner->api, &owner->desc) != 0;
        } catch (...) {
            error = "extension entry raised an exception";
        }
    } else
        error = "editor extension entry point was not found";
    owner->installing = false;
    if (!accepted || owner->desc.version != KB_EDITOR_EXTENSION_API_VERSION ||
        owner->desc.size < sizeof(KbEditorExtensionDesc) || !owner->desc.id || owner->id != owner->desc.id ||
        !owner->desc.shutdown) {
        if (error.empty())
            error = !owner->error.empty() ? owner->error
                                          : "extension entry rejected or SDK version/descriptor is incompatible";
        std::string cleanup;
        static_cast<void>(impl_->Remove(*owner, cleanup));
        return false;
    }
    impl_->context.Console().Info("Extension", "Installed " + owner->id);
    impl_->modules.push_back(std::move(owner));
    return true;
#endif
}
bool EditorExtensionHost::Uninstall(std::string_view id, std::string& error) {
    error.clear();
    if (std::this_thread::get_id() != impl_->thread) {
        error = "extension unload requires its owner thread";
        return false;
    }
    auto i = std::find_if(impl_->modules.begin(), impl_->modules.end(), [&](const auto& o) { return o->id == id; });
    if (i == impl_->modules.end()) {
        error = "extension is not installed";
        return false;
    }
    if ((*i)->activeCalls) {
        error = "cannot unload an executing extension callback";
        return false;
    }
    const bool ok = impl_->Remove(**i, error);
    impl_->modules.erase(i);
    impl_->context.Console().Info("Extension", "Unloaded " + std::string{id});
    return ok;
}
bool EditorExtensionHost::Invoke(std::string_view id, std::string& error) {
    error.clear();
    if (std::this_thread::get_id() != impl_->thread) {
        error = "extension action requires its owner thread";
        return false;
    }
    auto i = std::find_if(impl_->actions.begin(), impl_->actions.end(), [&](const auto& a) { return a.id == id; });
    if (i == impl_->actions.end()) {
        error = "extension action was not found";
        return false;
    }
    const auto action = *i;
    auto& o = *action.owner;
    if (o.closing) {
        error = "extension is unloading";
        return false;
    }
    ++o.activeCalls;
    bool okay = false;
    const char* detail = nullptr;
    try {
        okay = action.callback(action.user, &o.api, &detail) != 0;
        if (!okay)
            error = detail ? detail : "extension action failed";
    } catch (...) {
        error = "extension action raised an exception";
    }
    --o.activeCalls;
    return okay;
}
bool EditorExtensionHost::LoadProject(std::string& error) {
    auto folder = EditorProjectPaths::ProjectRoot() / "Editor" / "Extensions";
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec))
        return true;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file() && it->path().extension() == ".kbeditorext")
            files.push_back(it->path());
    if (ec) {
        error = ec.message();
        return false;
    }
    std::sort(files.begin(), files.end());
    for (auto& file : files)
        if (!Install(file, error))
            return false;
    return true;
}
std::size_t EditorExtensionHost::ModuleCount() const {
    return impl_->modules.size();
}
std::size_t EditorExtensionHost::ActionCount() const {
    return impl_->actions.size();
}
std::size_t EditorExtensionHost::PanelCount() const {
    return impl_->panels.size();
}
std::uint32_t EditorExtensionHost::PanelId(std::string_view id) const {
    for (auto& p : impl_->panels)
        if (id.empty() || p.id == id)
            return p.dockId;
    return 0;
}
std::string EditorExtensionHost::PanelText(std::string_view id) const {
    for (auto& p : impl_->panels)
        if (p.id == id)
            return p.text;
    return {};
}
#if defined(_WIN32)
RECT EditorExtensionHost::ButtonRect(const RECT& b) noexcept {
    return {b.left + 16, b.top + 136, std::min(b.right - 16, b.left + 260), b.top + 172};
}
bool EditorExtensionHost::Paint(HDC dc, const RECT& bounds, std::uint32_t id, const EditorTheme& theme) {
    auto i = std::find_if(impl_->panels.begin(), impl_->panels.end(), [&](auto& p) { return p.dockId == id; });
    if (i == impl_->panels.end())
        return false;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    SetBkMode(dc, TRANSPARENT);
    auto fill = [&](RECT rect, EditorColor c) {
        HBRUSH brush = CreateSolidBrush(RGB(c.r, c.g, c.b));
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    };
    fill(bounds, theme.panel);
    HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (font)
        SelectObject(dc, font);
    SetTextColor(dc, RGB(theme.textPrimary.r, theme.textPrimary.g, theme.textPrimary.b));
    RECT title{bounds.left + 16, bounds.top + 16, bounds.right - 16, bounds.top + 40};
    DrawTextUtf8(dc, i->title.c_str(), -1, &title, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT body{bounds.left + 16, bounds.top + 48, bounds.right - 16, bounds.top + 124};
    DrawTextUtf8(dc, i->text.c_str(), -1, &body, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    auto button = ButtonRect(bounds);
    fill(button, theme.accent);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextUtf8(dc, i->button.c_str(), -1, &button, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RestoreDC(dc, saved);
    if (font)
        DeleteObject(font);
    return true;
}
bool EditorExtensionHost::PointerDown(const RECT& bounds, std::uint32_t id, int x, int y, std::string& error) {
    error.clear();
    const auto button = ButtonRect(bounds);
    if (x < bounds.left || x >= bounds.right || y < bounds.top || y >= bounds.bottom || x < button.left ||
        x >= button.right || y < button.top || y >= button.bottom)
        return false;
    for (const auto& p : impl_->panels)
        if (p.dockId == id) {
            const auto action = p.action;
            return Invoke(action, error);
        }
    return false;
}
#endif
} // namespace kb::editor
