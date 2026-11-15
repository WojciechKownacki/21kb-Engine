#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace kb::script {

struct PucLuaMemoryBlock;
class PucLuaMemoryArena;

// The raw memory under every Lua state's allocator (the arena and the debugger's expression state), with the contract of a
// Lua allocator: null block allocates, zero size frees, otherwise resizes. LuaJIT keeps the address of every object it
// collects in 47 bits. Where malloc hands out such addresses (Windows, Linux, older Android) it is used, the fastest
// choice; where it tags the pointers it returns (the top byte is set, as on newer Android) a LuaJIT build takes its
// memory from LuaJIT's own allocator, reached through a state that is never run. Lua 5.4 always uses malloc.
class PucLuaRawHeap final {
public:
    explicit PucLuaRawHeap(bool alwaysUseVmAllocator = false) noexcept; // true: LuaJIT builds use its allocator on every platform (tests)
    ~PucLuaRawHeap();
    PucLuaRawHeap(const PucLuaRawHeap&) = delete;
    PucLuaRawHeap& operator=(const PucLuaRawHeap&) = delete;
    [[nodiscard]] void* Resize(void* block, std::size_t oldSize, std::size_t newSize) noexcept;
private:
    [[maybe_unused]] void* host_ = nullptr; // Lua 5.4 needs no host state
    void* (*allocate_)(void*, void*, std::size_t, std::size_t) = nullptr;
    void* allocatorData_ = nullptr;
};

class PucLuaHeapImage final {
public:
    PucLuaHeapImage() = default;
    ~PucLuaHeapImage();
    PucLuaHeapImage(const PucLuaHeapImage&) = delete;
    PucLuaHeapImage& operator=(const PucLuaHeapImage&) = delete;
    [[nodiscard]] std::size_t PayloadBytes() const noexcept { return payloadBytes_; }
    [[nodiscard]] std::size_t OwnedBytes() const noexcept { return ownedBytes_; }
private:
    friend class PucLuaMemoryArena;
    struct Record {
        PucLuaMemoryBlock* block = nullptr;
        std::size_t size = 0U;
        std::vector<std::byte> bytes;
    };
    std::shared_ptr<PucLuaMemoryArena> arena_;
    std::vector<Record> records_;
    std::size_t payloadBytes_ = 0U;
    std::size_t ownedBytes_ = 0U;
    bool pinned_ = false;
};

// Every image pins the addresses it records. A later grow moves pinned blocks
// into new storage, while free leaves their original addresses available to restore.
class PucLuaMemoryArena final : public std::enable_shared_from_this<PucLuaMemoryArena> {
public:
    PucLuaMemoryArena() = default;
    ~PucLuaMemoryArena();
    PucLuaMemoryArena(const PucLuaMemoryArena&) = delete;
    PucLuaMemoryArena& operator=(const PucLuaMemoryArena&) = delete;
    void* Resize(void* pointer, std::size_t size, std::size_t limit, bool enforce, bool& refused) noexcept;
    [[nodiscard]] std::unique_ptr<PucLuaHeapImage> Capture(std::size_t limit, std::string& error);
    [[nodiscard]] bool Restore(const PucLuaHeapImage& image, std::string& error);
    [[nodiscard]] std::size_t LiveBytes() const noexcept { return liveBytes_; }
    [[nodiscard]] std::size_t ResidentBytes() const noexcept { return residentBytes_ + imageBytes_; }
private:
    friend class PucLuaHeapImage;
    void Release(PucLuaMemoryBlock* block) noexcept;
    void Free(PucLuaMemoryBlock* block) noexcept;
    PucLuaRawHeap heap_; // outlives the blocks: the destructor frees them first
    PucLuaMemoryBlock* blocks_ = nullptr;
    std::size_t liveBytes_ = 0U;
    std::size_t residentBytes_ = 0U;
    std::size_t imageBytes_ = 0U;
};

} // namespace kb::script
