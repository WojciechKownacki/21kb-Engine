// Save games and user settings, decoded with the shipped migrations.
#include "FuzzSupport.hpp"

#include "engine/save/SaveGameService.hpp"
#include "save/SaveGameCodec.hpp"
#include "save/SaveGameFormat.hpp"

// Authenticated saves are checked against the development key; inputs in the older, unauthenticated
// schemas reach the payload decoder and the migrations without one.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static const kb::save::SaveGameIntegrity integrity = kb::save::DevelopmentSaveGameIntegrity();
    for (const kb::save::SaveDomain domain : { kb::save::SaveDomain::SaveGame, kb::save::SaveDomain::UserSettings }) {
        static_cast<void>(kb::save::SaveGameCodec::Decode(kb::fuzz::Bytes(data, size),
            kb::save::SaveGameFormat::kCurrentSchemaVersion, domain, kb::save::BuiltInSaveGameMigrations(), integrity));
    }
    return 0;
}
