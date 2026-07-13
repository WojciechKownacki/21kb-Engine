#pragma once

#include "engine/library/EngineLibraryInputLimits.hpp"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <span>
#include <utility>
#include <vector>

namespace kb::library {

// LIB-058: controlled, bounded collection types for script data (any T with
// operator==, including kb::script::ScriptValue itself — see
// EngineLibraryTests.cpp::RunCollectionsScriptValueTest). "Controlled" means
// every mutation that can grow a collection returns bool and refuses to
// exceed a hard capacity instead of growing unboundedly or throwing; it is
// never possible to construct one with a capacity above
// kDefaultLibraryInputLimits.maxCollectionSize (LIB-037's reserved policy
// value), even if a larger number is requested, so the shared limit cannot
// be bypassed per call site.
//
// Set<T>/Map<K,V> are vector-backed with linear (O(n)) membership/key
// lookup via operator==, not hash-backed: this keeps them generic over any
// T with operator== (no std::hash<T> specialization required — ScriptValue
// has none today).
//
// LIB-060 (deterministic iteration): Set<T>/Map<K,V> (and their NonAlloc
// counterparts below) formally GUARANTEE their iteration order — this is a
// contract callers may depend on (e.g. deterministic replay, snapshot
// diffing across two runs with the same input sequence), not an
// unspecified implementation detail. The exact rule: currently-present
// members/keys appear in the order they were most recently (re-)inserted.
// Insert/Set always appends a genuinely new member/key at the end; it
// never reorders existing members even when it updates one (Map::Set on
// an existing key updates its value in place, at its existing position).
// Remove always erases in place (shifting later elements down), never a
// swap-with-back, so removing one member never reorders the rest. The one
// subtlety: removing a member and then re-inserting the same value/key
// does NOT restore its old position — it reappears at the end, like any
// other new insertion, since by the time it is re-inserted it is, from
// the collection's point of view, simply absent and then newly added.
// RunCollectionsDeterministicIterationTest exercises exactly this
// insert/remove/re-insert sequence for both Set and Map, alloc and
// NonAlloc, to prove the guarantee is real and not just documented.
//
// LIB-059 (allocation cost): Array/Set/Map/Stack reserve() their backing
// std::vector to `capacity_` once, in the constructor — after that, every
// mutation up to Capacity() is guaranteed not to trigger a further
// reallocation (verified by AllocatedCapacity() staying constant; see
// RunCollectionsAllocationCostTest). Exactly one allocation, sized to the
// declared capacity, is the whole allocation cost of these four types.
// Queue<T> is the one exception: it is std::deque-backed for O(1)
// push/pop at both ends, and std::deque has no reserve() — its allocation
// cost is inherently a sequence of fixed-size block allocations as it
// grows, not one upfront allocation. This is a real, documented trade-off
// (deque's chunked structure is what gives it O(1) at both ends), not an
// oversight; a caller on a genuine allocation-free hot path should use
// QueueNonAlloc instead (below), which is a proper O(1) ring buffer over
// caller-provided storage and never allocates at all.
[[nodiscard]] constexpr std::size_t ClampCollectionCapacity(std::size_t requested) noexcept {
    return requested < kDefaultLibraryInputLimits.maxCollectionSize ? requested
                                                                     : kDefaultLibraryInputLimits.maxCollectionSize;
}

template <typename K, typename V>
struct MapEntry {
    K key;
    V value;
};

template <typename T>
class Array {
public:
    explicit Array(std::size_t capacity = kDefaultLibraryInputLimits.maxCollectionSize)
        : capacity_(ClampCollectionCapacity(capacity)) {
        items_.reserve(capacity_);
    }

    [[nodiscard]] std::size_t Count() const noexcept { return items_.size(); }
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }
    // The backing std::vector's actual reserved capacity — always >=
    // Capacity() after construction and never grows past it, proving the
    // "single upfront allocation" claim (LIB-059) rather than just
    // asserting it in a comment.
    [[nodiscard]] std::size_t AllocatedCapacity() const noexcept { return items_.capacity(); }
    [[nodiscard]] bool Empty() const noexcept { return items_.empty(); }
    [[nodiscard]] bool Full() const noexcept { return items_.size() >= capacity_; }

    [[nodiscard]] bool PushBack(T value) {
        if (Full()) {
            return false;
        }
        items_.push_back(std::move(value));
        return true;
    }

