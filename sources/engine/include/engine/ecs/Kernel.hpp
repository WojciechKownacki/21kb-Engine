#pragma once

#include "engine/ecs/Query.hpp"
#include "engine/ecs/QueryExecutionScratch.hpp"
#include "engine/ecs/System.hpp"
#include "engine/ecs/SystemAccess.hpp"
#include "engine/ecs/World.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#include <cpuid.h>
#endif

namespace kb::ecs {

struct KernelNoConstants {};

enum class KernelBackend {
    Scalar,
    Neon,
    Sse2,
    Avx2,
    Avx512,
};

enum class KernelBackendPreference {
    Auto,
    Scalar,
    Neon,
    Sse2,
    Avx2,
    Avx512,
};

[[nodiscard]] constexpr std::string_view KernelBackendName(KernelBackend backend) noexcept {
    switch (backend) {
    case KernelBackend::Scalar:
        return "scalar";
    case KernelBackend::Neon:
        return "neon";
    case KernelBackend::Sse2:
        return "sse2";
    case KernelBackend::Avx2:
        return "avx2";
    case KernelBackend::Avx512:
        return "avx512";
    }
    return "scalar";
}

[[nodiscard]] constexpr std::string_view KernelBackendPreferenceName(KernelBackendPreference preference) noexcept {
    switch (preference) {
    case KernelBackendPreference::Auto:
        return "auto";
    case KernelBackendPreference::Scalar:
        return "scalar";
    case KernelBackendPreference::Neon:
        return "neon";
    case KernelBackendPreference::Sse2:
        return "sse2";
    case KernelBackendPreference::Avx2:
        return "avx2";
    case KernelBackendPreference::Avx512:
        return "avx512";
    }
    return "auto";
}

struct KernelScalarTag {
    static constexpr KernelBackend Backend = KernelBackend::Scalar;
    static constexpr std::size_t FloatLaneCount = 1;
};

struct KernelNeonTag {
    static constexpr KernelBackend Backend = KernelBackend::Neon;
    static constexpr std::size_t FloatLaneCount = 4;
};

struct KernelSse2Tag {
    static constexpr KernelBackend Backend = KernelBackend::Sse2;
    static constexpr std::size_t FloatLaneCount = 4;
};

struct KernelAvx2Tag {
    static constexpr KernelBackend Backend = KernelBackend::Avx2;
    static constexpr std::size_t FloatLaneCount = 8;
};

struct KernelAvx512Tag {
    static constexpr KernelBackend Backend = KernelBackend::Avx512;
    static constexpr std::size_t FloatLaneCount = 16;
};

template <typename... Components>
struct KernelInputComponents {
    static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS kernel input components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<Components> && ...), "ECS kernel input components must be trivially destructible");
    static constexpr std::size_t Count = sizeof...(Components);
};

template <typename... Components>
struct KernelOutputComponents {
    static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS kernel output components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<Components> && ...), "ECS kernel output components must be trivially destructible");
    static constexpr std::size_t Count = sizeof...(Components);
};

template <typename... Assets>
struct KernelReadOnlyAssets {
    static_assert((std::is_object_v<Assets> && ...), "ECS kernel readonly assets must be object types");
    static constexpr std::size_t Count = sizeof...(Assets);
};

template <typename InputPack, typename OutputPack, typename AssetPack = KernelReadOnlyAssets<>, typename Constants = KernelNoConstants>
struct KernelContract;

template <typename... Assets>
class KernelAssets {
public:
    constexpr KernelAssets() noexcept
        requires(sizeof...(Assets) == 0)
    = default;

    explicit constexpr KernelAssets(const Assets&... assets) noexcept
        : assets_(std::addressof(assets)...) {}

    template <std::size_t Index>
    [[nodiscard]] constexpr const auto& Get() const noexcept {
        return *std::get<Index>(assets_);
    }

private:
    std::tuple<const Assets*...> assets_{};
};

template <typename... Assets>
[[nodiscard]] constexpr KernelAssets<Assets...> BindKernelAssets(const Assets&... assets) noexcept {
    return KernelAssets<Assets...>{ assets... };
}

