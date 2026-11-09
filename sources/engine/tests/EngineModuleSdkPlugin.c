#include "engine/modules/EngineModuleSdk.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(KbModuleError) == 256, "fixed error buffer");
_Static_assert(offsetof(KbModuleHostApi, context) == 8, "host API prefix");

typedef struct Module {
    KbModuleHostApi host;
    KbModuleScene previous_scene;
    KbModuleEntity previous_entity;
} Module;
typedef struct TickData {
    Module* module;
    KbModuleEntity entity;
    KbModuleRegistration registration;
} TickData;

static void trace(const char* text) {
    FILE* file = fopen(KB_SDK_TRACE, "a");
    if (file) { fprintf(file, "%s\n", text); fclose(file); }
}
static void trace_registration(KbModuleRegistration registration) {
    FILE* file = fopen(KB_SDK_TRACE, "a");
    if (file) { fprintf(file, "registration %llu\n", (unsigned long long)registration); fclose(file); }
}
static KbModuleStatus KB_MODULE_CALL create_module(const KbModuleHostApi* host, void** output, KbModuleError* error) {
    Module* module;
    if (!host || host->size != sizeof(*host) || host->version != KB_MODULE_SDK_VERSION || !output)
        return KB_MODULE_UNSUPPORTED_VERSION;
    module = (Module*)calloc(1, sizeof(*module));
    if (!module) return KB_MODULE_INTERNAL_ERROR;
    module->host = *host;
    *output = module;
    trace("create");
#if KB_SDK_FAULT == 3
    if (error) strcpy(error->message, "fixture create refused");
    return KB_MODULE_INTERNAL_ERROR;
#else
    (void)error;
    return KB_MODULE_OK;
#endif
}
static void KB_MODULE_CALL destroy_module(void* instance) {
    trace("destroy");
    free(instance);
}
static void KB_MODULE_CALL release_tick(void* user) {
    trace("release");
    free(user);
}
static KbModuleStatus KB_MODULE_CALL tick(void* user, KbModuleScene scene, float delta, KbModuleError* error) {
    TickData* data = (TickData*)user;
    KbModuleHostApi* host = &data->module->host;
    KbModuleVec3 position;
    KbModuleStatus result = host->unregister_tick(host->context, data->registration, error);
    if (result != KB_MODULE_BUSY) return KB_MODULE_INTERNAL_ERROR;
    result = host->get_position(host->context, scene, data->entity, &position, error);
    if (result != KB_MODULE_OK) return result;
    position.x += delta;
    return host->set_position(host->context, scene, data->entity, position, error);
}
static KbModuleStatus KB_MODULE_CALL attach(void* instance, KbModuleScene scene, KbModuleError* error) {
    Module* module = (Module*)instance;
    TickData* data;
    KbModuleStatus result;
    KbModuleString name = { "SdkObject", 9 };
    if (module->previous_scene && module->previous_scene != scene) {
        KbModuleVec3 position;
        result = module->host.get_position(module->host.context, scene, module->previous_entity, &position, error);
        if (result != KB_MODULE_STALE_HANDLE) return KB_MODULE_INTERNAL_ERROR;
        trace("foreign entity refused");
    }
    data = (TickData*)calloc(1, sizeof(*data));
    if (!data) return KB_MODULE_INTERNAL_ERROR;
    data->module = module;
    result = module->host.create_entity(module->host.context, scene, name, &data->entity, error);
    if (result != KB_MODULE_OK) { free(data); return result; }
    result = module->host.register_tick(module->host.context, scene, tick, data, release_tick, &data->registration, error);
    if (result != KB_MODULE_OK) { free(data); return result; }
    module->previous_scene = scene;
    module->previous_entity = data->entity;
    trace_registration(data->registration);
#if KB_SDK_FAULT == 4
    if (error) strcpy(error->message, "fixture attach refused");
    return KB_MODULE_INTERNAL_ERROR;
#else
    return KB_MODULE_OK;
#endif
}
/* Deliberately leave callbacks and entities registered: the engine owns their
   scope and must release them before destroy, including partial attach failure. */
static void KB_MODULE_CALL detach(void* instance, KbModuleScene scene) {
    (void)instance; (void)scene;
    trace("detach");
}

KB_MODULE_SDK_EXPORT KbModuleStatus KB_MODULE_CALL kb_get_engine_module_api(
    uint32_t version, uint32_t size, KbModuleApi* output, KbModuleError* error) {
    (void)error;
    if (version != KB_MODULE_SDK_VERSION || size != sizeof(*output) || !output)
        return KB_MODULE_UNSUPPORTED_VERSION;
    memset(output, 0, sizeof(*output));
    output->size = sizeof(*output);
    output->version = KB_MODULE_SDK_VERSION;
#if KB_SDK_FAULT == 2
    output->version += 1;
#endif
    output->pointer_size = sizeof(void*);
    output->module_version = 1;
    output->name.data = "Tests.PublicSdk";
    output->name.size = 15;
    output->loading_phase = 2;
    output->create = create_module;
    output->destroy = destroy_module;
    output->attach = attach;
    output->detach = detach;
    return KB_MODULE_OK;
}