    [[nodiscard]] bool RemoveAt(std::size_t index) {
        if (index >= items_.size()) {
            return false;
        }
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    [[nodiscard]] bool SetAt(std::size_t index, T value) {
        if (index >= items_.size()) {
            return false;
        }
        items_[index] = std::move(value);
        return true;
    }

    [[nodiscard]] const T* GetAt(std::size_t index) const noexcept {
        return index < items_.size() ? &items_[index] : nullptr;
    }

    void Clear() noexcept { items_.clear(); }

    [[nodiscard]] auto begin() const noexcept { return items_.begin(); }
    [[nodiscard]] auto end() const noexcept { return items_.end(); }

private:
    std::vector<T> items_;
    std::size_t capacity_;
};

template <typename T>
class Set {
public:
    explicit Set(std::size_t capacity = kDefaultLibraryInputLimits.maxCollectionSize)
        : capacity_(ClampCollectionCapacity(capacity)) {
        items_.reserve(capacity_);
    }

    [[nodiscard]] std::size_t Count() const noexcept { return items_.size(); }
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t AllocatedCapacity() const noexcept { return items_.capacity(); }
    [[nodiscard]] bool Empty() const noexcept { return items_.empty(); }

    [[nodiscard]] bool Contains(const T& value) const noexcept {
        return std::find(items_.begin(), items_.end(), value) != items_.end();
    }

    // Returns true iff `value` is a member of the set after this call:
    // inserting a value already present is a no-op that still returns
    // true. Returns false only when `value` is new AND the set is already
    // at capacity — a real insertion failure, not a silently dropped
    // duplicate.
    [[nodiscard]] bool Insert(T value) {
        if (Contains(value)) {
            return true;
        }
        if (items_.size() >= capacity_) {
            return false;
        }
        items_.push_back(std::move(value));
        return true;
    }

    bool Remove(const T& value) {
        const auto it = std::find(items_.begin(), items_.end(), value);
        if (it == items_.end()) {
            return false;
        }
        items_.erase(it);
        return true;
    }

    void Clear() noexcept { items_.clear(); }

    [[nodiscard]] auto begin() const noexcept { return items_.begin(); }
    [[nodiscard]] auto end() const noexcept { return items_.end(); }

private:
    std::vector<T> items_;
    std::size_t capacity_;
};

template <typename K, typename V>
class Map {
public:
    using Entry = MapEntry<K, V>;

    explicit Map(std::size_t capacity = kDefaultLibraryInputLimits.maxCollectionSize)
        : capacity_(ClampCollectionCapacity(capacity)) {
        entries_.reserve(capacity_);
    }

    [[nodiscard]] std::size_t Count() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t AllocatedCapacity() const noexcept { return entries_.capacity(); }
    [[nodiscard]] bool Empty() const noexcept { return entries_.empty(); }