namespace detail {

[[nodiscard]] inline std::uint32_t MaxX86CpuLeaf() noexcept {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    std::array<int, 4> cpuInfo{};
    __cpuidex(cpuInfo.data(), 0, 0);
    return static_cast<std::uint32_t>(cpuInfo[0]);
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    return __get_cpuid_max(0, nullptr);
#else
    return 0;
#endif
}

[[nodiscard]] inline bool IsX86CpuFeatureSupported(std::uint32_t leaf, std::uint32_t subleaf, std::uint32_t registerIndex, std::uint32_t bit) noexcept {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    if (leaf > MaxX86CpuLeaf()) {
        return false;
    }
    std::array<int, 4> cpuInfo{};
    __cpuidex(cpuInfo.data(), static_cast<int>(leaf), static_cast<int>(subleaf));
    return (static_cast<std::uint32_t>(cpuInfo[registerIndex]) & (1U << bit)) != 0U;
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    if (leaf > MaxX86CpuLeaf()) {
        return false;
    }
    std::uint32_t eax = 0;
    std::uint32_t ebx = 0;
    std::uint32_t ecx = 0;
    std::uint32_t edx = 0;
    if (__get_cpuid_count(leaf, subleaf, &eax, &ebx, &ecx, &edx) == 0) {
        return false;
    }
    const std::array registers{ eax, ebx, ecx, edx };
    return (registers[registerIndex] & (1U << bit)) != 0U;
#else
    static_cast<void>(leaf);
    static_cast<void>(subleaf);
    static_cast<void>(registerIndex);
    static_cast<void>(bit);
    return false;
#endif
}

[[nodiscard]] inline std::uint64_t ReadXcr0() noexcept {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    return _xgetbv(0);
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    std::uint32_t eax = 0;
    std::uint32_t edx = 0;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return (static_cast<std::uint64_t>(edx) << 32U) | eax;
#else
    return 0;
#endif
}

[[nodiscard]] inline bool CpuSupportsSse2() noexcept {
#if defined(_M_X64) || defined(__x86_64__)
    return true;
#elif defined(_M_IX86) || defined(__i386__)
    return IsX86CpuFeatureSupported(1, 0, 3, 26);
#else
    return false;
#endif
}

[[nodiscard]] inline bool CpuSupportsAvx2() noexcept {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    const bool osxSave = IsX86CpuFeatureSupported(1, 0, 2, 27);
    const bool avx = IsX86CpuFeatureSupported(1, 0, 2, 28);
    if (!osxSave || !avx) {
        return false;
    }

    const std::uint64_t xcr0 = ReadXcr0();
    if ((xcr0 & 0x6U) != 0x6U) {
        return false;
    }

    return IsX86CpuFeatureSupported(7, 0, 1, 5);
#else
    return false;
#endif
}

[[nodiscard]] inline bool CpuSupportsAvx512() noexcept {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    const bool osxSave = IsX86CpuFeatureSupported(1, 0, 2, 27);
    const bool avx = IsX86CpuFeatureSupported(1, 0, 2, 28);
    if (!osxSave || !avx) {
        return false;
    }

    const std::uint64_t xcr0 = ReadXcr0();
    if ((xcr0 & 0xE6U) != 0xE6U) {
        return false;
    }

    return IsX86CpuFeatureSupported(7, 0, 1, 16);
#else
    return false;
#endif
}

[[nodiscard]] inline bool CpuSupportsNeon() noexcept {
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64) || defined(__aarch64__)
    return true;
#else
    return false;
#endif
}

template <typename T, typename... Types>
inline constexpr bool ContainsType = (std::is_same_v<T, Types> || ...);

template <typename... Types>
struct AreUniqueTypes : std::true_type {};

template <typename T, typename... Rest>
struct AreUniqueTypes<T, Rest...> : std::bool_constant<!ContainsType<T, Rest...> && AreUniqueTypes<Rest...>::value> {};

template <typename InputPack, typename OutputPack>
struct KernelComponentPacksDoNotOverlap;

template <typename... Inputs, typename... Outputs>
struct KernelComponentPacksDoNotOverlap<KernelInputComponents<Inputs...>, KernelOutputComponents<Outputs...>>
    : std::bool_constant<(!ContainsType<Inputs, Outputs...> && ...)> {};

template <typename Constants>
inline constexpr bool IsValidKernelConstants =
    std::is_same_v<Constants, KernelNoConstants> || (std::is_object_v<Constants> && std::is_trivially_copyable_v<Constants>);

template <typename Kernel, typename Batch>
inline constexpr bool CanInvokeKernelScalar =
    std::is_invocable_v<Kernel&, Batch&> || std::is_invocable_v<Kernel&, Batch&, KernelScalarTag>;

template <typename Kernel, typename Batch>
inline constexpr bool CanInvokeKernelNeon = std::is_invocable_v<Kernel&, Batch&, KernelNeonTag>;

template <typename Kernel, typename Batch>
inline constexpr bool CanInvokeKernelSse2 = std::is_invocable_v<Kernel&, Batch&, KernelSse2Tag>;

template <typename Kernel, typename Batch>
inline constexpr bool CanInvokeKernelAvx2 = std::is_invocable_v<Kernel&, Batch&, KernelAvx2Tag>;

template <typename Kernel, typename Batch>
inline constexpr bool CanInvokeKernelAvx512 = std::is_invocable_v<Kernel&, Batch&, KernelAvx512Tag>;

template <typename Kernel, typename Batch>
void InvokeKernelScalar(Kernel& kernel, Batch& batch) {
    if constexpr (std::is_invocable_v<Kernel&, Batch&, KernelScalarTag>) {
        std::invoke(kernel, batch, KernelScalarTag{});
    } else {
        std::invoke(kernel, batch);
    }
}

template <typename Kernel, typename Batch>
[[nodiscard]] KernelBackend ResolveKernelBackend(KernelBackendPreference preference) noexcept {
    if (preference == KernelBackendPreference::Scalar) {
        return KernelBackend::Scalar;
    }
    if ((preference == KernelBackendPreference::Neon || preference == KernelBackendPreference::Auto)
        && CanInvokeKernelNeon<Kernel, Batch> && CpuSupportsNeon()) {
        return KernelBackend::Neon;
    }
    if ((preference == KernelBackendPreference::Avx512 || preference == KernelBackendPreference::Auto)
        && CanInvokeKernelAvx512<Kernel, Batch> && CpuSupportsAvx512()) {
        return KernelBackend::Avx512;
    }
    if ((preference == KernelBackendPreference::Avx2 || preference == KernelBackendPreference::Auto)
        && CanInvokeKernelAvx2<Kernel, Batch> && CpuSupportsAvx2()) {
        return KernelBackend::Avx2;
    }
    if ((preference == KernelBackendPreference::Sse2 || preference == KernelBackendPreference::Auto)
        && CanInvokeKernelSse2<Kernel, Batch> && CpuSupportsSse2()) {
        return KernelBackend::Sse2;
    }
    return KernelBackend::Scalar;
}

