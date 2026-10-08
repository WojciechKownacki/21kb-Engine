// Save games and user settings, decoded with the shipped migrations.
#include "FuzzSupport.hpp"

#include "save/SaveGameCodec.hpp"
#include "save/SaveGameFormat.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    for (const kb::save::SaveDomain domain : { kb::save::SaveDomain::SaveGame, kb::save::SaveDomain::UserSettings }) {
        static_cast<void>(kb::save::SaveGameCodec::Decode(kb::fuzz::Bytes(data, size),
            kb::save::SaveGameFormat::kCurrentSchemaVersion, domain, kb::save::BuiltInSaveGameMigrations()));
    }
    return 0;
}
