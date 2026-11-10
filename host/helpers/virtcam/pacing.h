// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// Real-time pacing of an isochronous IN stream. QEMU's usb-redir takes one buffered packet per
// (micro)frame the guest's xHCI schedules and holds about 60 ms of them (hw/usb/redirect.c,
// usbredir_start_iso_stream: target = pkts_per_sec * 60 ms, dropped beyond twice that), so the
// camera sends its packets at the bus rate, not as fast as it can make them. A timer wakes every
// millisecond and sends the microframes that came due since the stream started; a wake-up that is
// late sends the backlog, up to a bound beyond which the missed microframes are skipped (a stream
// that far behind would overflow QEMU's queue on catching up, and its frames are stale).
#pragma once

#include <chrono>
#include <cstdint>

namespace virtcam
{

class Pacer
{
  public:
    // per_second: 8000 for a high-speed endpoint with bInterval 1 (8000 / 2^(bInterval-1));
    // max_backlog: the most packets one wake-up sends.
    explicit Pacer(std::uint32_t per_second = 8000, std::uint32_t max_backlog = 240)
        : per_second_(per_second), max_backlog_(max_backlog)
    {
    }

    struct Due {
        std::uint32_t send = 0;    // packets to send now
        std::uint64_t skipped = 0; // packets given up this time
    };

    // The packets due elapsed after the stream's start, given those sent so far.
    Due due(std::chrono::nanoseconds elapsed)
    {
        const std::uint64_t total = std::uint64_t(elapsed.count()) * per_second_ / 1000000000ull;
        Due d;
        if (total <= accounted_)
            return d;
        std::uint64_t n = total - accounted_;
        if (n > max_backlog_) {
            d.skipped = n - max_backlog_;
            n = max_backlog_;
        }
        // a wake-up that has more than two milliseconds' worth to send was late
        if (n > 2ull * per_second_ / 1000)
            late_++;
        d.send = std::uint32_t(n);
        accounted_ = total;
        sent_ += d.send;
        skipped_ += d.skipped;
        return d;
    }

    std::uint64_t sent() const
    {
        return sent_;
    }
    std::uint64_t skipped() const
    {
        return skipped_;
    }
    std::uint64_t late() const
    {
        return late_;
    }
    std::uint32_t per_second() const
    {
        return per_second_;
    }

  private:
    std::uint32_t per_second_, max_backlog_;
    std::uint64_t accounted_ = 0, sent_ = 0, skipped_ = 0, late_ = 0;
};

// Packets per second of an isochronous endpoint: high speed counts 125 us microframes,
// full speed 1 ms frames; bInterval is 2^(bInterval-1) of them (USB 2.0 9.6.6).
constexpr std::uint32_t iso_rate(bool high_speed, std::uint8_t interval)
{
    const std::uint32_t base = high_speed ? 8000 : 1000;
    const std::uint32_t shift = interval ? (interval > 16 ? 15 : interval - 1) : 0;
    return base >> shift ? base >> shift : 1;
}

} // namespace virtcam
