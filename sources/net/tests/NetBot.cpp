#include "DedicatedServer.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/security/Crypto.hpp"
#include "kb/net/ByteIO.hpp"
#include "kb/net/SceneReplication.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
using namespace kb::net;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

void Require(bool ok, const std::string& error) {
    if (!ok) throw std::runtime_error(error);
}

void MakeKeys(std::size_t count, const fs::path& path) {
    std::ofstream out(path);
    Require(out.good(), "cannot create keys file");
    std::set<std::uint64_t> ids;
    while (ids.size() < count) {
        std::array<std::uint8_t, 8> bytes{};
        SessionKey key{};
        Require(kb::security::SecureRandom(bytes) && kb::security::SecureRandom(key), "secure random source unavailable");
        ByteReader reader(bytes);
        const auto id = reader.U64();
        if (id <= 1U || !ids.insert(id).second) continue;
        out << id << ' ' << kb::security::ToHex(key) << '\n';
        kb::security::SecureWipe(key);
    }
    Require(out.good(), "cannot write keys file");
#if !defined(_WIN32)
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
#endif
}

void MakeProject(const fs::path& root, std::size_t count) {
    Require(!fs::exists(root / "Project.21kbproject"), "project already exists");
    fs::create_directories(root / "Assets/Scenes");
    fs::create_directories(root / "Assets/Logic");
    Require(kb::project::ProjectManager::SaveProject(root / "Project.21kbproject", {}), "cannot save project");
    kb::project::ProjectSettings settings;
    settings.name = "NetworkSample";
    settings.gameName = settings.name;
    settings.defaultMap = "/Game/Scenes/Main.21kbscene";
    std::string error;
    Require(kb::project::ProjectSettingsStore::Save(kb::project::ProjectSettingsStore::FilePath(root), settings, error), error);
    {
        std::ofstream script(root / "Assets/Logic/Mover.lua");
        script << R"lua(local elapsed = 0
function Tick(self, dt)
    elapsed = elapsed + dt
    Transform.SetPosition(self.entity, 100 * math.cos(elapsed), 0, 100 * math.sin(elapsed))
end
)lua";
        Require(script.good(), "cannot write movement script");
    }
    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root) && scene.Assets().Discover() != 0U, "cannot mount project assets");
    const auto* script = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Mover.lua");
    Require(script != nullptr, "movement script not found");
    for (std::size_t index = 0; index < count; ++index) {
        const auto object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Net_Npc_" + std::to_string(index) });
        scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
            .behaviourAssetId = script->id.value, .backend = kb::scene::BehaviourBackend::Lua, .enabled = true });
    }
    Require(kb::scene::SceneDocumentService::Save(scene, root / "Assets/Scenes/Main.21kbscene", "Main"), "cannot save scene");
}

struct Bot {
    kb::ecs::World world;
    std::unique_ptr<IDatagramTransport> transport;
    std::unique_ptr<Client> client;
    std::unique_ptr<ReplicationClient> replication;
    std::uint64_t previousBytes = 0U;
};

