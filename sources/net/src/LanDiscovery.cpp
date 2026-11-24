#include "kb/net/LanDiscovery.hpp"

#include "kb/net/ByteIO.hpp"

#include <algorithm>
#include <array>

namespace kb::net {

namespace {

constexpr std::array<std::uint8_t, 4U> kMagic{ '2', '1', 'K', 'B' };
constexpr std::uint8_t kQuery = 1U;
constexpr std::uint8_t kAnnounce = 2U;
constexpr std::uint8_t kPasswordFlag = 1U;
constexpr std::size_t kHeaderBytes = 10U;
constexpr std::size_t kAnnounceFixedBytes = 34U;
constexpr std::size_t kAnswersPerSecond = 10U;

void PadTo(std::vector<std::uint8_t>& out) {
    if (out.size() < kLanDiscoveryDatagramBytes) {
        out.insert(out.end(), kLanDiscoveryDatagramBytes - out.size(), std::uint8_t{ 0U });
    }
}

[[nodiscard]] std::vector<std::uint8_t> BuildQuery(std::uint32_t nonce) {
    std::vector<std::uint8_t> out;
    out.reserve(kLanDiscoveryDatagramBytes);
    ByteWriter writer(out);
    writer.Bytes(kMagic);
    writer.U8(kLanDiscoveryProtocol);
    writer.U8(kQuery);
    writer.U32(nonce);
    PadTo(out);
    return out;
}

[[nodiscard]] std::string CutUtf8(std::string text) {
    if (text.size() <= kLanGameNameBytes) {
        return text;
    }
    std::size_t cut = kLanGameNameBytes;
    while (cut > 0U && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    text.resize(cut);
    return text;
}

// Reads the common header. False for a datagram that is not ours or is too short to hold the type.
[[nodiscard]] bool ReadHeader(std::span<const std::uint8_t> datagram, std::uint8_t& type, std::uint32_t& nonce) {
    if (datagram.size() < kHeaderBytes) {
        return false;
    }
    ByteReader reader(datagram);
    const auto magic = reader.Bytes(kMagic.size());
    if (!reader.Ok() || !std::equal(magic.begin(), magic.end(), kMagic.begin())) {
        return false;
    }
    if (reader.U8() != kLanDiscoveryProtocol) {
        return false;
    }
    type = reader.U8();
    nonce = reader.U32();
    return reader.Ok();
}

// Reads an announce body into `game`. False for a truncated or malformed one.
[[nodiscard]] bool ReadAnnounce(std::span<const std::uint8_t> datagram, LanGame& game) {
    if (datagram.size() < kAnnounceFixedBytes) {
        return false;
    }
    ByteReader reader(datagram.subspan(kHeaderBytes));
    game.gameKey = reader.U64();
    game.port = reader.U16();
    game.players = reader.U16();
    game.maxPlayers = reader.U16();
    game.passwordRequired = (reader.U8() & kPasswordFlag) != 0U;
    game.hostSalt = reader.U64();
    const std::uint8_t nameBytes = reader.U8();
    if (!reader.Ok() || nameBytes > kLanGameNameBytes) {
        return false;
    }
    const auto name = reader.Bytes(nameBytes);
    if (!reader.Ok()) {
        return false;
    }
    game.name.assign(reinterpret_cast<const char*>(name.data()), name.size());
    return true;
}

} // namespace

LanBeacon::LanBeacon(IDatagramTransport& transport, LanBeaconOptions options)
    : transport_(transport), options_(std::move(options)), players_(options_.maxPlayers) {
    options_.name = CutUtf8(std::move(options_.name));
    targets_ = options_.announceTargets.empty() ? ListBroadcastEndpoints(kLanDiscoveryPort) : std::move(options_.announceTargets);
}

void LanBeacon::Announce(const Endpoint& to, std::uint32_t nonce, std::uint64_t nowMilliseconds) {
    std::erase_if(answers_, [nowMilliseconds](std::uint64_t sent) { return sent + 1000U <= nowMilliseconds; });
    if (answers_.size() >= kAnswersPerSecond) {
        return;
    }
    announce_.clear();
    ByteWriter writer(announce_);
    writer.Bytes(kMagic);
    writer.U8(kLanDiscoveryProtocol);
    writer.U8(kAnnounce);
    writer.U32(nonce);
    writer.U64(options_.gameKey);
    writer.U16(options_.hostPort);
    writer.U16(players_);
    writer.U16(options_.maxPlayers);
    writer.U8(options_.passwordRequired ? kPasswordFlag : 0U);
    writer.U64(options_.hostSalt);
    writer.U8(static_cast<std::uint8_t>(options_.name.size()));
    writer.Bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(options_.name.data()), options_.name.size()));
    PadTo(announce_);
    static_cast<void>(transport_.Send(to, announce_));
    answers_.push_back(nowMilliseconds);
}

