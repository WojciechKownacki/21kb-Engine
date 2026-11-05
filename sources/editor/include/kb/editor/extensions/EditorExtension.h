#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KB_EDITOR_EXTENSION_API_VERSION 1u
#if defined(_WIN32)
#define KB_EDITOR_EXTENSION_EXPORT __declspec(dllexport)
#else
#define KB_EDITOR_EXTENSION_EXPORT __attribute__((visibility("default")))
#endif
struct KbEditorExtensionHost;
typedef int (*KbEditorActionCallback)(void* user,const struct KbEditorExtensionHost* host,const char** error);
typedef struct KbEditorActionDesc { const char* id; const char* label; void* user; KbEditorActionCallback invoke; } KbEditorActionDesc;
// A minimal dock panel: UTF-8 paragraph and one action button. Host copies all strings.
typedef struct KbEditorPanelDesc { const char* id; const char* title; const char* text; const char* actionId; const char* buttonLabel; } KbEditorPanelDesc;
typedef struct KbEditorExtensionHost {
    uint32_t version;
    uint32_t size;
    void* user;
    int (*registerAction)(void*,const KbEditorActionDesc*,const char** error);
    int (*registerPanel)(void*,const KbEditorPanelDesc*,const char** error);
    int (*setPanelText)(void*,const char* panelId,const char* text,const char** error);
    // JSON authoring contract from EditorAuthoring.hpp. Returned UTF-8 pointers
    // are borrowed until the next request through this host. Copy if retained.
    int (*request)(void*,const char* json,const char** response,const char** error);
    void (*log)(void*,const char* message);
} KbEditorExtensionHost;
typedef struct KbEditorExtensionDesc {
    uint32_t version;
    uint32_t size;
    const char* id;
    void* user;
    // Set before registering resources. Called once on unload/failed install,
    // after callbacks/panels are removed and while this DLL is still loaded.
    void (*shutdown)(void*);
} KbEditorExtensionDesc;
typedef int (*KbEditorExtensionEntry)(const KbEditorExtensionHost*,KbEditorExtensionDesc*);
// Export: int kb_editor_extension_entry(const KbEditorExtensionHost*,KbEditorExtensionDesc*).
// Construct/install/invoke/unload on the editor owner thread. Join workers and
// release module-owned resources in shutdown; no host pointer survives unload.
#ifdef __cplusplus
}
#endif
