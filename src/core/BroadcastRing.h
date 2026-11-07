// Single-producer / multi-consumer ring buffer of interleaved float frames.
//
// The capture thread of an input device is the only writer. Every output that has a
// route from this input owns an independent read cursor, so one microphone can feed any
// number of speakers that run on unrelated hardware clocks. The writer never waits for
// readers: a reader that falls more than the capacity behind detects the overrun and
// resynchronises. No locks, no allocation after allocate().
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

namespace ab {

class BroadcastRing {
public:
    // Maximum number of frames written in one write() call; readers keep this much
    // headroom so the writer can never be overwriting data a reader is copying.
    static constexpr std::uint64_t kMaxWriteChunk = 16384;

    BroadcastRing() = default;
    BroadcastRing(const BroadcastRing&) = delete;
    BroadcastRing& operator=(const BroadcastRing&) = delete;

    // Control thread only, while no reader/writer is active on this ring.
    void allocate(std::size_t capacityFramesPow2, int channels);
    bool isAllocated() const { return data_ != nullptr; }
    std::uint64_t capacity() const { return capacity_; }
    int channels() const { return channels_; }

    // Writer (single thread). Frames are published atomically after being copied.
    void write(const float* frames, std::size_t count);
    void writeSilence(std::size_t count);

    // Total number of frames ever written (monotonic). Acquire semantics.
    std::uint64_t writeIndex() const { return writeIndex_.load(std::memory_order_acquire); }

    // Largest backlog a reader may safely hold.
    std::uint64_t maxReadableBacklog() const { return capacity_ > kMaxWriteChunk ? capacity_ - kMaxWriteChunk : 0; }

    // Copies `count` frames starting at absolute index `from`. Returns false if the
    // data is not (or no longer) valid: not yet written or overwritten by the writer.
    bool read(std::uint64_t from, float* dst, std::size_t count) const;

private:
    std::unique_ptr<float[]> data_;
    std::uint64_t capacity_ = 0; // frames, power of two
    std::uint64_t mask_ = 0;
    int channels_ = 2;
    std::atomic<std::uint64_t> writeIndex_{0};
};

} // namespace ab
