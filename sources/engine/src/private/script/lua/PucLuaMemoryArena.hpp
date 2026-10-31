#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace kb::script {

struct PucLuaMemoryBlock;
class PucLuaMemoryArena;

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
    PucLuaMemoryBlock* blocks_ = nullptr;
    std::size_t liveBytes_ = 0U;
    std::size_t residentBytes_ = 0U;
    std::size_t imageBytes_ = 0U;
};

} // namespace kb::script
