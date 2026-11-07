#include "BroadcastRing.h"

#include <algorithm>
#include <cstring>

namespace ab {

void BroadcastRing::allocate(std::size_t capacityFramesPow2, int channels)
{
    std::size_t cap = 1;
    while (cap < capacityFramesPow2)
        cap <<= 1;
    channels_ = channels;
    capacity_ = cap;
    mask_ = cap - 1;
    data_.reset(new float[cap * static_cast<std::size_t>(channels)]());
    // Keep the write index monotonic across re-allocation so stale reader cursors are
    // detected as invalid rather than silently reading garbage.
}

void BroadcastRing::write(const float* frames, std::size_t count)
{
    if (!data_ || count == 0)
        return;
    std::uint64_t w = writeIndex_.load(std::memory_order_relaxed);
    while (count > 0) {
        const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(count, kMaxWriteChunk));
        std::size_t done = 0;
        while (done < chunk) {
            const std::uint64_t pos = (w + done) & mask_;
            const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(chunk - done, capacity_ - pos));
            std::memcpy(data_.get() + pos * channels_, frames + done * channels_, n * channels_ * sizeof(float));
            done += n;
        }
        w += chunk;
        frames += chunk * channels_;
        count -= chunk;
        writeIndex_.store(w, std::memory_order_release);
    }
}

void BroadcastRing::writeSilence(std::size_t count)
{
    if (!data_ || count == 0)
        return;
    std::uint64_t w = writeIndex_.load(std::memory_order_relaxed);
    while (count > 0) {
        const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(count, kMaxWriteChunk));
        std::size_t done = 0;
        while (done < chunk) {
            const std::uint64_t pos = (w + done) & mask_;
            const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(chunk - done, capacity_ - pos));
            std::memset(data_.get() + pos * channels_, 0, n * channels_ * sizeof(float));
            done += n;
        }
        w += chunk;
        count -= chunk;
        writeIndex_.store(w, std::memory_order_release);
    }
}

bool BroadcastRing::read(std::uint64_t from, float* dst, std::size_t count) const
{
    if (!data_)
        return false;
    const std::uint64_t w0 = writeIndex_.load(std::memory_order_acquire);
    if (from + count > w0)
        return false; // not written yet
    if (w0 - from > maxReadableBacklog())
        return false; // already (about to be) overwritten
    std::size_t done = 0;
    while (done < count) {
        const std::uint64_t pos = (from + done) & mask_;
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(count - done, capacity_ - pos));
        std::memcpy(dst + done * channels_, data_.get() + pos * channels_, n * channels_ * sizeof(float));
        done += n;
    }
    // Seqlock-style validation: if the writer advanced so far that it may have
    // overwritten the region while we copied, discard the copy.
    std::atomic_thread_fence(std::memory_order_acquire);
    const std::uint64_t w1 = writeIndex_.load(std::memory_order_acquire);
    return w1 - from <= maxReadableBacklog();
}

} // namespace ab