    [[nodiscard]] const V* Find(const K& key) const noexcept {
        for (const Entry& entry : entries_) {
            if (entry.key == key) {
                return &entry.value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool ContainsKey(const K& key) const noexcept { return Find(key) != nullptr; }

    // Returns false only when `key` is new AND the map is already at
    // capacity; updating the value of an existing key never fails.
    [[nodiscard]] bool Set(K key, V value) {
        for (Entry& entry : entries_) {
            if (entry.key == key) {
                entry.value = std::move(value);
                return true;
            }
        }
        if (entries_.size() >= capacity_) {
            return false;
        }
        entries_.push_back(Entry{ std::move(key), std::move(value) });
        return true;
    }

    bool Remove(const K& key) {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].key == key) {
                entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    void Clear() noexcept { entries_.clear(); }

    [[nodiscard]] auto begin() const noexcept { return entries_.begin(); }
    [[nodiscard]] auto end() const noexcept { return entries_.end(); }

private:
    std::vector<Entry> entries_;
    std::size_t capacity_;
};

template <typename T>
class Queue {
public:
    explicit Queue(std::size_t capacity = kDefaultLibraryInputLimits.maxCollectionSize)
        : capacity_(ClampCollectionCapacity(capacity)) {}

    [[nodiscard]] std::size_t Count() const noexcept { return items_.size(); }
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool Empty() const noexcept { return items_.empty(); }

    [[nodiscard]] bool Enqueue(T value) {
        if (items_.size() >= capacity_) {
            return false;
        }
        items_.push_back(std::move(value));
        return true;
    }

    // FIFO: removes and returns (via outValue) the oldest enqueued item.
    // Returns false, leaving outValue untouched, when the queue is empty.
    [[nodiscard]] bool Dequeue(T& outValue) {
        if (items_.empty()) {
            return false;
        }
        outValue = std::move(items_.front());
        items_.pop_front();
        return true;
    }

    [[nodiscard]] const T* Peek() const noexcept { return items_.empty() ? nullptr : &items_.front(); }

    void Clear() noexcept { items_.clear(); }

private:
    std::deque<T> items_;
    std::size_t capacity_;
};

template <typename T>
class Stack {
public:
    explicit Stack(std::size_t capacity = kDefaultLibraryInputLimits.maxCollectionSize)
        : capacity_(ClampCollectionCapacity(capacity)) {
        items_.reserve(capacity_);
    }

    [[nodiscard]] std::size_t Count() const noexcept { return items_.size(); }
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t AllocatedCapacity() const noexcept { return items_.capacity(); }
    [[nodiscard]] bool Empty() const noexcept { return items_.empty(); }

    [[nodiscard]] bool Push(T value) {
        if (items_.size() >= capacity_) {
            return false;
        }
        items_.push_back(std::move(value));
        return true;
    }

    // LIFO: removes and returns (via outValue) the most recently pushed
    // item. Returns false, leaving outValue untouched, when the stack is
    // empty.
    [[nodiscard]] bool Pop(T& outValue) {
        if (items_.empty()) {
            return false;
        }
        outValue = std::move(items_.back());
        items_.pop_back();
        return true;
    }

    [[nodiscard]] const T* Top() const noexcept { return items_.empty() ? nullptr : &items_.back(); }

    void Clear() noexcept { items_.clear(); }

private:
    std::vector<T> items_;
    std::size_t capacity_;
};

// LIB-059: NonAlloc variants for the hot path (e.g. per-frame scratch data)
// — every one of these operates entirely over a std::span<T> the caller
// already owns (a stack array, a thread_local buffer, a frame arena slice)
// and never allocates memory itself. Capacity is fixed at construction to
// the supplied span's size; there is no separate capacity parameter to
// clamp against kDefaultLibraryInputLimits.maxCollectionSize because the
// caller-owned storage IS the bound, chosen by the caller. Semantics
// otherwise match the owning counterparts above (same bool-returning
// mutation contract, same FIFO/LIFO ordering for Queue/Stack).
template <typename T>
class ArrayNonAlloc {
public:
    explicit ArrayNonAlloc(std::span<T> storage) noexcept
        : storage_(storage) {}

    [[nodiscard]] std::size_t Count() const noexcept { return count_; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return storage_.size(); }
    [[nodiscard]] bool Empty() const noexcept { return count_ == 0U; }
    [[nodiscard]] bool Full() const noexcept { return count_ >= storage_.size(); }

    [[nodiscard]] bool PushBack(T value) {
        if (Full()) {
            return false;
        }
        storage_[count_] = std::move(value);
        ++count_;
        return true;
    }

    [[nodiscard]] bool RemoveAt(std::size_t index) {
        if (index >= count_) {
            return false;
        }
        for (std::size_t i = index; i + 1U < count_; ++i) {
            storage_[i] = std::move(storage_[i + 1U]);
        }
        --count_;
        return true;
    }

    [[nodiscard]] bool SetAt(std::size_t index, T value) {
        if (index >= count_) {
            return false;
        }
        storage_[index] = std::move(value);
        return true;
    }

    [[nodiscard]] const T* GetAt(std::size_t index) const noexcept { return index < count_ ? &storage_[index] : nullptr; }

    void Clear() noexcept { count_ = 0U; }

    [[nodiscard]] auto begin() const noexcept { return storage_.begin(); }
    [[nodiscard]] auto end() const noexcept { return storage_.begin() + static_cast<std::ptrdiff_t>(count_); }

private:
    std::span<T> storage_;
    std::size_t count_ = 0U;
};

template <typename T>
class SetNonAlloc {
public:
    explicit SetNonAlloc(std::span<T> storage) noexcept
        : storage_(storage) {}

    [[nodiscard]] std::size_t Count() const noexcept { return count_; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return storage_.size(); }
    [[nodiscard]] bool Empty() const noexcept { return count_ == 0U; }

    [[nodiscard]] bool Contains(const T& value) const noexcept {
        for (std::size_t i = 0; i < count_; ++i) {
            if (storage_[i] == value) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool Insert(T value) {
        if (Contains(value)) {
            return true;
        }
        if (count_ >= storage_.size()) {
            return false;
        }
        storage_[count_] = std::move(value);
        ++count_;
        return true;
    }

    bool Remove(const T& value) {
        for (std::size_t i = 0; i < count_; ++i) {
            if (storage_[i] == value) {
                for (std::size_t j = i; j + 1U < count_; ++j) {
                    storage_[j] = std::move(storage_[j + 1U]);
                }
                --count_;
                return true;
            }
        }
        return false;
    }

    void Clear() noexcept { count_ = 0U; }

    [[nodiscard]] auto begin() const noexcept { return storage_.begin(); }
    [[nodiscard]] auto end() const noexcept { return storage_.begin() + static_cast<std::ptrdiff_t>(count_); }

private:
    std::span<T> storage_;
    std::size_t count_ = 0U;
};

template <typename K, typename V>
class MapNonAlloc {
public:
    explicit MapNonAlloc(std::span<MapEntry<K, V>> storage) noexcept
        : storage_(storage) {}

    [[nodiscard]] std::size_t Count() const noexcept { return count_; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return storage_.size(); }
    [[nodiscard]] bool Empty() const noexcept { return count_ == 0U; }

    [[nodiscard]] const V* Find(const K& key) const noexcept {
        for (std::size_t i = 0; i < count_; ++i) {
            if (storage_[i].key == key) {
                return &storage_[i].value;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool ContainsKey(const K& key) const noexcept { return Find(key) != nullptr; }

    [[nodiscard]] bool Set(K key, V value) {
        for (std::size_t i = 0; i < count_; ++i) {
            if (storage_[i].key == key) {
                storage_[i].value = std::move(value);
                return true;
            }
        }
        if (count_ >= storage_.size()) {
            return false;
        }
        storage_[count_] = MapEntry<K, V>{ std::move(key), std::move(value) };
        ++count_;
        return true;
    }

    bool Remove(const K& key) {
        for (std::size_t i = 0; i < count_; ++i) {
            if (storage_[i].key == key) {
                for (std::size_t j = i; j + 1U < count_; ++j) {
                    storage_[j] = std::move(storage_[j + 1U]);
                }
                --count_;
                return true;
            }
        }
        return false;
    }

    void Clear() noexcept { count_ = 0U; }

    [[nodiscard]] auto begin() const noexcept { return storage_.begin(); }
    [[nodiscard]] auto end() const noexcept { return storage_.begin() + static_cast<std::ptrdiff_t>(count_); }

private:
    std::span<MapEntry<K, V>> storage_;
    std::size_t count_ = 0U;
};

// A proper O(1)-enqueue/O(1)-dequeue ring buffer over caller-provided
// storage — strictly better for the hot path than the owning Queue<T>
// above, which is std::deque-backed and therefore not allocation-free.
template <typename T>
class QueueNonAlloc {
public:
    explicit QueueNonAlloc(std::span<T> storage) noexcept
        : storage_(storage) {}

    [[nodiscard]] std::size_t Count() const noexcept { return count_; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return storage_.size(); }
    [[nodiscard]] bool Empty() const noexcept { return count_ == 0U; }

    [[nodiscard]] bool Enqueue(T value) {
        if (count_ >= storage_.size()) {
            return false;
        }
        storage_[(head_ + count_) % storage_.size()] = std::move(value);
        ++count_;
        return true;
    }

    [[nodiscard]] bool Dequeue(T& outValue) {
        if (count_ == 0U) {
            return false;
        }
        outValue = std::move(storage_[head_]);
        head_ = (head_ + 1U) % storage_.size();
        --count_;
        return true;
    }

    [[nodiscard]] const T* Peek() const noexcept { return count_ == 0U ? nullptr : &storage_[head_]; }

    void Clear() noexcept {
        head_ = 0U;
        count_ = 0U;
    }

private:
    std::span<T> storage_;
    std::size_t head_ = 0U;
    std::size_t count_ = 0U;
};

template <typename T>
class StackNonAlloc {
public:
    explicit StackNonAlloc(std::span<T> storage) noexcept
        : storage_(storage) {}

    [[nodiscard]] std::size_t Count() const noexcept { return count_; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return storage_.size(); }
    [[nodiscard]] bool Empty() const noexcept { return count_ == 0U; }

    [[nodiscard]] bool Push(T value) {
        if (count_ >= storage_.size()) {
            return false;
        }
        storage_[count_] = std::move(value);
        ++count_;
        return true;
    }

    [[nodiscard]] bool Pop(T& outValue) {
        if (count_ == 0U) {
            return false;
        }
        --count_;
        outValue = std::move(storage_[count_]);
        return true;
    }

    [[nodiscard]] const T* Top() const noexcept { return count_ == 0U ? nullptr : &storage_[count_ - 1U]; }

    void Clear() noexcept { count_ = 0U; }

private:
    std::span<T> storage_;
    std::size_t count_ = 0U;
};

} // namespace kb::library
