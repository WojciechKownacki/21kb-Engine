#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/security/Crypto.hpp"
#include "engine/security/ReleaseKeys.hpp"
#include "engine/security/ReleaseManifest.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using kb::tests::Require;

[[nodiscard]] std::vector<std::uint8_t> Hex(std::string_view text) {
    std::vector<std::uint8_t> bytes(text.size() / 2U);
    Require(text.size() % 2U == 0U && kb::security::TryParseHex(text, bytes), "security test vector is not valid hex");
    return bytes;
}

template <std::size_t Size>
[[nodiscard]] std::array<std::uint8_t, Size> HexArray(std::string_view text) {
    std::array<std::uint8_t, Size> bytes{};
    Require(kb::security::TryParseHex(text, bytes), "security test vector has the wrong length");
    return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> Text(std::string_view text) {
    return { text.begin(), text.end() };
}

[[nodiscard]] std::vector<std::uint8_t> Repeat(std::uint8_t value, std::size_t count) {
    return std::vector<std::uint8_t>(count, value);
}

// RFC 8032 section 7.1, tests 1, 2, 3 and SHA(abc).
void RunEd25519VectorTests() {
    struct Vector {
        std::string_view seed;
        std::string_view publicKey;
        std::string_view message;
        std::string_view signature;
    };
    constexpr std::array<Vector, 4> kVectors{ {
        { "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
          "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
          "",
          "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
          "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" },
        { "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
          "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
          "72",
          "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
          "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00" },
        { "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
          "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
          "af82",
          "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
          "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a" },
        { "833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42",
          "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
          "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
          "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
          "dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b589"
          "09351fc9ac90b3ecfdfbc7c66431e0303dca179c138ac17ad9bef1177331a704" },
    } };
    for (const Vector& vector : kVectors) {
        kb::security::Ed25519Seed seed;
        Require(kb::security::TryParseHex(vector.seed, seed.Span()), "Ed25519 seed vector is malformed");
        kb::security::Ed25519SecretKey secretKey;
        kb::security::Ed25519PublicKey publicKey{};
        kb::security::Ed25519KeyPairFromSeed(seed, secretKey, publicKey);
        Require(publicKey == HexArray<32U>(vector.publicKey), "Ed25519 public key must match RFC 8032");

        const std::vector<std::uint8_t> message = Hex(vector.message);
        const kb::security::Ed25519Signature signature = kb::security::Ed25519Sign(secretKey, message);
        Require(signature == HexArray<64U>(vector.signature), "Ed25519 signature must match RFC 8032");
        Require(kb::security::Ed25519Verify(signature, publicKey, message), "Ed25519 must accept an RFC 8032 signature");

        kb::security::Ed25519Signature flipped = signature;
        flipped[17] ^= 0x01U;
        Require(!kb::security::Ed25519Verify(flipped, publicKey, message), "Ed25519 must refuse a modified signature");
        std::vector<std::uint8_t> longer = message;
        longer.push_back(0U);
        Require(!kb::security::Ed25519Verify(signature, publicKey, longer), "Ed25519 must refuse a modified message");
        kb::security::Ed25519PublicKey otherKey = publicKey;
        otherKey[0] ^= 0x80U;
        Require(!kb::security::Ed25519Verify(signature, otherKey, message), "Ed25519 must refuse a different public key");
    }

    kb::security::Ed25519Seed first;
    kb::security::Ed25519Seed second;
    Require(kb::security::GenerateEd25519Seed(first) && kb::security::GenerateEd25519Seed(second),
        "Ed25519 seeds must come from the system CSPRNG");
    Require(!kb::security::ConstantTimeEqual(first.Span(), second.Span()), "two generated Ed25519 seeds must differ");
}

// FIPS 180-4 examples and RFC 4231 HMAC-SHA-512 test cases 1, 2, 3, 6 and 7.
void RunSha512VectorTests() {
    Require(kb::security::Sha512({}) == HexArray<64U>(
        "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
        "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"),
        "SHA-512 of the empty message must match FIPS 180-4");
    const std::array<std::uint8_t, 64U> abc = HexArray<64U>(
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    Require(kb::security::Sha512(Text("abc")) == abc, "SHA-512 of \"abc\" must match FIPS 180-4");
    kb::security::Sha512Hasher streamed;
    streamed.Update(Text("a"));
    streamed.Update({});
    streamed.Update(Text("bc"));
    Require(streamed.Finish() == abc, "streamed SHA-512 must equal the one-shot digest");

    struct HmacVector {
        std::vector<std::uint8_t> key;
        std::vector<std::uint8_t> data;
        std::string_view mac;
    };
    const std::array<HmacVector, 5> kHmacVectors{ {
        { Repeat(0x0BU, 20U), Text("Hi There"),
          "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
          "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854" },
        { Text("Jefe"), Text("what do ya want for nothing?"),
          "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
          "9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737" },
        { Repeat(0xAAU, 20U), Repeat(0xDDU, 50U),
          "fa73b0089d56a284efb0f0756c890be9b1b5dbdd8ee81a3655f83e33b2279d39"
          "bf3e848279a722c806b485a47e67c807b946a337bee8942674278859e13292fb" },
        { Repeat(0xAAU, 131U), Text("Test Using Larger Than Block-Size Key - Hash Key First"),
          "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f352"
          "6b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598" },
        { Repeat(0xAAU, 131U),
          Text("This is a test using a larger than block-size key and a larger than block-size data. "
               "The key needs to be hashed before being used by the HMAC algorithm."),
          "e37b6a775dc87dbaa4dfa9f96e5e3ffddebd71f8867289865df5a32d20cdc944"
          "b6022cac3c4982b10d5eeb55c3e4de15134676fb6de0446065c97440fa8c6a58" },
    } };
    for (const HmacVector& vector : kHmacVectors) {
        const std::array<std::uint8_t, 64U> expected = HexArray<64U>(vector.mac);
        Require(kb::security::HmacSha512(vector.key, vector.data) == expected, "HMAC-SHA-512 must match RFC 4231");
        kb::security::HmacSha512Hasher hasher{ vector.key };
        const std::size_t split = vector.data.size() / 3U;
        hasher.Update(std::span{ vector.data }.first(split));
        hasher.Update(std::span{ vector.data }.subspan(split));
        Require(hasher.Finish() == expected, "streamed HMAC-SHA-512 must match RFC 4231");
    }

    // RFC 5869 defines HKDF over SHA-256 only, so this pins the SHA-512 instance by its
    // definition: PRK = HMAC(salt, IKM), OKM = HMAC(PRK, info || 0x01) for one block.
    const std::vector<std::uint8_t> ikm = Repeat(0x0BU, 22U);
    const std::vector<std::uint8_t> salt = Hex("000102030405060708090a0b0c");
    const std::vector<std::uint8_t> info = Hex("f0f1f2f3f4f5f6f7f8f9");
    const kb::security::Sha512Digest prk = kb::security::HmacSha512(salt, ikm);
    std::vector<std::uint8_t> block = info;
    block.push_back(0x01U);
    const kb::security::Sha512Digest firstBlock = kb::security::HmacSha512(prk, block);
    std::array<std::uint8_t, 42U> okm{};
    kb::security::HkdfSha512(okm, ikm, salt, info);
    Require(std::equal(okm.begin(), okm.end(), firstBlock.begin()), "HKDF-SHA-512 must follow RFC 5869");
}

// draft-irtf-cfrg-xchacha-03 appendix A.1, plus refusal of every kind of tampering.
void RunAeadVectorTests() {
    kb::security::AeadKey key;
    Require(kb::security::TryParseHex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f", key.Span()),
        "XChaCha20-Poly1305 key vector is malformed");
    const kb::security::AeadNonce nonce = HexArray<24U>("404142434445464748494a4b4c4d4e4f5051525354555657");
    const std::vector<std::uint8_t> associated = Hex("50515253c0c1c2c3c4c5c6c7");
    const std::vector<std::uint8_t> plain = Text(
        "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, "
        "sunscreen would be it.");
    const std::vector<std::uint8_t> expectedCipher = Hex(
        "bd6d179d3e83d43b9576579493c0e939572a1700252bfaccbed2902c21396cbb"
        "731c7f1b0b4aa6440bf3a82f4eda7e39ae64c6708c54c216cb96b72e1213b452"
        "2f8c9ba40db5d945b11b69b982c1bb9e3f3fac2bc369488f76b2383565d3fff9"
        "21f9664c97637da9768812f615c68b13b52e");
    const kb::security::AeadTag expectedTag = HexArray<16U>("c0875924c1c7987947deafd8780acf49");

    std::vector<std::uint8_t> bytes = plain;
    kb::security::AeadTag tag{};
    kb::security::AeadEncryptInPlace(bytes, tag, key, nonce, associated);
    Require(bytes == expectedCipher && tag == expectedTag, "XChaCha20-Poly1305 must match the published vector");

    std::vector<std::uint8_t> tampered = bytes;
    tampered[40] ^= 0x04U;
    Require(!kb::security::AeadDecryptInPlace(tampered, tag, key, nonce, associated),
        "XChaCha20-Poly1305 must refuse a modified ciphertext");
    kb::security::AeadTag badTag = tag;
    badTag[3] ^= 0x01U;
    std::vector<std::uint8_t> copy = bytes;
    Require(!kb::security::AeadDecryptInPlace(copy, badTag, key, nonce, associated),
        "XChaCha20-Poly1305 must refuse a modified tag");
    Require(copy == bytes, "a refused decryption must leave the ciphertext untouched");
    kb::security::AeadNonce otherNonce = nonce;
    otherNonce[23] ^= 0x01U;
    Require(!kb::security::AeadDecryptInPlace(copy, tag, key, otherNonce, associated),
        "XChaCha20-Poly1305 must refuse a different nonce");
    Require(!kb::security::AeadDecryptInPlace(copy, tag, key, nonce, {}),
        "XChaCha20-Poly1305 must refuse different associated data");
    Require(kb::security::AeadDecryptInPlace(bytes, tag, key, nonce, associated) && bytes == plain,
        "XChaCha20-Poly1305 must decrypt the published vector");
}

void RunPrimitiveHelperTests() {
    const std::array<std::uint8_t, 5U> a{ 1U, 2U, 3U, 4U, 5U };
    std::array<std::uint8_t, 5U> b = a;
    Require(kb::security::ConstantTimeEqual(a, b), "equal buffers must compare equal");
    b[4] = 6U;
    Require(!kb::security::ConstantTimeEqual(a, b), "a difference in the last byte must be found");
    Require(!kb::security::ConstantTimeEqual(std::span{ a }.first(4U), a), "different lengths must never be equal");
    const std::array<std::uint8_t, 64U> zero{};
    std::array<std::uint8_t, 64U> one{};
    one[63] = 1U;
    Require(!kb::security::ConstantTimeEqual(zero, one), "64-byte comparison must find the last byte");

    std::array<std::uint8_t, 32U> random{};
    Require(kb::security::SecureRandom(random), "the system CSPRNG must supply bytes");
    Require(!kb::security::ConstantTimeEqual(random, std::array<std::uint8_t, 32U>{}),
        "32 random bytes must not all be zero");
    std::vector<std::uint8_t> large(100000U, 0U);
    Require(kb::security::SecureRandom(large), "the system CSPRNG must supply a large request");

    kb::security::SecureWipe(random);
    Require(random == std::array<std::uint8_t, 32U>{}, "SecureWipe must clear every byte");

    Require(kb::security::ToHex(a) == "0102030405", "hex encoding must be lowercase and ordered");
    std::array<std::uint8_t, 2U> parsed{};
    Require(kb::security::TryParseHex("aBcD", parsed) && parsed[0] == 0xABU && parsed[1] == 0xCDU,
        "hex parsing must accept either case");
    Require(!kb::security::TryParseHex("abc", parsed) && !kb::security::TryParseHex("zzzz", parsed),
        "hex parsing must refuse a wrong length or a non-hex digit");
}

// Key files, trust anchors and the installation secret round-trip exactly, and a damaged one is
// refused instead of being read as something else.
void RunReleaseKeyMaterialTests() {
    kb::security::ReleaseSigningKey key;
    Require(kb::security::GenerateReleaseSigningKey(key), "a release signing key must be generated");
    std::string error;
    kb::security::ReleaseSigningKey decoded;
    Require(kb::security::DecodeReleaseSigningKey(kb::security::EncodeReleaseSigningKey(key), decoded, error) &&
            decoded.publicKey == key.publicKey,
        "a release signing key file must round-trip");
    std::string damaged = kb::security::EncodeReleaseSigningKey(key);
    damaged[damaged.find("seed ") + 5U] = damaged[damaged.find("seed ") + 5U] == '0' ? '1' : '0';
    Require(!kb::security::DecodeReleaseSigningKey(damaged, decoded, error) && error.find("damaged") != std::string::npos,
        "a key file whose seed no longer matches its public key must be refused");

    kb::security::TrustAnchor anchor{};
    anchor.productId = "Publisher.Game";
    anchor.releaseKey = key.publicKey;
    anchor.saveSecret = kb::security::DeriveGameSaveSecret(key, anchor.productId);
    anchor.packContentKey.emplace();
    Require(kb::security::SecureRandom(anchor.packContentKey->Span()), "a content key must be generated");
    const std::vector<std::uint8_t> bytes = kb::security::EncodeTrustAnchor(anchor);
    kb::security::TrustAnchor read{};
    Require(kb::security::DecodeTrustAnchor(bytes, read, error) && read.productId == anchor.productId &&
            read.releaseKey == anchor.releaseKey && read.saveSecret.has_value() && read.packContentKey.has_value() &&
            kb::security::ConstantTimeEqual(read.saveSecret->Span(), anchor.saveSecret->Span()) &&
            kb::security::ConstantTimeEqual(read.packContentKey->Span(), anchor.packContentKey->Span()),
        "a trust anchor must round-trip every field");
    std::vector<std::uint8_t> truncated = bytes;
    truncated.pop_back();
    Require(!kb::security::DecodeTrustAnchor(truncated, read, error), "a truncated trust anchor must be refused");
    std::vector<std::uint8_t> unknownFlag = bytes;
    unknownFlag[12] |= 0x80U;
    Require(!kb::security::DecodeTrustAnchor(unknownFlag, read, error), "a trust anchor with an unknown flag must be refused");

    // The save secret is a stable function of key and product, and nothing else.
    Require(kb::security::ConstantTimeEqual(kb::security::DeriveGameSaveSecret(key, "Publisher.Game").Span(),
                anchor.saveSecret->Span()) &&
            !kb::security::ConstantTimeEqual(kb::security::DeriveGameSaveSecret(key, "Publisher.Other").Span(),
                anchor.saveSecret->Span()),
        "the game save secret must be stable per key and product");
    Require(kb::security::IsValidProductId("Publisher.Game-1_x") && !kb::security::IsValidProductId(".hidden") &&
            !kb::security::IsValidProductId("a/b") && !kb::security::IsValidProductId(std::string(129U, 'a')),
        "product ids must be portable file names");

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_engine_security_tests";
    std::error_code fileError;
    std::filesystem::remove_all(root, fileError);
    kb::security::InstallationSecret first;
    kb::security::InstallationSecret second;
    Require(kb::security::LoadOrCreateInstallationSecret(root, first, error) &&
            kb::security::LoadOrCreateInstallationSecret(root, second, error) &&
            kb::security::ConstantTimeEqual(first.Span(), second.Span()),
        "the installation secret must be created once and then read back");
    {
        std::ofstream output{ root / "installation.secret", std::ios::binary | std::ios::trunc };
        output << "21kb-installation-secret 1\nsecret zz\n";
    }
    Require(!kb::security::LoadOrCreateInstallationSecret(root, second, error) && error.find("damaged") != std::string::npos,
        "a damaged installation secret must be refused, not replaced");

    std::filesystem::create_directories(root / "Project" / "Keys", fileError);
    { std::ofstream project{ root / "Project" / "Project.21kbproject" }; }
    Require(kb::security::IsInsideProjectOrRepository(root / "Project" / "Keys" / "game.kbkey") &&
            !kb::security::IsInsideProjectOrRepository(root / "Keys" / "game.kbkey"),
        "a key path inside a project must be recognised");
    std::filesystem::remove_all(root, fileError);
}

void WriteBytes(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output << text;
}

[[nodiscard]] std::string SignedManifestFor(
    const std::filesystem::path& root,
    const kb::security::ReleaseSigningKey& key,
    std::uint64_t release,
    bool antiRollback) {
    kb::security::ReleaseManifest manifest{};
    manifest.productId = "Publisher.Game";
    manifest.contentVersion = "1.2.0";
    manifest.releaseNumber = release;
    manifest.antiRollback = antiRollback;
    std::string error;
    Require(kb::security::BuildReleaseManifest(root, manifest, error), "a release manifest could not be built");
    return kb::security::SignReleaseManifest(manifest, key);
}

// The release manifest is accepted only under its release key and only for its product; every
// modified, missing or unlisted critical file is refused by name; anti-rollback refuses an older
// release once a newer one has run.
// A trust anchor slot as a Linux player carries it, filled with `anchor` (empty: an unfilled slot).
[[nodiscard]] std::vector<std::uint8_t> MakeTrustAnchorSlot(std::span<const std::uint8_t> anchor) {
    std::vector<std::uint8_t> slot(kb::security::kTrustAnchorSlotBytes, 0U);
    std::copy(kb::security::kTrustAnchorSlotMagic.begin(), kb::security::kTrustAnchorSlotMagic.end(), slot.begin());
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        slot[16U + index] = static_cast<std::uint8_t>(anchor.size() >> (index * 8U));
    }
    std::copy(anchor.begin(), anchor.end(), slot.begin() + 24);
    return slot;
}

struct ElfSection {
    std::string name;
    std::uint32_t type = 1U; // SHT_PROGBITS
    std::vector<std::uint8_t> contents;
};

void PutLittleEndian(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value, std::size_t width) {
    for (std::size_t index = 0U; index < width; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

// A minimal 64-bit little-endian x86-64 ELF executable: header, the given sections' contents, a
// section name table and the section header table, as a linker lays them out.
[[nodiscard]] std::vector<std::uint8_t> MakeElfImage(const std::vector<ElfSection>& sections) {
    std::vector<std::uint8_t> image(64U, 0U);
    const std::array<std::uint8_t, 8U> ident{ 0x7FU, 'E', 'L', 'F', 2U, 1U, 1U, 0U };
    std::copy(ident.begin(), ident.end(), image.begin());
    PutLittleEndian(image, 0x10U, 2U, 2U);  // ET_EXEC
    PutLittleEndian(image, 0x12U, 62U, 2U); // EM_X86_64
    PutLittleEndian(image, 0x14U, 1U, 4U);
    PutLittleEndian(image, 0x34U, 64U, 2U);
    PutLittleEndian(image, 0x36U, 56U, 2U);
    std::string names(1U, '\0');
    std::vector<std::pair<std::uint64_t, std::uint32_t>> placed; // offset, name offset
    for (const ElfSection& section : sections) {
        while (image.size() % 16U != 0U) image.push_back(0U);
        placed.emplace_back(image.size(), static_cast<std::uint32_t>(names.size()));
        names += section.name;
        names.push_back('\0');
        image.insert(image.end(), section.contents.begin(), section.contents.end());
    }
    const auto namesName = static_cast<std::uint32_t>(names.size());
    names += ".shstrtab";
    names.push_back('\0');
    const std::uint64_t namesOffset = image.size();
    image.insert(image.end(), names.begin(), names.end());
    while (image.size() % 8U != 0U) image.push_back(0U);
    const std::uint64_t table = image.size();
    const std::size_t count = sections.size() + 2U;
    image.resize(image.size() + count * 64U, 0U);
    for (std::size_t index = 0U; index < sections.size(); ++index) {
        const std::size_t header = static_cast<std::size_t>(table) + (index + 1U) * 64U;
        PutLittleEndian(image, header + 0x00U, placed[index].second, 4U);
        PutLittleEndian(image, header + 0x04U, sections[index].type, 4U);
        PutLittleEndian(image, header + 0x08U, 2U, 8U); // SHF_ALLOC
        PutLittleEndian(image, header + 0x18U, placed[index].first, 8U);
        PutLittleEndian(image, header + 0x20U, sections[index].contents.size(), 8U);
        PutLittleEndian(image, header + 0x30U, 16U, 8U);
    }
    const std::size_t namesHeader = static_cast<std::size_t>(table) + (count - 1U) * 64U;
    PutLittleEndian(image, namesHeader + 0x00U, namesName, 4U);
    PutLittleEndian(image, namesHeader + 0x04U, 3U, 4U); // SHT_STRTAB
    PutLittleEndian(image, namesHeader + 0x18U, namesOffset, 8U);
    PutLittleEndian(image, namesHeader + 0x20U, names.size(), 8U);
    PutLittleEndian(image, 0x28U, table, 8U);
    PutLittleEndian(image, 0x3AU, 64U, 2U);
    PutLittleEndian(image, 0x3CU, count, 2U);
    PutLittleEndian(image, 0x3EU, count - 1U, 2U);
    return image;
}

[[nodiscard]] std::vector<ElfSection> PlayerSections(std::vector<std::uint8_t> slot) {
    return {
        ElfSection{ .name = ".text", .contents = std::vector<std::uint8_t>(48U, 0xC3U) },
        ElfSection{ .name = std::string{ kb::security::kTrustAnchorElfSectionName }, .contents = std::move(slot) },
        ElfSection{ .name = ".data", .contents = std::vector<std::uint8_t>(32U, 0x11U) },
    };
}

// A Linux player carries its trust anchor in a reserved ELF section that packaging fills after the
// build. An empty slot is a development player; a filled one is read back exactly; anything
// damaged is Invalid, never Absent.
void RunElfTrustAnchorTests() {
    using State = kb::security::TrustAnchorLookup::State;
    kb::security::ReleaseSigningKey key;
    Require(kb::security::GenerateReleaseSigningKey(key), "a release signing key must be generated");
    kb::security::TrustAnchor anchor{};
    anchor.productId = "Publisher.LinuxGame";
    anchor.releaseKey = key.publicKey;
    anchor.saveSecret = kb::security::DeriveGameSaveSecret(key, anchor.productId);
    const std::vector<std::uint8_t> encoded = kb::security::EncodeTrustAnchor(anchor);

    const std::vector<std::uint8_t> empty = MakeElfImage(PlayerSections(MakeTrustAnchorSlot({})));
    Require(kb::security::ReadTrustAnchorFromElf(empty).state == State::Absent,
        "an unfilled trust anchor slot must read as a development player");
    Require(kb::security::ReadTrustAnchorFromElf(MakeElfImage({ ElfSection{ .name = ".text",
                .contents = std::vector<std::uint8_t>(16U, 0x90U) } })).state == State::Absent,
        "an ELF image without a slot section has no trust anchor");

    const std::vector<std::uint8_t> filled = MakeElfImage(PlayerSections(MakeTrustAnchorSlot(encoded)));
    const kb::security::TrustAnchorLookup present = kb::security::ReadTrustAnchorFromElf(filled);
    Require(present.state == State::Present && present.anchor.productId == anchor.productId &&
            present.anchor.releaseKey == anchor.releaseKey && present.anchor.saveSecret.has_value() &&
            kb::security::ConstantTimeEqual(present.anchor.saveSecret->Span(), anchor.saveSecret->Span()) &&
            !present.anchor.packContentKey.has_value(),
        "a filled ELF trust anchor slot must read back every field");

    const auto invalid = [](const std::vector<std::uint8_t>& image, const char* message) {
        const kb::security::TrustAnchorLookup lookup = kb::security::ReadTrustAnchorFromElf(image);
        Require(lookup.state == State::Invalid && !lookup.error.empty(), message);
    };
    std::vector<std::uint8_t> slot = MakeTrustAnchorSlot(encoded);
    slot[0] = 'X';
    invalid(MakeElfImage(PlayerSections(slot)), "a slot with the wrong magic must be Invalid");
    slot = MakeTrustAnchorSlot(encoded);
    slot.back() = 1U;
    invalid(MakeElfImage(PlayerSections(slot)), "a slot with bytes after its anchor must be Invalid");
    slot = MakeTrustAnchorSlot(encoded);
    slot[16] = 0xFFU;
    slot[17] = 0xFFU;
    invalid(MakeElfImage(PlayerSections(slot)), "a slot that claims more than it holds must be Invalid");
    slot = MakeTrustAnchorSlot(encoded);
    slot[20] = 1U;
    invalid(MakeElfImage(PlayerSections(slot)), "a slot with a non-zero reserved field must be Invalid");
    slot = MakeTrustAnchorSlot(std::span<const std::uint8_t>{ encoded }.first(encoded.size() - 1U));
    invalid(MakeElfImage(PlayerSections(slot)), "a slot whose anchor does not decode must be Invalid");
    slot = MakeTrustAnchorSlot(encoded);
    slot.pop_back();
    invalid(MakeElfImage(PlayerSections(slot)), "a slot section of the wrong size must be Invalid");

    std::vector<ElfSection> twice = PlayerSections(MakeTrustAnchorSlot(encoded));
    twice.push_back(twice[1]);
    invalid(MakeElfImage(twice), "an image with two slot sections must be Invalid");
    std::vector<ElfSection> noBits = PlayerSections(MakeTrustAnchorSlot(encoded));
    noBits[1].type = 8U; // SHT_NOBITS
    invalid(MakeElfImage(noBits), "a slot section without file contents must be Invalid");

    std::vector<std::uint8_t> truncated = filled;
    truncated.resize(truncated.size() - 64U);
    invalid(truncated, "an image whose section table runs past the file must be Invalid");
    std::vector<std::uint8_t> outside = filled;
    std::uint64_t table = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        table |= static_cast<std::uint64_t>(outside[0x28U + index]) << (index * 8U);
    }
    PutLittleEndian(outside, static_cast<std::size_t>(table) + 2U * 64U + 0x18U, outside.size(), 8U);
    invalid(outside, "a slot section that points past the file must be Invalid");
    std::vector<std::uint8_t> elf32 = filled;
    elf32[4] = 1U;
    invalid(elf32, "a 32-bit ELF image must be Invalid, not read as 64-bit");

    // Release tooling reads a player on disk, whatever the host: the Linux player has no extension.
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("21kb_elf_trust_anchor_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code fileError;
    std::filesystem::create_directories(root, fileError);
    Require(!fileError, "the ELF trust anchor test directory could not be created");
    {
        std::ofstream output{ root / "Game", std::ios::binary };
        output.write(reinterpret_cast<const char*>(filled.data()), static_cast<std::streamsize>(filled.size()));
    }
    const kb::security::TrustAnchorLookup fromDisk = kb::security::ReadTrustAnchorFromExecutable(root / "Game");
    Require(fromDisk.state == State::Present && fromDisk.anchor.productId == anchor.productId,
        "release tooling did not read the trust anchor of a Linux player on disk");
    std::filesystem::remove_all(root, fileError);

#if defined(__linux__) && !defined(__ANDROID__)
    // This test executable links the slot too; nothing filled it, so it is a development player.
    Require(kb::security::LoadExecutableTrustAnchor().state == State::Absent,
        "an unfilled slot linked into the running executable must read as Absent");
#endif
}

void RunReleaseManifestTests() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_engine_release_tests";
    std::error_code fileError;
    std::filesystem::remove_all(root, fileError);
    const std::filesystem::path release = root / "Release";
    WriteBytes(release / "Game.exe", "player bytes");
    WriteBytes(release / "kb_plugin.dll", "plugin bytes");
    WriteBytes(release / "Licenses" / "notice with spaces.txt", "license text");

    kb::security::ReleaseSigningKey key;
    kb::security::ReleaseSigningKey otherKey;
    Require(kb::security::GenerateReleaseSigningKey(key) && kb::security::GenerateReleaseSigningKey(otherKey),
        "release test keys could not be generated");
    const std::string text = SignedManifestFor(release, key, 41U, false);
    WriteBytes(release / "release.kbmanifest", text);

    kb::security::ReleaseManifest parsed{};
    Require(kb::security::ParseAndVerifyReleaseManifest(text, key.publicKey, parsed) == kb::security::ReleaseManifestStatus::Success &&
            parsed.releaseNumber == 41U && parsed.files.size() == 3U &&
            parsed.FindFile("Licenses/notice with spaces.txt") != nullptr,
        "a signed release manifest must parse under its key");
    Require(kb::security::ParseAndVerifyReleaseManifest(text, otherKey.publicKey, parsed) ==
            kb::security::ReleaseManifestStatus::SignatureInvalid,
        "a release manifest must be refused under another key");
    std::string edited = text;
    edited[edited.find("release 41") + 9U] = '9';
    Require(kb::security::ParseAndVerifyReleaseManifest(edited, key.publicKey, parsed) ==
            kb::security::ReleaseManifestStatus::SignatureInvalid,
        "an edited release manifest must be refused");

    Require(kb::security::VerifyReleaseDirectory(release, key.publicKey, "Publisher.Game").status ==
            kb::security::ReleaseManifestStatus::Success,
        "an intact release must verify");
    Require(kb::security::VerifyReleaseDirectory(release, key.publicKey, "Publisher.Other").status ==
            kb::security::ReleaseManifestStatus::ProductMismatch,
        "a release of another product must be refused");
    const std::array<std::filesystem::path, 1U> executable{ release / "Game.exe" };
    Require(kb::security::VerifyInstalledRelease(release, key.publicKey, "Publisher.Game", executable).status ==
            kb::security::ReleaseManifestStatus::Success,
        "an intact installed release must pass the startup check");

    // The same length, different bytes: only a hash notices.
    WriteBytes(release / "Game.exe", "player bytez");
    kb::security::ReleaseVerification modified =
        kb::security::VerifyInstalledRelease(release, key.publicKey, "Publisher.Game", executable);
    Require(modified.status == kb::security::ReleaseManifestStatus::FileModified && modified.detail == "Game.exe",
        "a modified executable must be refused by name");
    WriteBytes(release / "Game.exe", "player bytes");

    WriteBytes(release / "version.dll", "planted");
    modified = kb::security::VerifyInstalledRelease(release, key.publicKey, "Publisher.Game", executable);
    Require(modified.status == kb::security::ReleaseManifestStatus::UnlistedFile && modified.detail == "version.dll",
        "an unlisted native module beside the player must be refused");
    std::filesystem::remove(release / "version.dll");
    // A file without an extension may be an executable (a Linux player has none): unlisted, it
    // is refused at startup like a planted DLL, at any depth.
    WriteBytes(release / "Licenses" / "helper", "\x7F" "ELF planted");
    modified = kb::security::VerifyInstalledRelease(release, key.publicKey, "Publisher.Game", executable);
    Require(modified.status == kb::security::ReleaseManifestStatus::UnlistedFile && modified.detail == "Licenses/helper" &&
            kb::security::IsCriticalReleaseFile("Game") && kb::security::IsCriticalReleaseFile(".hidden") &&
            !kb::security::IsCriticalReleaseFile("Licenses/notice.txt"),
        "an unlisted file without an extension must be refused at startup");
    std::filesystem::remove(release / "Licenses" / "helper");

    WriteBytes(release / "Licenses" / "notice with spaces.txt", "license text, edited");
    Require(kb::security::VerifyInstalledRelease(release, key.publicKey, "Publisher.Game", executable).status ==
                kb::security::ReleaseManifestStatus::Success &&
            kb::security::VerifyReleaseDirectory(release, key.publicKey, "Publisher.Game").status ==
                kb::security::ReleaseManifestStatus::FileModified,
        "a modified non-critical file must pass the startup check and fail the full verification");
    WriteBytes(release / "Licenses" / "notice with spaces.txt", "license text");
    WriteBytes(release / "package.receipt.json", "{}");
    const kb::security::ReleaseVerification extra = kb::security::VerifyReleaseDirectory(release, key.publicKey, "Publisher.Game");
    Require(extra.status == kb::security::ReleaseManifestStatus::Success && extra.uncovered.size() == 1U &&
            extra.uncovered.front() == "package.receipt.json",
        "a non-critical file written after the manifest must be reported, not refused");
    std::filesystem::remove(release / "kb_plugin.dll");
    Require(kb::security::VerifyInstalledRelease(release, key.publicKey, "Publisher.Game", executable).status ==
            kb::security::ReleaseManifestStatus::FileMissing,
        "a missing native module must be refused");

    kb::security::ReleaseManifest newer{};
    newer.productId = "Publisher.Game";
    newer.releaseNumber = 7U;
    const std::filesystem::path state = root / "State";
    Require(kb::security::EnforceReleaseAntiRollback(state, newer) == kb::security::ReleaseManifestStatus::Success,
        "a first release must be recorded");
    kb::security::ReleaseManifest older = newer;
    older.releaseNumber = 6U;
    Require(kb::security::EnforceReleaseAntiRollback(state, older) == kb::security::ReleaseManifestStatus::RolledBack,
        "an older release must be refused after a newer one ran");
    newer.releaseNumber = 8U;
    Require(kb::security::EnforceReleaseAntiRollback(state, newer) == kb::security::ReleaseManifestStatus::Success &&
            kb::security::EnforceReleaseAntiRollback(state, older) == kb::security::ReleaseManifestStatus::RolledBack,
        "a newer release must raise the recorded floor");
    std::filesystem::remove_all(root, fileError);
}

} // namespace

namespace kb::tests {

void RunSecurityTests() {
    RunEd25519VectorTests();
    RunSha512VectorTests();
    RunAeadVectorTests();
    RunPrimitiveHelperTests();
    RunReleaseKeyMaterialTests();
    RunElfTrustAnchorTests();
    RunReleaseManifestTests();
}

} // namespace kb::tests