template <typename Kernel, typename Batch>
void InvokeKernelWithBackend(Kernel& kernel, Batch& batch, KernelBackend backend) {
    static_assert(CanInvokeKernelScalar<Kernel, Batch>, "ECS kernels must provide a scalar KernelBatch fallback");

    switch (backend) {
    case KernelBackend::Neon:
        if constexpr (CanInvokeKernelNeon<Kernel, Batch>) {
            std::invoke(kernel, batch, KernelNeonTag{});
            return;
        }
        break;
    case KernelBackend::Avx512:
        if constexpr (CanInvokeKernelAvx512<Kernel, Batch>) {
            std::invoke(kernel, batch, KernelAvx512Tag{});
            return;
        }
        break;
    case KernelBackend::Avx2:
        if constexpr (CanInvokeKernelAvx2<Kernel, Batch>) {
            std::invoke(kernel, batch, KernelAvx2Tag{});
            return;
        }
        break;
    case KernelBackend::Sse2:
        if constexpr (CanInvokeKernelSse2<Kernel, Batch>) {
            std::invoke(kernel, batch, KernelSse2Tag{});
            return;
        }
        break;
    case KernelBackend::Scalar:
        break;
    }

    InvokeKernelScalar(kernel, batch);
}

template <typename T>
void AssertKernelComponentColumnAligned(const T* components, std::size_t count) noexcept {
    static_cast<void>(components);
    static_cast<void>(count);
    assert((count == 0U || components != nullptr) && "ECS kernel component column is null for a non-empty batch");
    assert((count == 0U || (reinterpret_cast<std::uintptr_t>(components) % alignof(T)) == 0U) && "ECS kernel component column is not aligned for its component type");
}

} // namespace detail

[[nodiscard]] inline bool IsKernelBackendSupported(KernelBackend backend) noexcept {
    switch (backend) {
    case KernelBackend::Scalar:
        return true;
    case KernelBackend::Neon:
        return detail::CpuSupportsNeon();
    case KernelBackend::Sse2:
        return detail::CpuSupportsSse2();
    case KernelBackend::Avx2:
        return detail::CpuSupportsAvx2();
    case KernelBackend::Avx512:
        return detail::CpuSupportsAvx512();
    }
    return false;
}

[[nodiscard]] constexpr std::size_t KernelBackendFloatLaneCount(KernelBackend backend) noexcept {
    switch (backend) {
    case KernelBackend::Scalar:
        return KernelScalarTag::FloatLaneCount;
    case KernelBackend::Neon:
        return KernelNeonTag::FloatLaneCount;
    case KernelBackend::Sse2:
        return KernelSse2Tag::FloatLaneCount;
    case KernelBackend::Avx2:
        return KernelAvx2Tag::FloatLaneCount;
    case KernelBackend::Avx512:
        return KernelAvx512Tag::FloatLaneCount;
    }
    return KernelScalarTag::FloatLaneCount;
}

[[nodiscard]] inline KernelBackend PreferredKernelBackend() noexcept {
    if (IsKernelBackendSupported(KernelBackend::Neon)) {
        return KernelBackend::Neon;
    }
    if (IsKernelBackendSupported(KernelBackend::Avx512)) {
        return KernelBackend::Avx512;
    }
    if (IsKernelBackendSupported(KernelBackend::Avx2)) {
        return KernelBackend::Avx2;
    }
    if (IsKernelBackendSupported(KernelBackend::Sse2)) {
        return KernelBackend::Sse2;
    }
    return KernelBackend::Scalar;
}

template <typename... InputTypes, typename... OutputTypes, typename... AssetTypes, typename ConstantsType>
struct KernelContract<KernelInputComponents<InputTypes...>, KernelOutputComponents<OutputTypes...>, KernelReadOnlyAssets<AssetTypes...>, ConstantsType> {
    static_assert(sizeof...(OutputTypes) > 0, "ECS kernel contract must declare at least one output component");
    static_assert(detail::AreUniqueTypes<InputTypes...>::value, "ECS kernel input components must be unique");
    static_assert(detail::AreUniqueTypes<OutputTypes...>::value, "ECS kernel output components must be unique");
    static_assert(
        detail::KernelComponentPacksDoNotOverlap<KernelInputComponents<InputTypes...>, KernelOutputComponents<OutputTypes...>>::value,
        "ECS kernel components cannot be declared as both input and output");
    static_assert(detail::AreUniqueTypes<AssetTypes...>::value, "ECS kernel readonly asset types must be unique");
    static_assert(detail::IsValidKernelConstants<ConstantsType>, "ECS kernel constants must be trivially copyable object types");

    using InputComponents = KernelInputComponents<InputTypes...>;
    using OutputComponents = KernelOutputComponents<OutputTypes...>;
    using ReadOnlyAssets = KernelReadOnlyAssets<AssetTypes...>;
    using Constants = ConstantsType;
    using QueryType = Query<InputTypes..., OutputTypes...>;
    using MutableBatchType = MutableQueryBatch<InputTypes..., OutputTypes...>;
    using AssetBindings = KernelAssets<AssetTypes...>;

    static constexpr std::size_t InputCount = sizeof...(InputTypes);
    static constexpr std::size_t OutputCount = sizeof...(OutputTypes);
    static constexpr std::size_t AssetCount = sizeof...(AssetTypes);
};

template <typename Contract>
class KernelBatch;