void LanBeacon::Update(std::uint64_t nowMilliseconds) {
    if (!started_) {
        started_ = true;
        nextAnnounceAt_ = nowMilliseconds;
    }
    if (nowMilliseconds >= nextAnnounceAt_) {
        for (const Endpoint& target : targets_) {
            Announce(target, ++nonce_, nowMilliseconds);
        }
        nextAnnounceAt_ = nowMilliseconds + kLanBeaconIntervalMilliseconds;
    }
    std::array<std::uint8_t, kMaxDatagramBytes + 1U> buffer{};
    for (int attempt = 0; attempt < 64; ++attempt) {
        Endpoint from;
        std::size_t size = 0U;
        if (transport_.Receive(from, buffer, size) != TransportStatus::Ok) {
            break;
        }
        std::uint8_t type = 0U;
        std::uint32_t nonce = 0U;
        if (!ReadHeader(std::span<const std::uint8_t>(buffer.data(), size), type, nonce) || type != kQuery) {
            continue;
        }
        Announce(from, nonce, nowMilliseconds);
    }
}

LanBrowser::LanBrowser(IDatagramTransport& transport, std::uint64_t gameKey)
    : LanBrowser(transport, gameKey, ListBroadcastEndpoints(kLanDiscoveryPort)) {}

LanBrowser::LanBrowser(IDatagramTransport& transport, std::uint64_t gameKey, std::vector<Endpoint> queryTargets)
    : transport_(transport), gameKey_(gameKey), targets_(std::move(queryTargets)) {}

void LanBrowser::Query(std::uint64_t nowMilliseconds) {
    std::vector<std::uint8_t> datagram = BuildQuery(++nonce_);
    for (const Endpoint& target : targets_) {
        static_cast<void>(transport_.Send(target, datagram));
    }
    nextQueryAt_ = nowMilliseconds + kLanBrowserIntervalMilliseconds;
}

void LanBrowser::Update(std::uint64_t nowMilliseconds) {
    if (!started_) {
        started_ = true;
        nextQueryAt_ = nowMilliseconds;
    }
    if (nowMilliseconds >= nextQueryAt_) {
        Query(nowMilliseconds);
    }
    std::array<std::uint8_t, kMaxDatagramBytes + 1U> buffer{};
    for (int attempt = 0; attempt < 256; ++attempt) {
        Endpoint from;
        std::size_t size = 0U;
        if (transport_.Receive(from, buffer, size) != TransportStatus::Ok) {
            break;
        }
        std::uint8_t type = 0U;
        std::uint32_t nonce = 0U;
        LanGame game;
        if (!ReadHeader(std::span<const std::uint8_t>(buffer.data(), size), type, nonce) || type != kAnnounce ||
            !ReadAnnounce(std::span<const std::uint8_t>(buffer.data(), size), game)) {
            continue;
        }
        if (gameKey_ != 0U && game.gameKey != gameKey_) {
            continue;
        }
        game.address = from;
        game.lastSeenMilliseconds = nowMilliseconds;
        const auto existing = std::find_if(games_.begin(), games_.end(), [&game](const LanGame& known) {
            return known.address.address == game.address.address && known.port == game.port;
        });
        if (existing != games_.end()) {
            *existing = std::move(game);
        } else if (games_.size() < kLanMaxGames) {
            games_.push_back(std::move(game));
        }
    }
    std::erase_if(games_, [nowMilliseconds](const LanGame& game) {
        return nowMilliseconds > game.lastSeenMilliseconds && nowMilliseconds - game.lastSeenMilliseconds > kLanGameExpiryMilliseconds;
    });
}

} // namespace kb::net
