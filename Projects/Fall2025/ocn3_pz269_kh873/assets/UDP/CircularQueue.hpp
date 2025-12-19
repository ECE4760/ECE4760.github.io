#pragma once

#include <array>
#include <optional>
#include <atomic>

/**
 * A circular queue is defined here, with a fixed and compile-time constant length.
 * This is used to store the key sequence for playback.
 */


template<class T, std::size_t N>
class CircularQueue {
    std::array<T, N> nums;
    // std::atomic<int> is used to mark the start and end position of the queue, ensuring thread-safety.
    std::atomic<int> start, end;
    // we need to always keep a slot empty, nothing should be stored at `end` index.
    static constexpr int capacity = N - 1;

public:
    // At the begining, start == end, which means it is empty.
    CircularQueue() : start(0), end(0) {
    }

    bool enQueue(T value) {
        if (isFull()) {
            return false;
        }
        // store the new element at `end`, then move end forward. If it exceeds the length of the base array, it is wrapped to zero.
        int e = end.load(std::memory_order_acquire);
        nums[e] = value;
        end.store((e + 1) % (capacity + 1), std::memory_order_release);
        return true;
    }


    // std::optional<T> is used whether there is a value returned or not.
    // Although we can return -1 as empty here, we dont have a '-1' key.
    std::optional<T> deQueue() {
        if (isEmpty()) {
            return std::nullopt;
        }
        int s = start.load(std::memory_order_acquire);
        T value = nums[s];
        start.store((s + 1) % (capacity + 1), std::memory_order_release);
        return value;
    }

    std::optional<T> front() const {
        if (isEmpty()) {
            return std::nullopt;
        }
        int s = start.load(std::memory_order_acquire);
        return nums[s];
    }

    std::optional<T> back() const {
        if (isEmpty()) {
            return std::nullopt;
        }
        int e = end.load(std::memory_order_acquire);
        return nums[(e + capacity) % (capacity + 1)];
    }

    // if start == end, this means the queue is empty.
    bool isEmpty() const {
        int s = start.load(std::memory_order_relaxed);
        return s == end.load(std::memory_order_relaxed);
    }

    // set end = start
    void clear() {
        int s = start.load(std::memory_order_acquire);
        end.store(s, std::memory_order_release);
    }

    // if end + 1 mod by the size of the base array (capacity + 1) equals to start, this means the queue is full.
    bool isFull() const {
        int e = end.load(std::memory_order_relaxed);
        return (e + 1) % (capacity + 1) == start.load(std::memory_order_relaxed);
    }
};