template <typename... InputTypes, typename... OutputTypes, typename... AssetTypes, typename ConstantsType>
class KernelBatch<KernelContract<KernelInputComponents<InputTypes...>, KernelOutputComponents<OutputTypes...>, KernelReadOnlyAssets<AssetTypes...>, ConstantsType>> {
public:
    using Contract = KernelContract<KernelInputComponents<InputTypes...>, KernelOutputComponents<OutputTypes...>, KernelReadOnlyAssets<AssetTypes...>, ConstantsType>;
    using MutableBatchType = typename Contract::MutableBatchType;
    using AssetBindings = typename Contract::AssetBindings;

    KernelBatch(MutableBatchType& batch, const AssetBindings& assets, const ConstantsType& constants) noexcept
        : batch_(batch)
        , assets_(assets)
        , constants_(std::addressof(constants)) {
        ValidateComponentAlignment(std::index_sequence_for<InputTypes...>{}, std::index_sequence_for<OutputTypes...>{});
    }

    [[nodiscard]] std::size_t Count() const noexcept { return batch_.Count(); }
    [[nodiscard]] bool Empty() const noexcept { return batch_.Empty(); }
    [[nodiscard]] Entity EntityAt(std::size_t index) const noexcept { return batch_.EntityAt(index); }

    template <std::size_t Index>
    [[nodiscard]] const auto* Inputs() const noexcept {
        static_assert(Index < sizeof...(InputTypes), "ECS kernel input component index is out of range");
        return batch_.template Components<Index>();
    }

    template <std::size_t Index>
    [[nodiscard]] auto* Outputs() const noexcept {
        static_assert(Index < sizeof...(OutputTypes), "ECS kernel output component index is out of range");
        return batch_.template Components<sizeof...(InputTypes) + Index>();
    }

    template <std::size_t Index>
    [[nodiscard]] const auto& Asset() const noexcept {
        static_assert(Index < sizeof...(AssetTypes), "ECS kernel readonly asset index is out of range");
        return assets_.template Get<Index>();
    }

    [[nodiscard]] const ConstantsType& Constants() const noexcept { return *constants_; }

    void Prefetch(std::size_t index) const noexcept { batch_.Prefetch(index); }

private:
    template <std::size_t... Indices>
    void ValidateInputAlignment(std::index_sequence<Indices...>) const noexcept {
        (detail::AssertKernelComponentColumnAligned<InputTypes>(batch_.template Components<Indices>(), batch_.Count()), ...);
    }

    template <std::size_t... Indices>
    void ValidateOutputAlignment(std::index_sequence<Indices...>) const noexcept {
        (detail::AssertKernelComponentColumnAligned<OutputTypes>(batch_.template Components<sizeof...(InputTypes) + Indices>(), batch_.Count()), ...);
    }

    template <std::size_t... InputIndices, std::size_t... OutputIndices>
    void ValidateComponentAlignment(std::index_sequence<InputIndices...> inputIndices, std::index_sequence<OutputIndices...> outputIndices) const noexcept {
        ValidateInputAlignment(inputIndices);
        ValidateOutputAlignment(outputIndices);
    }

    MutableBatchType& batch_;
    const AssetBindings& assets_;
    const ConstantsType* constants_ = nullptr;
};

template <typename Contract>
using KernelQuery = typename Contract::QueryType;

template <typename Contract>
using KernelAssetBindings = typename Contract::AssetBindings;

template <typename Contract>
using KernelConstants = typename Contract::Constants;

template <typename Contract, typename Kernel>
class CompiledKernelQuery {
public:
    using QueryType = KernelQuery<Contract>;
    using KernelType = std::decay_t<Kernel>;
    using AssetBindings = KernelAssetBindings<Contract>;
    using Constants = KernelConstants<Contract>;
    using MutableBatchType = typename Contract::MutableBatchType;
    using BatchType = KernelBatch<Contract>;

    static_assert(detail::CanInvokeKernelScalar<KernelType, BatchType>, "ECS compiled kernel query requires a scalar KernelBatch fallback");

    CompiledKernelQuery(
        QueryType query,
        QueryExecutionSettings settings,
        KernelType kernel,
        AssetBindings assets,
        Constants constants,
        KernelBackendPreference backendPreference = KernelBackendPreference::Auto) noexcept(std::is_nothrow_move_constructible_v<QueryType> && std::is_nothrow_move_constructible_v<KernelType>)
        : query_(std::move(query))
        , settings_(settings)
        , backendPreference_(backendPreference)
        , kernel_(std::move(kernel))
        , assets_(assets)
        , constants_(constants) {
        ResolveBackend();
        Prepare();
    }

    CompiledKernelQuery(const CompiledKernelQuery&) = delete;
    CompiledKernelQuery& operator=(const CompiledKernelQuery&) = delete;
    CompiledKernelQuery(CompiledKernelQuery&&) noexcept = default;
    CompiledKernelQuery& operator=(CompiledKernelQuery&&) noexcept = default;

    [[nodiscard]] bool IsValid() const noexcept { return query_.IsValid(); }
    [[nodiscard]] QueryExecutionSettings Settings() const noexcept { return settings_; }
    [[nodiscard]] KernelBackendPreference BackendPreference() const noexcept { return backendPreference_; }
    [[nodiscard]] KernelBackend ResolvedBackend() const noexcept { return resolvedBackend_; }

    void SetSettings(QueryExecutionSettings settings) {
        settings_ = settings;
        Prepare();
    }

    void SetBackendPreference(KernelBackendPreference backendPreference) noexcept {
        backendPreference_ = backendPreference;
        ResolveBackend();
    }

