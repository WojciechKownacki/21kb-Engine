#include "ModuleTypeFixture.hpp"
#include "engine/modules/EngineModuleSdk.h"

#include <typeinfo>

// Legacy C++ type identity fixture; the public C SDK never exports RTTI.
KB_MODULE_SDK_EXPORT const std::type_info* KB_MODULE_CALL kb_test_module_type(unsigned index) {
    switch (index) {
    case 0: return &typeid(kb::tests::ModuleValue);
    case 1: return &typeid(kb::tests::ModuleTag);
    case 2: return &typeid(kb::tests::ModuleRelation);
    default: return nullptr;
    }
}
