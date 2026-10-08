// kb_authenticode_signer: Authenticode-signs Windows images with a certificate and
// private key from a PFX file. The PFX password is read from standard input, never
// from the command line or the environment, and the key is imported into memory
// only: nothing is added to any certificate or key store.
//
//   kb_authenticode_signer --pfx <file.pfx> [--timestamp-url <RFC 3161 URL>] [--] <image>...
//
// Every image gets a SHA-256 file digest and, with a URL, a SHA-256 RFC 3161
// timestamp. On success the signer is printed as SIGNER|<SHA-1 thumbprint>|<subject>.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <ncrypt.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

// mssign32.dll's signing interface. The structures are documented with
// SignerSignEx2 but not declared by the SDK headers.
struct SignerFileInfo {
    DWORD cbSize;
    LPCWSTR pwszFileName;
    HANDLE hFile;
};

struct SignerSubjectInfo {
    DWORD cbSize;
    DWORD* pdwIndex;
    DWORD dwSubjectChoice;
    SignerFileInfo* pSignerFileInfo;
};

struct SignerCertStoreInfo {
    DWORD cbSize;
    PCCERT_CONTEXT pSigningCert;
    DWORD dwCertPolicy;
    HCERTSTORE hCertStore;
};

struct SignerCert {
    DWORD cbSize;
    DWORD dwCertChoice;
    SignerCertStoreInfo* pCertStoreInfo;
    HWND hwnd;
};

struct SignerAttrAuthcode {
    DWORD cbSize;
    BOOL fCommercial;
    BOOL fIndividual;
    LPCWSTR pwszName;
    LPCWSTR pwszInfo;
};

struct SignerSignatureInfo {
    DWORD cbSize;
    ALG_ID algidHash;
    DWORD dwAttrChoice;
    SignerAttrAuthcode* pAttrAuthcode;
    PCRYPT_ATTRIBUTES psAuthenticated;
    PCRYPT_ATTRIBUTES psUnauthenticated;
};

struct SignerContext {
    DWORD cbSize;
    DWORD cbBlob;
    BYTE* pbBlob;
};

constexpr DWORD kSignerSubjectFile = 1U;
constexpr DWORD kSignerCertStore = 2U;
constexpr DWORD kSignerCertPolicyChain = 2U;
constexpr DWORD kSignerAuthcodeAttr = 1U;
constexpr DWORD kSignerTimestampRfc3161 = 2U;

using SignerSignEx2Function = HRESULT(WINAPI*)(DWORD, SignerSubjectInfo*, SignerCert*, SignerSignatureInfo*, void*,
    DWORD, PCSTR, PCWSTR, PCRYPT_ATTRIBUTES, void*, SignerContext**, void*, void*);
using SignerFreeSignerContextFunction = HRESULT(WINAPI*)(SignerContext*);

int Fail(int code, const char* message) {
    std::fprintf(stderr, "kb_authenticode_signer: %s\n", message);
    return code;
}

int FailWithError(int code, const char* message, HRESULT error) {
    std::fprintf(stderr, "kb_authenticode_signer: %s (0x%08lx)\n", message, static_cast<unsigned long>(error));
    return code;
}

void Wipe(std::wstring& value) noexcept {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    }
    value.clear();
}

void Wipe(std::string& value) noexcept {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size());
    }
    value.clear();
}

// One line from standard input, read without echo or buffering beyond it.
[[nodiscard]] std::optional<std::wstring> ReadPassword() {
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    std::string utf8;
    char character = 0;
    DWORD read = 0U;
    while (utf8.size() <= 1024U && ReadFile(input, &character, 1U, &read, nullptr) != FALSE && read == 1U) {
        if (character == '\n') {
            break;
        }
        utf8.push_back(character);
    }
    if (!utf8.empty() && utf8.back() == '\r') {
        utf8.pop_back();
    }
    if (utf8.size() > 1024U) {
        Wipe(utf8);
        return std::nullopt;
    }
    std::wstring password;
    if (!utf8.empty()) {
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (count <= 0) {
            Wipe(utf8);
            return std::nullopt;
        }
        password.resize(static_cast<std::size_t>(count));
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), password.data(), count);
    }
    Wipe(utf8);
    return password;
}