    void Prepare() {
        if (query_.IsValid()) {
            query_.PrepareMutableBatchExecution(settings_, scratch_);
        }
    }

    void Execute() {
        if (!query_.IsValid()) {
            return;
        }

        query_.ForEachMutableBatchKernel(settings_, *this, scratch_);
    }

    void operator()(MutableBatchType& queryBatch) {
        BatchType batch{ queryBatch, assets_, constants_ };
        detail::InvokeKernelWithBackend(kernel_, batch, resolvedBackend_);
    }

private:
    void ResolveBackend() noexcept {
        resolvedBackend_ = detail::ResolveKernelBackend<KernelType, BatchType>(backendPreference_);
    }

    QueryType query_;
    QueryExecutionSettings settings_{};
    KernelBackendPreference backendPreference_ = KernelBackendPreference::Auto;
    KernelBackend resolvedBackend_ = KernelBackend::Scalar;
    KernelType kernel_;
    AssetBindings assets_;
    Constants constants_;
    QueryBatchExecutionScratch scratch_;
};

template <typename Contract>
class EditorKernelBinding {
public:
    using BatchType = KernelBatch<Contract>;
    using Function = void (*)(BatchType& batch, void* context);

    constexpr EditorKernelBinding() noexcept = default;

    constexpr EditorKernelBinding(Function function, void* context = nullptr) noexcept
        : function_(function)
        , context_(context) {}

    [[nodiscard]] bool IsBound() const noexcept { return function_ != nullptr; }
    [[nodiscard]] Function BoundFunction() const noexcept { return function_; }
    [[nodiscard]] void* Context() const noexcept { return context_; }

    void Bind(Function function, void* context = nullptr) noexcept {
        function_ = function;
        context_ = context;
    }

    void Clear() noexcept {
        function_ = nullptr;
        context_ = nullptr;
    }

    [[nodiscard]] bool Invoke(BatchType& batch) const {
        if (function_ == nullptr) {
            return false;
        }
        function_(batch, context_);
        return true;
    }

private:
    Function function_ = nullptr;
    void* context_ = nullptr;
};

template <typename Contract>
using EditorKernelBindingPtr = std::shared_ptr<EditorKernelBinding<Contract>>;

template <typename Contract>
[[nodiscard]] EditorKernelBindingPtr<Contract> MakeEditorKernelBinding(
    typename EditorKernelBinding<Contract>::Function function,
    void* context = nullptr) {
    return std::make_shared<EditorKernelBinding<Contract>>(function, context);
}

template <typename Contract>
class EditorCompiledKernelQuery {
public:
    using QueryType = KernelQuery<Contract>;
    using AssetBindings = KernelAssetBindings<Contract>;
    using Constants = KernelConstants<Contract>;
    using MutableBatchType = typename Contract::MutableBatchType;
    using BatchType = KernelBatch<Contract>;
    using Binding = EditorKernelBinding<Contract>;

    EditorCompiledKernelQuery(
        QueryType query,
        QueryExecutionSettings settings,
        EditorKernelBindingPtr<Contract> binding,
        AssetBindings assets,
        Constants constants) noexcept(std::is_nothrow_move_constructible_v<QueryType>)
        : query_(std::move(query))
        , settings_(settings)
        , binding_(std::move(binding))
        , assets_(assets)
        , constants_(constants) {
        Prepare();
    }

    EditorCompiledKernelQuery(const EditorCompiledKernelQuery&) = delete;
    EditorCompiledKernelQuery& operator=(const EditorCompiledKernelQuery&) = delete;
    EditorCompiledKernelQuery(EditorCompiledKernelQuery&&) noexcept = default;
    EditorCompiledKernelQuery& operator=(EditorCompiledKernelQuery&&) noexcept = default;

    [[nodiscard]] bool IsValid() const noexcept { return query_.IsValid() && HasKernelBinding(); }
    [[nodiscard]] bool HasKernelBinding() const noexcept { return binding_ != nullptr && binding_->IsBound(); }
    [[nodiscard]] QueryExecutionSettings Settings() const noexcept { return settings_; }
    [[nodiscard]] EditorKernelBindingPtr<Contract> KernelBinding() const noexcept { return binding_; }

    void SetSettings(QueryExecutionSettings settings) {
        settings_ = settings;
        Prepare();
    }

    void SetKernelBinding(EditorKernelBindingPtr<Contract> binding) noexcept {
        binding_ = std::move(binding);
    }

    void Prepare() {
        if (query_.IsValid()) {
            query_.PrepareMutableBatchExecution(settings_, scratch_);
        }
    }

    [[nodiscard]] bool Execute() {
        if (!query_.IsValid() || !HasKernelBinding()) {
            return false;
        }

        invocationFailed_ = false;
        query_.ForEachMutableBatchKernel(settings_, *this, scratch_);
        return !invocationFailed_;
    }

    void operator()(MutableBatchType& queryBatch) {
        BatchType batch{ queryBatch, assets_, constants_ };
        if (binding_ == nullptr || !binding_->Invoke(batch)) {
            invocationFailed_ = true;
        }
    }

private:
    QueryType query_;
    QueryExecutionSettings settings_{};
    EditorKernelBindingPtr<Contract> binding_;
    AssetBindings assets_;
    Constants constants_;
    QueryBatchExecutionScratch scratch_;
    bool invocationFailed_ = false;
};

