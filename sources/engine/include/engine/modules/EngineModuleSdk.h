#ifndef KB_ENGINE_MODULE_SDK_H
#define KB_ENGINE_MODULE_SDK_H

#include <stdint.h>

#if defined(_WIN32)
#define KB_MODULE_CALL __cdecl
#define KB_MODULE_EXPORT_ATTRIBUTE __declspec(dllexport)
#else
#define KB_MODULE_CALL
#define KB_MODULE_EXPORT_ATTRIBUTE __attribute__((visibility("default")))
#endif
#if defined(__cplusplus)
#define KB_MODULE_SDK_EXPORT extern "C" KB_MODULE_EXPORT_ATTRIBUTE
#else
#define KB_MODULE_SDK_EXPORT KB_MODULE_EXPORT_ATTRIBUTE
#endif

#define KB_MODULE_SDK_VERSION 1u
#define KB_MODULE_SDK_ENTRY "kb_get_engine_module_api"

typedef int32_t KbModuleStatus;
#define KB_MODULE_OK ((KbModuleStatus)0)
#define KB_MODULE_INVALID_ARGUMENT ((KbModuleStatus)1)
#define KB_MODULE_UNSUPPORTED_VERSION ((KbModuleStatus)2)
#define KB_MODULE_STALE_HANDLE ((KbModuleStatus)3)
#define KB_MODULE_WRONG_THREAD ((KbModuleStatus)4)
#define KB_MODULE_BUSY ((KbModuleStatus)5)
#define KB_MODULE_INTERNAL_ERROR ((KbModuleStatus)6)

typedef uint64_t KbModuleScene;
typedef uint64_t KbModuleEntity;
typedef uint64_t KbModuleRegistration;

/* Borrowed UTF-8 bytes. The recipient copies data it needs after the call. */
typedef struct KbModuleString {
    const char* data;
    uint32_t size;
} KbModuleString;

/* Caller-owned output; always initialize to zero before a call. */
typedef struct KbModuleError { char message[256]; } KbModuleError;
typedef struct KbModuleVec3 { float x, y, z; } KbModuleVec3;

typedef KbModuleStatus (KB_MODULE_CALL *KbModuleTick)(
    void* user, KbModuleScene scene, float delta_seconds, KbModuleError* error);
typedef void (KB_MODULE_CALL *KbModuleRelease)(void* user);

/* Owner-thread API. No exception, STL object or allocator crosses this boundary.
   Context and table remain valid until the module destroy callback returns.
   A scene handle is valid only between attach and detach for this module.
   Entity handles must be used with the scene that created them. SDK-created
   entities are destroyed automatically after detach or a failed attach. */
typedef struct KbModuleHostApi {
    uint32_t size;
    uint32_t version;
    void* context;
    KbModuleStatus (KB_MODULE_CALL *create_entity)(void*, KbModuleScene, KbModuleString, KbModuleEntity*, KbModuleError*);
    KbModuleStatus (KB_MODULE_CALL *destroy_entity)(void*, KbModuleScene, KbModuleEntity, KbModuleError*);
    KbModuleStatus (KB_MODULE_CALL *get_position)(void*, KbModuleScene, KbModuleEntity, KbModuleVec3*, KbModuleError*);
    KbModuleStatus (KB_MODULE_CALL *set_position)(void*, KbModuleScene, KbModuleEntity, KbModuleVec3, KbModuleError*);
    /* Registration is allowed during attach. Ownership of user transfers only on
       success. release_user runs exactly once before the module library closes.
       The engine removes registrations even if detach omits explicit cleanup. */
    KbModuleStatus (KB_MODULE_CALL *register_tick)(void*, KbModuleScene, KbModuleTick, void*, KbModuleRelease,
        KbModuleRegistration*, KbModuleError*);
    KbModuleStatus (KB_MODULE_CALL *unregister_tick)(void*, KbModuleRegistration, KbModuleError*);
} KbModuleHostApi;

typedef KbModuleStatus (KB_MODULE_CALL *KbModuleLifecycle)(void*, KbModuleError*);
typedef KbModuleStatus (KB_MODULE_CALL *KbModuleSceneLifecycle)(void*, KbModuleScene, KbModuleError*);
typedef void (KB_MODULE_CALL *KbModuleSceneRelease)(void*, KbModuleScene);

/* Name/dependency views belong to the library and are copied before create.
   create/destroy are mandatory; other hooks are optional. create transfers any
   non-null output instance even on failure, so destroy can clean partial state.
   Fallible callbacks return status; cleanup callbacks must succeed. No callback
   may throw. Memory is freed by its allocator's
   release callback, including when host and plugin use different C runtimes. */
typedef struct KbModuleApi {
    uint32_t size;
    uint32_t version;
    uint32_t pointer_size;
    uint32_t module_version;
    KbModuleString name;
    const KbModuleString* dependencies;
    uint32_t dependency_count;
    uint32_t loading_phase; /* 0 earliest, 1 pre-default, 2 default, 3 post-default */
    KbModuleStatus (KB_MODULE_CALL *create)(const KbModuleHostApi*, void**, KbModuleError*);
    KbModuleRelease destroy;
    KbModuleLifecycle load;
    KbModuleLifecycle enable;
    KbModuleSceneLifecycle attach;
    KbModuleSceneRelease detach;
    KbModuleRelease disable;
    KbModuleRelease unload;
} KbModuleApi;

/* Export KB_MODULE_SDK_ENTRY with this signature. Reject unknown versions or
   sizes without writing beyond the caller's buffer. Version 1 uses exact size. */
typedef KbModuleStatus (KB_MODULE_CALL *KbModuleGetApi)(
    uint32_t requested_version, uint32_t output_size, KbModuleApi* output, KbModuleError* error);

#endif
