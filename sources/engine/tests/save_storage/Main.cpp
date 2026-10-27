#include "TestSupport.hpp"
#include "TestSuites.hpp"
#include "engine/platform/UserStorage.hpp"
#include "engine/save/SaveGameChunkStore.hpp"

#include <array>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc == 1) {
        kb::tests::RunSaveGameTests();
        std::puts("save storage tests passed");
        return 0;
    }
    if (argc != 3) return 2;
    kb::platform::UserStorage storage(argv[2], 64U << 20U);
    kb::save::SaveGameChunkStore chunks(storage, "Persistent");
    if (std::string_view{ argv[1] } == "write") {
        kb::save::SaveGame save;
        save.SetInt("generation", 42);
        save.SetString("text", "persistent data");
        const auto bytes = kb::save::SaveGameService::Serialize(save);
        kb::tests::Require(bytes.has_value() && storage.Write("single.kbsave",
            { reinterpret_cast<const char*>(bytes->data()), bytes->size() }), "Persistent single save failed");
        const std::array updates{ kb::save::SaveGameChunkUpdate{ "Region", &save } };
        kb::tests::Require(chunks.Commit(updates), "Persistent chunk save failed");
        std::puts("persistent write passed");
        return 0;
    }
    if (std::string_view{ argv[1] } != "read") return 2;
    const auto bytes = storage.ReadBounded("single.kbsave", kb::save::SaveGameService::MaxSerializedBytes);
    kb::tests::Require(bytes.has_value(), "Persistent single save missing after process restart");
    const auto single = kb::save::SaveGameService::Deserialize(
        { reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size() });
    const auto chunk = chunks.Load(chunks.Open(), "Region");
    std::int64_t generation = 0;
    std::string text;
    kb::tests::Require(single.Succeeded() && single.save.GetInt("generation", generation) && generation == 42 &&
        chunk.Succeeded() && chunk.save.GetInt("generation", generation) && generation == 42 &&
        chunk.save.GetString("text", text) && text == "persistent data", "Process restart changed persistent save data");
    std::puts("persistent reload passed");
    return 0;
}