namespace detail {

template <typename Contract>
struct CompiledKernelQueryFactory;

template <typename... InputTypes, typename... OutputTypes, typename... AssetTypes, typename ConstantsType>
struct CompiledKernelQueryFactory<
    KernelContract<KernelInputComponents<InputTypes...>, KernelOutputComponents<OutputTypes...>, KernelReadOnlyAssets<AssetTypes...>, ConstantsType>> {
    using Contract = KernelContract<KernelInputComponents<InputTypes...>, KernelOutputComponents<OutputTypes...>, KernelReadOnlyAssets<AssetTypes...>, ConstantsType>;

    template <typename Kernel>
    [[nodiscard]] static CompiledKernelQuery<Contract, std::decay_t<Kernel>> Compile(
        World& world,
        QueryExecutionSettings settings,
        KernelBackendPreference backendPreference,
        Kernel&& kernel,
        const KernelAssetBindings<Contract>& assets,
        const KernelConstants<Contract>& constants) {
        return CompiledKernelQuery<Contract, std::decay_t<Kernel>>{
            world.CreateQuery<InputTypes..., OutputTypes...>(),
            settings,
            std::forward<Kernel>(kernel),
            assets,
            constants,
            backendPreference,
        };
    }
};

template <typename Contract>
struct EditorCompiledKernelQueryFactory;

template <typename... InputTypes, typename... OutputTypes, typename... AssetTypes, typename ConstantsType>
struct EditorCompiledKernelQueryFactory<
    KernelContract<KernelInputComponents<InputTypes...>, KernelOutputComponents<OutputTypes...>, KernelReadOnlyAssets<AssetTypes...>, ConstantsType>> {
    using Contract = KernelContract<KernelInputComponents<InputTypes...>, KernelOutputComponents<OutputTypes...>, KernelReadOnlyAssets<AssetTypes...>, ConstantsType>;

    [[nodiscard]] static EditorCompiledKernelQuery<Contract> Compile(
        World& world,
        QueryExecutionSettings settings,
        EditorKernelBindingPtr<Contract> binding,
        const KernelAssetBindings<Contract>& assets,
        const KernelConstants<Contract>& constants) {
        return EditorCompiledKernelQuery<Contract>{
            world.CreateQuery<InputTypes..., OutputTypes...>(),
            settings,
            std::move(binding),
            assets,
            constants,
        };
    }
};

} // namespace detail

template <typename Contract, typename Kernel>
[[nodiscard]] CompiledKernelQuery<Contract, std::decay_t<Kernel>> CompileKernelQuery(
    KernelQuery<Contract> query,
    QueryExecutionSettings settings,
    KernelBackendPreference backendPreference,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    return CompiledKernelQuery<Contract, std::decay_t<Kernel>>{
        std::move(query),
        settings,
        std::forward<Kernel>(kernel),
        assets,
        constants,
        backendPreference,
    };
}

template <typename Contract, typename Kernel>
[[nodiscard]] CompiledKernelQuery<Contract, std::decay_t<Kernel>> CompileKernelQuery(
    KernelQuery<Contract> query,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    return CompileKernelQuery<Contract>(
        std::move(query),
        settings,
        KernelBackendPreference::Auto,
        std::forward<Kernel>(kernel),
        assets,
        constants);
}

template <typename Contract, typename Kernel>
[[nodiscard]] CompiledKernelQuery<Contract, std::decay_t<Kernel>> CompileKernelQuery(
    KernelQuery<Contract> query,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    return CompileKernelQuery<Contract>(std::move(query), QueryExecutionSettings{}, std::forward<Kernel>(kernel), assets, constants);
}

template <typename Contract, typename Kernel>
[[nodiscard]] CompiledKernelQuery<Contract, std::decay_t<Kernel>> CompileKernelQuery(
    World& world,
    QueryExecutionSettings settings,
    KernelBackendPreference backendPreference,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    return detail::CompiledKernelQueryFactory<Contract>::Compile(
        world,
        settings,
        backendPreference,
        std::forward<Kernel>(kernel),
        assets,
        constants);
}

template <typename Contract, typename Kernel>
[[nodiscard]] CompiledKernelQuery<Contract, std::decay_t<Kernel>> CompileKernelQuery(
    World& world,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    return CompileKernelQuery<Contract>(
        world,
        settings,
        KernelBackendPreference::Auto,
        std::forward<Kernel>(kernel),
        assets,
        constants);
}

template <typename Contract, typename Kernel>
[[nodiscard]] CompiledKernelQuery<Contract, std::decay_t<Kernel>> CompileKernelQuery(
    World& world,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    return CompileKernelQuery<Contract>(world, QueryExecutionSettings{}, std::forward<Kernel>(kernel), assets, constants);
}

template <typename Contract, typename Kernel>
class CompiledKernelSystem : public System {
public:
    using KernelType = std::decay_t<Kernel>;
    using AssetBindings = KernelAssetBindings<Contract>;
    using Constants = KernelConstants<Contract>;
    using CompiledQuery = CompiledKernelQuery<Contract, KernelType>;

    explicit CompiledKernelSystem(
        QueryExecutionSettings settings,
        KernelType kernel,
        AssetBindings assets = AssetBindings{},
        Constants constants = Constants{},
        KernelBackendPreference backendPreference = KernelBackendPreference::Auto)
        : settings_(settings)
        , kernel_(std::move(kernel))
        , assets_(assets)
        , constants_(constants)
        , backendPreference_(backendPreference) {}

    [[nodiscard]] SystemAccess DeclareAccess(World& world) const override {
        return DeclareKernelAccess<Contract>(world);
    }

