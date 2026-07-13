#pragma once

#include "engine/library/EngineLibraryInputLimits.hpp"

#include <algorithm>
#include <cstddef>
#include <deque>
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
// has none today) and makes iteration order equal to insertion order as an
// incidental property of the representation. LIB-059 (allocation cost /
// NonAlloc variants) and LIB-060 (deterministic map/set iteration) revisit
// both the performance and the iteration-order guarantee formally; this is
// the honest MVP shape those tasks build on, not a hidden shortcut.
[[nodiscard]] constexpr std::size_t ClampCollectionCapacity(std::size_t requested) noexcept {
    return requested < kDefaultLibraryInputLimits.maxCollectionSize ? requested
                                                                     : kDefaultLibraryInputLimits.maxCollectionSize;
}

template <typename T>
class Array {
public:
    explicit Array(std::size_t capacity = kDefaultLibraryInputLimits.maxCollectionSize)
        : capacity_(ClampCollectionCapacity(capacity)) {}

    [[nodiscard]] std::size_t Count() const noexcept { return items_.size(); }
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }
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
        : capacity_(ClampCollectionCapacity(capacity)) {}

    [[nodiscard]] std::size_t Count() const noexcept { return items_.size(); }
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }
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
    struct Entry {
        K key;
        V value;
    };

    explicit Map(std::size_t capacity = kDefaultLibraryInputLimits.maxCollectionSize)
        : capacity_(ClampCollectionCapacity(capacity)) {}

    [[nodiscard]] std::size_t Count() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }
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
        entries_.push_back(Entry{std::move(key), std::move(value)});
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
        : capacity_(ClampCollectionCapacity(capacity)) {}

    [[nodiscard]] std::size_t Count() const noexcept { return items_.size(); }
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }
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

} // namespace kb::library