int Run(const std::map<std::string, std::string>& options) {
    const auto number = [&](const std::string& key, std::size_t fallback, std::size_t limit) {
        const auto found = options.find(key);
        if (found == options.end()) return fallback;
        std::size_t consumed = 0;
        const auto value = std::stoull(found->second, &consumed);
        Require(consumed == found->second.size() && value > 0U && value <= limit, "invalid " + key);
        return static_cast<std::size_t>(value);
    };
    if (options.contains("--make-keys")) {
        MakeKeys(number("--make-keys", 1U, 4096U), options.at("--keys-out"));
        return 0;
    }
    if (options.contains("--make-project")) {
        MakeProject(options.at("--make-project"), number("--entities", 1000U, 1'000'001U));
        return 0;
    }
    const auto count = number("--clients", 1U, 4096U);
    const auto seconds = number("--seconds", 20U, 86400U);
    const std::string address = options.at("--server");
    const auto colon = address.find(':');
    Require(colon != std::string::npos, "server must be an IPv4 address and port");
    Endpoint endpoint;
    std::istringstream octets(address.substr(0, colon));
    std::string part;
    for (int i = 0; i < 4; ++i) {
        Require(static_cast<bool>(std::getline(octets, part, '.')), "invalid server address");
        const auto value = std::stoul(part);
        Require(value <= 255U, "invalid server address");
        endpoint.address = (endpoint.address << 8U) | static_cast<std::uint32_t>(value);
    }
    const auto port = std::stoul(address.substr(colon + 1));
    Require(port > 0U && port <= 65535U, "invalid server port");
    endpoint.port = static_cast<std::uint16_t>(port);
    std::ifstream keysFile(options.at("--keys"));
    std::ostringstream keyText;
    keyText << keysFile.rdbuf();
    Require(keysFile.good() || keysFile.eof(), "cannot read keys");
    StaticKeyTable keys;
    std::size_t keyCount = 0U;
    std::string error;
    Require(kb::net::server::ParseKeysFile(keyText.str(), keys, keyCount, error), error);
    Require(keyCount >= count, "not enough client keys");
    std::vector<std::uint64_t> ids;
    std::istringstream lines(keyText.str());
    for (std::string line; std::getline(lines, line);) {
        std::istringstream fields(line);
        std::uint64_t id;
        if (fields >> id) ids.push_back(id);
    }
    const kb::ecs::World registryWorld;
    auto components = BuildReplicationRegistry(registryWorld, {}, 1000.0F, error);
    Require(components != nullptr, "cannot create component registry");
    RpcRegistry rpcs;
    DefineStandardRpcs(rpcs);
    std::vector<std::unique_ptr<Bot>> bots;
    const auto started = Clock::now();
    for (std::size_t i = 0; i < count; ++i) {
        auto bot = std::make_unique<Bot>();
        bot->transport = CreateUdpTransport(Endpoint{}, error);
        Require(bot->transport != nullptr, error);
        SessionKey key{};
        Require(keys.LookupKey(ids[i], key), "missing client key");
        ClientConfig config;
        config.connectTimeoutMilliseconds = 30'000U;
        bot->client = std::make_unique<Client>(*bot->transport, config, ids[i], key, endpoint, 0U);
        kb::security::SecureWipe(key);
        bot->replication = std::make_unique<ReplicationClient>(*bot->client, bot->world, *components, rpcs);
        bots.push_back(std::move(bot));
    }
    std::ofstream csv(options.at("--csv"));
    Require(csv.good(), "cannot create bot CSV");
    csv << "second,client,replicas,state_msgs,bytes_per_second,rtt_ms\n";
    std::uint64_t nextReport = 1000U;
    for (;;) {
        const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count());
        for (auto& bot : bots) {
            bot->client->Update(now);
            NetEvent event;
            while (bot->client->PollEvent(event)) bot->replication->HandleEvent(event);
            bot->replication->Flush(now);
        }
        if (now >= nextReport) {
            std::size_t minimum = bots.front()->replication->ReplicaCount();
            for (std::size_t i = 0; i < bots.size(); ++i) {
                auto& bot = *bots[i];
                const auto* stats = bot.client->Stats();
                const auto bytes = stats == nullptr ? 0U : stats->bytesAccepted;
                csv << now / 1000U << ',' << i << ',' << bot.replication->ReplicaCount() << ',' << bot.replication->Stats().stateMessagesApplied
                    << ',' << bytes - bot.previousBytes << ',' << (stats == nullptr ? 0U : stats->smoothedRttMilliseconds) << '\n';
                bot.previousBytes = bytes;
                minimum = std::min(minimum, bot.replication->ReplicaCount());
            }
            csv.flush();
            std::cout << "second=" << now / 1000U << " clients=" << count << " replicas_min=" << minimum << '\n';
            nextReport += 1000U;
        }
        if (now >= seconds * 1000U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(std::ranges::all_of(bots, [](const auto& bot) { return bot->client->State() == ClientState::Connected && bot->replication->Ready(); }),
        "some bots did not connect or agree on replication");
    return csv.good() ? 0 : 1;
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::map<std::string, std::string> options;
        const std::set<std::string> allowed{ "--make-keys", "--keys-out", "--make-project", "--entities", "--server", "--keys", "--clients", "--seconds", "--csv" };
        for (int i = 1; i < argc; i += 2) {
            Require(allowed.contains(argv[i]) && i + 1 < argc, "unknown option or missing value");
            Require(options.emplace(argv[i], argv[i + 1]).second, "duplicate option");
        }
        return Run(options);
    } catch (const std::exception& error) {
        std::cerr << "net bot: " << error.what() << '\n';
        return 1;
    }
}