    [[nodiscard]] std::string_view ExecutionPathName() const noexcept override {
        return "compiled_kernel";
    }

    void OnCreate(World& world) override {
        compiled_ = std::make_unique<CompiledQuery>(
            CompileKernelQuery<Contract>(
                world,
                RuntimeSettings(),
                backendPreference_,
                kernel_,
                assets_,
                constants_));
    }

    void SetExecutionWorkerPool(WorkerPool* workerPool) noexcept override {
        executionWorkerPool_ = workerPool;
    }

    void OnUpdate(World& world, float deltaSeconds) override {
        static_cast<void>(world);
        static_cast<void>(deltaSeconds);
        if (compiled_ == nullptr || !compiled_->IsValid()) {
            executionWorkerPool_ = nullptr;
            return;
        }

        compiled_->SetSettings(RuntimeSettings());
        compiled_->Execute();
        executionWorkerPool_ = nullptr;
    }

    [[nodiscard]] const CompiledQuery* Compiled() const noexcept {
        return compiled_.get();
    }

private:
    [[nodiscard]] QueryExecutionSettings RuntimeSettings() const noexcept {
        QueryExecutionSettings settings = settings_;
        if (settings.workerPool == nullptr) {
            settings.workerPool = executionWorkerPool_;
        }
        return settings;
    }

    QueryExecutionSettings settings_{};
    KernelType kernel_;
    AssetBindings assets_;
    Constants constants_;
    KernelBackendPreference backendPreference_ = KernelBackendPreference::Auto;
    WorkerPool* executionWorkerPool_ = nullptr;
    std::unique_ptr<CompiledQuery> compiled_;
};

template <typename Contract>
[[nodiscard]] EditorCompiledKernelQuery<Contract> CompileEditorKernelQuery(
    KernelQuery<Contract> query,
    QueryExecutionSettings settings,
    EditorKernelBindingPtr<Contract> binding,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    return EditorCompiledKernelQuery<Contract>{
        std::move(query),
        settings,
        std::move(binding),
        assets,
        constants,
    };
}

template <typename Contract>
[[nodiscard]] EditorCompiledKernelQuery<Contract> CompileEditorKernelQuery(
    KernelQuery<Contract> query,
    QueryExecutionSettings settings,
    typename EditorKernelBinding<Contract>::Function function,
    void* context,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    return CompileEditorKernelQuery<Contract>(
        std::move(query),
        settings,
        MakeEditorKernelBinding<Contract>(function, context),
        assets,
        constants);
}

template <typename Contract>
[[nodiscard]] EditorCompiledKernelQuery<Contract> CompileEditorKernelQuery(
    World& world,
    QueryExecutionSettings settings,
    EditorKernelBindingPtr<Contract> binding,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    return detail::EditorCompiledKernelQueryFactory<Contract>::Compile(
        world,
        settings,
        std::move(binding),
        assets,
        constants);
}

template <typename Contract>
[[nodiscard]] EditorCompiledKernelQuery<Contract> CompileEditorKernelQuery(
    World& world,
    QueryExecutionSettings settings,
    typename EditorKernelBinding<Contract>::Function function,
    void* context,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    return CompileEditorKernelQuery<Contract>(
        world,
        settings,
        MakeEditorKernelBinding<Contract>(function, context),
        assets,
        constants);
}

template <typename... Inputs, typename... Outputs, typename... Assets, typename Constants>
[[nodiscard]] SystemAccess DeclareKernelAccess(
    World& world,
    KernelContract<KernelInputComponents<Inputs...>, KernelOutputComponents<Outputs...>, KernelReadOnlyAssets<Assets...>, Constants>* = nullptr) {
    SystemAccess access;
    (access.Read<Inputs>(world), ...);
    (access.Write<Outputs>(world), ...);
    return access;
}

template <typename Contract>
[[nodiscard]] SystemAccess DeclareKernelAccess(World& world) {
    return DeclareKernelAccess(world, static_cast<Contract*>(nullptr));
}

template <typename Contract, typename Kernel>
void ExecuteKernelScalar(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants,
    QueryBatchExecutionScratch& scratch) {
    if (!query.IsValid()) {
        return;
    }

    using KernelType = std::remove_reference_t<Kernel>;
    struct ScalarInvocation {
        KernelType* kernel = nullptr;
        const KernelAssetBindings<Contract>* assets = nullptr;
        const KernelConstants<Contract>* constants = nullptr;

        void operator()(typename Contract::MutableBatchType& queryBatch) {
            KernelBatch<Contract> batch{ queryBatch, *assets, *constants };
            detail::InvokeKernelScalar(*kernel, batch);
        }
    };

    ScalarInvocation invocation{
        .kernel = std::addressof(kernel),
        .assets = std::addressof(assets),
        .constants = std::addressof(constants),
    };
    query.ForEachMutableBatchKernel(settings, invocation, scratch);
}

template <typename Contract, typename Kernel>
void ExecuteKernelScalar(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    QueryBatchExecutionScratch scratch;
    ExecuteKernelScalar<Contract>(
        query,
        settings,
        std::forward<Kernel>(kernel),
        assets,
        constants,
        scratch);
}

template <typename Contract, typename Kernel>
void ExecuteKernelScalar(
    KernelQuery<Contract>& query,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants,
    QueryBatchExecutionScratch& scratch) {
    ExecuteKernelScalar<Contract>(query, QueryExecutionSettings{}, std::forward<Kernel>(kernel), assets, constants, scratch);
}