[[nodiscard]] bool HasCodeSigningUsage(PCCERT_CONTEXT certificate) {
    DWORD bytes = 0U;
    if (CertGetEnhancedKeyUsage(certificate, 0U, nullptr, &bytes) == FALSE || bytes == 0U) {
        return false;
    }
    std::vector<BYTE> storage(bytes);
    auto* usage = reinterpret_cast<CERT_ENHKEY_USAGE*>(storage.data());
    if (CertGetEnhancedKeyUsage(certificate, 0U, usage, &bytes) == FALSE) {
        return false;
    }
    for (DWORD index = 0U; index < usage->cUsageIdentifier; ++index) {
        if (std::string_view{ usage->rgpszUsageIdentifier[index] } == szOID_PKIX_KP_CODE_SIGNING) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool HasPrivateKey(PCCERT_CONTEXT certificate) {
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE key = 0U;
    DWORD keySpec = 0U;
    BOOL mustFree = FALSE;
    // An imported key that was not persisted exists only as the handle cached on the
    // certificate, which only the cache flag looks at.
    if (CryptAcquireCertificatePrivateKey(certificate,
            CRYPT_ACQUIRE_CACHE_FLAG | CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG,
            nullptr, &key, &keySpec, &mustFree) == FALSE) {
        return false;
    }
    if (mustFree != FALSE) {
        if (keySpec == CERT_NCRYPT_KEY_SPEC) {
            NCryptFreeObject(key);
        } else {
            CryptReleaseContext(key, 0U);
        }
    }
    return true;
}

[[nodiscard]] std::string Thumbprint(PCCERT_CONTEXT certificate) {
    BYTE hash[20]{};
    DWORD bytes = sizeof(hash);
    if (CertGetCertificateContextProperty(certificate, CERT_SHA1_HASH_PROP_ID, hash, &bytes) == FALSE) {
        return {};
    }
    std::string text;
    constexpr char kDigits[] = "0123456789ABCDEF";
    for (DWORD index = 0U; index < bytes; ++index) {
        text.push_back(kDigits[hash[index] >> 4U]);
        text.push_back(kDigits[hash[index] & 0xFU]);
    }
    return text;
}

[[nodiscard]] std::string Subject(PCCERT_CONTEXT certificate) {
    wchar_t name[512]{};
    CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0U, nullptr, name, 512U);
    char utf8[1536]{};
    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
    std::string text{ utf8 };
    for (char& character : text) {
        if (character == '|' || static_cast<unsigned char>(character) < 0x20U) {
            character = '_';
        }
    }
    return text;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::filesystem::path pfx;
    std::wstring timestampUrl;
    std::vector<std::filesystem::path> images;
    bool options = true;
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument{ argv[index] };
        if (options && argument == L"--pfx" && index + 1 < argc) {
            pfx = argv[++index];
        } else if (options && argument == L"--timestamp-url" && index + 1 < argc) {
            timestampUrl = argv[++index];
        } else if (options && argument == L"--") {
            options = false;
        } else if (options && argument.starts_with(L"--")) {
            return Fail(2, "unknown option");
        } else {
            images.emplace_back(argument);
        }
    }
    if (pfx.empty() || images.empty() || images.size() > 1024U) {
        return Fail(2, "usage: kb_authenticode_signer --pfx <file.pfx> [--timestamp-url <url>] [--] <image>...");
    }
    if (!timestampUrl.empty() && !timestampUrl.starts_with(L"https://") && !timestampUrl.starts_with(L"http://")) {
        return Fail(2, "the timestamp URL must be an HTTP or HTTPS RFC 3161 service");
    }

    std::optional<std::wstring> password = ReadPassword();
    if (!password.has_value()) {
        return Fail(3, "the PFX password could not be read from standard input");
    }
    std::vector<BYTE> pfxBytes;
    {
        std::ifstream stream(pfx, std::ios::binary);
        pfxBytes.assign(std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{});
        if (!stream.good() && !stream.eof()) {
            pfxBytes.clear();
        }
    }
    if (pfxBytes.empty() || pfxBytes.size() > 1024U * 1024U) {
        Wipe(*password);
        return Fail(3, "the PFX file could not be read");
    }
    CRYPT_DATA_BLOB blob{ static_cast<DWORD>(pfxBytes.size()), pfxBytes.data() };
    // The key lives in this process only and goes with it.
    const HCERTSTORE store = PFXImportCertStore(&blob, password->c_str(), PKCS12_NO_PERSIST_KEY | PKCS12_PREFER_CNG_KSP);
    Wipe(*password);
    SecureZeroMemory(pfxBytes.data(), pfxBytes.size());
    if (store == nullptr) {
        return FailWithError(3, "the PFX file could not be opened with that password", HRESULT_FROM_WIN32(GetLastError()));
    }

    PCCERT_CONTEXT signing = nullptr;
    for (PCCERT_CONTEXT candidate = CertEnumCertificatesInStore(store, nullptr); candidate != nullptr;
         candidate = CertEnumCertificatesInStore(store, candidate)) {
        if (!HasPrivateKey(candidate)) {
            continue;
        }
        if (signing != nullptr) {
            CertFreeCertificateContext(signing);
            CertFreeCertificateContext(candidate);
            CertCloseStore(store, 0U);
            return Fail(3, "the PFX file holds more than one private key");
        }
        signing = CertDuplicateCertificateContext(candidate);
    }
    if (signing == nullptr || !HasCodeSigningUsage(signing)) {
        if (signing != nullptr) {
            CertFreeCertificateContext(signing);
        }
        CertCloseStore(store, 0U);
        return Fail(3, "the PFX file holds no code signing certificate with a private key");
    }

    const HMODULE mssign = LoadLibraryExW(L"mssign32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto sign = mssign != nullptr ? reinterpret_cast<SignerSignEx2Function>(
        reinterpret_cast<void*>(GetProcAddress(mssign, "SignerSignEx2"))) : nullptr;
    const auto freeContext = mssign != nullptr ? reinterpret_cast<SignerFreeSignerContextFunction>(
        reinterpret_cast<void*>(GetProcAddress(mssign, "SignerFreeSignerContext"))) : nullptr;
    int result = 0;
    if (sign == nullptr || freeContext == nullptr) {
        result = Fail(4, "mssign32.dll does not provide SignerSignEx2");
    }
    for (const std::filesystem::path& image : images) {
        if (result != 0) {
            break;
        }
        const std::wstring imagePath = image.wstring();
        SignerFileInfo file{ sizeof(SignerFileInfo), imagePath.c_str(), nullptr };
        DWORD index = 0U;
        SignerSubjectInfo subject{ sizeof(SignerSubjectInfo), &index, kSignerSubjectFile, &file };
        SignerCertStoreInfo storeInfo{ sizeof(SignerCertStoreInfo), signing, kSignerCertPolicyChain, store };
        SignerCert certificate{ sizeof(SignerCert), kSignerCertStore, &storeInfo, nullptr };
        SignerAttrAuthcode authcode{ sizeof(SignerAttrAuthcode), FALSE, TRUE, nullptr, nullptr };
        SignerSignatureInfo signature{ sizeof(SignerSignatureInfo), CALG_SHA_256, kSignerAuthcodeAttr, &authcode, nullptr, nullptr };
        SignerContext* context = nullptr;
        const HRESULT signed_ = sign(0U, &subject, &certificate, &signature, nullptr,
            timestampUrl.empty() ? 0U : kSignerTimestampRfc3161,
            timestampUrl.empty() ? nullptr : szOID_NIST_sha256,
            timestampUrl.empty() ? nullptr : timestampUrl.c_str(),
            nullptr, nullptr, &context, nullptr, nullptr);
        if (context != nullptr) {
            freeContext(context);
        }
        if (FAILED(signed_)) {
            const std::string name = image.filename().string();
            std::fprintf(stderr, "kb_authenticode_signer: %s could not be signed (0x%08lx)\n", name.c_str(),
                static_cast<unsigned long>(signed_));
            result = 4;
        }
    }
    if (result == 0) {
        std::printf("SIGNER|%s|%s\n", Thumbprint(signing).c_str(), Subject(signing).c_str());
    }
    CertFreeCertificateContext(signing);
    CertCloseStore(store, 0U);
    return result;
}
