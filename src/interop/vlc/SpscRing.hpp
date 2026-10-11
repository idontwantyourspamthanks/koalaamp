// SPDX-License-Identifier: MIT
// interop/vlc — lock-free SPSC ring buffer for decoded interleaved float PCM.
//
// The libvlc audio-output thread is the sole producer (VlcPcmTap's play
// callback); the miniaudio device thread is the sole consumer
// (MiniAudioSink's data callback). Neither side takes a lock, allocates, or
// calls into the GUI (docs/spec.md §2.1). Indices are monotonic and masked on
// access, so the head-tail difference survives unsigned wrap.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace koalaamp::interop::vlc {

class FloatRing {
public:
    /// Capacity is rounded up to a power of two so masking is cheap.
    explicit FloatRing(std::size_t capacityFloats) {
        std::size_t cap = 1;
        while (cap < capacityFloats) {
            cap <<= 1;
        }
        m_buf.resize(cap);
        m_mask = cap - 1;
    }

    FloatRing(const FloatRing&) = delete;
    FloatRing& operator=(const FloatRing&) = delete;

    std::size_t capacity() const noexcept { return m_buf.size(); }

    /// Producer side. Appends up to `n` floats; returns the count written
    /// (the remainder did not fit — the caller counts that as an overrun).
    std::size_t write(const float* src, std::size_t n) noexcept {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        const std::size_t tail = m_tail.load(std::memory_order_acquire);
        const std::size_t freeSpace = m_buf.size() - (head - tail);
        const std::size_t k = n < freeSpace ? n : freeSpace;
        for (std::size_t i = 0; i < k; ++i) {
            m_buf[(head + i) & m_mask] = src[i];
        }
        m_head.store(head + k, std::memory_order_release);
        return k;
    }

    /// Consumer side. Pops up to `n` floats; returns the count read.
    std::size_t read(float* dst, std::size_t n) noexcept {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        const std::size_t head = m_head.load(std::memory_order_acquire);
        const std::size_t avail = head - tail;
        const std::size_t k = n < avail ? n : avail;
        for (std::size_t i = 0; i < k; ++i) {
            dst[i] = m_buf[(tail + i) & m_mask];
        }
        m_tail.store(tail + k, std::memory_order_release);
        return k;
    }

    /// Drop everything currently buffered (libvlc flush callback).
    void discardAll() noexcept {
        m_tail.store(m_head.load(std::memory_order_acquire), std::memory_order_release);
    }

    /// Buffered float count (frames * channels).
    std::size_t fill() const noexcept {
        return m_head.load(std::memory_order_acquire) - m_tail.load(std::memory_order_acquire);
    }

    // --- enqueue→render residency (FE-2 latency measurement) ----------------
    // The producer stamps the FIFO end after each write; the consumer pops
    // stamps once the corresponding floats have been consumed, yielding the
    // real age of a frame at the moment it is handed to the device. This is the
    // "callback-timestamp delta" latency the ticket asks for.

    /// Producer side: stamp the current write position with a steady-clock ns.
    void stampAfterWrite(std::int64_t nowNs) noexcept {
        const std::size_t i = m_stampHead.load(std::memory_order_relaxed);
        m_stamps[i & (kStampCapacity - 1)] = Stamp{m_head.load(std::memory_order_relaxed), nowNs};
        m_stampHead.store(i + 1, std::memory_order_release);
    }

    /// Consumer side: write the residency (ns) of every fully-consumed stamp
    /// into `outNs` (up to `cap`); returns how many were written. Call at the
    /// start of a device callback, before reading.
    std::size_t collectResidency(std::int64_t nowNs, std::int64_t* outNs,
                                 std::size_t cap) noexcept {
        const std::uint64_t consumed = m_tail.load(std::memory_order_acquire);
        const std::size_t head = m_stampHead.load(std::memory_order_acquire);
        std::size_t tail = m_stampTail.load(std::memory_order_relaxed);
        std::size_t written = 0;
        while (tail != head && written < cap) {
            const Stamp& s = m_stamps[tail & (kStampCapacity - 1)];
            if (s.end > consumed) {
                break;
            }
            if (nowNs >= s.ns) {
                outNs[written++] = nowNs - s.ns;
            }
            ++tail;
        }
        m_stampTail.store(tail, std::memory_order_release);
        return written;
    }

private:
    struct Stamp {
        std::uint64_t end; // float index just past the stamped write
        std::int64_t ns;   // steady-clock timestamp of that write
    };
    static constexpr std::size_t kStampCapacity = 1u << 13;

    std::vector<float> m_buf;
    std::size_t m_mask{0};
    std::atomic<std::size_t> m_head{0}; // producer index, monotonic
    std::atomic<std::size_t> m_tail{0}; // consumer index, monotonic
    std::array<Stamp, kStampCapacity> m_stamps{};
    std::atomic<std::size_t> m_stampHead{0};
    std::atomic<std::size_t> m_stampTail{0};
};

} // namespace koalaamp::interop::vlc