template <typename Contract, typename Kernel>
void ExecuteKernelScalar(
    KernelQuery<Contract>& query,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    ExecuteKernelScalar<Contract>(query, QueryExecutionSettings{}, std::forward<Kernel>(kernel), assets, constants);
}

template <typename Contract, typename Kernel>
void ExecuteKernel(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    KernelBackendPreference backendPreference,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants,
    QueryBatchExecutionScratch& scratch) {
    if (!query.IsValid()) {
        return;
    }

    using KernelType = std::remove_reference_t<Kernel>;
    using BatchType = KernelBatch<Contract>;
    static_assert(detail::CanInvokeKernelScalar<KernelType, BatchType>, "ECS kernels must provide a scalar KernelBatch fallback");

    struct Invocation {
        KernelType* kernel = nullptr;
        const KernelAssetBindings<Contract>* assets = nullptr;
        const KernelConstants<Contract>* constants = nullptr;
        KernelBackend backend = KernelBackend::Scalar;

        void operator()(typename Contract::MutableBatchType& queryBatch) {
            KernelBatch<Contract> batch{ queryBatch, *assets, *constants };
            detail::InvokeKernelWithBackend(*kernel, batch, backend);
        }
    };

    Invocation invocation{
        .kernel = std::addressof(kernel),
        .assets = std::addressof(assets),
        .constants = std::addressof(constants),
        .backend = detail::ResolveKernelBackend<KernelType, BatchType>(backendPreference),
    };
    query.ForEachMutableBatchKernel(settings, invocation, scratch);
}

template <typename Contract, typename Kernel>
void ExecuteKernel(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    KernelBackendPreference backendPreference,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    QueryBatchExecutionScratch scratch;
    ExecuteKernel<Contract>(
        query,
        settings,
        backendPreference,
        std::forward<Kernel>(kernel),
        assets,
        constants,
        scratch);
}

template <typename Contract, typename Kernel>
void ExecuteKernel(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants,
    QueryBatchExecutionScratch& scratch) {
    ExecuteKernel<Contract>(
        query,
        settings,
        KernelBackendPreference::Auto,
        std::forward<Kernel>(kernel),
        assets,
        constants,
        scratch);
}

template <typename Contract, typename Kernel>
void ExecuteKernel(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    QueryBatchExecutionScratch scratch;
    ExecuteKernel<Contract>(
        query,
        settings,
        std::forward<Kernel>(kernel),
        assets,
        constants,
        scratch);
}

template <typename Contract, typename Kernel>
void ExecuteKernel(
    KernelQuery<Contract>& query,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants,
    QueryBatchExecutionScratch& scratch) {
    ExecuteKernel<Contract>(query, QueryExecutionSettings{}, std::forward<Kernel>(kernel), assets, constants, scratch);
}

template <typename Contract, typename Kernel>
void ExecuteKernel(
    KernelQuery<Contract>& query,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    ExecuteKernel<Contract>(query, QueryExecutionSettings{}, std::forward<Kernel>(kernel), assets, constants);
}

template <typename Contract, typename Kernel>
void ExecuteKernelNeon(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants,
    QueryBatchExecutionScratch& scratch) {
    ExecuteKernel<Contract>(
        query,
        settings,
        KernelBackendPreference::Neon,
        std::forward<Kernel>(kernel),
        assets,
        constants,
        scratch);
}

template <typename Contract, typename Kernel>
void ExecuteKernelNeon(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    ExecuteKernel<Contract>(
        query,
        settings,
        KernelBackendPreference::Neon,
        std::forward<Kernel>(kernel),
        assets,
        constants);
}

template <typename Contract, typename Kernel>
void ExecuteKernelSse2(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants,
    QueryBatchExecutionScratch& scratch) {
    ExecuteKernel<Contract>(
        query,
        settings,
        KernelBackendPreference::Sse2,
        std::forward<Kernel>(kernel),
        assets,
        constants,
        scratch);
}

template <typename Contract, typename Kernel>
void ExecuteKernelSse2(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    ExecuteKernel<Contract>(
        query,
        settings,
        KernelBackendPreference::Sse2,
        std::forward<Kernel>(kernel),
        assets,
        constants);
}

template <typename Contract, typename Kernel>
void ExecuteKernelAvx2(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants,
    QueryBatchExecutionScratch& scratch) {
    ExecuteKernel<Contract>(
        query,
        settings,
        KernelBackendPreference::Avx2,
        std::forward<Kernel>(kernel),
        assets,
        constants,
        scratch);
}

template <typename Contract, typename Kernel>
void ExecuteKernelAvx2(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    ExecuteKernel<Contract>(
        query,
        settings,
        KernelBackendPreference::Avx2,
        std::forward<Kernel>(kernel),
        assets,
        constants);
}

template <typename Contract, typename Kernel>
void ExecuteKernelAvx512(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants,
    QueryBatchExecutionScratch& scratch) {
    ExecuteKernel<Contract>(
        query,
        settings,
        KernelBackendPreference::Avx512,
        std::forward<Kernel>(kernel),
        assets,
        constants,
        scratch);
}

template <typename Contract, typename Kernel>
void ExecuteKernelAvx512(
    KernelQuery<Contract>& query,
    QueryExecutionSettings settings,
    Kernel&& kernel,
    const KernelAssetBindings<Contract>& assets,
    const KernelConstants<Contract>& constants) {
    ExecuteKernel<Contract>(
        query,
        settings,
        KernelBackendPreference::Avx512,
        std::forward<Kernel>(kernel),
        assets,
        constants);
}

} // namespace kb::ecs
