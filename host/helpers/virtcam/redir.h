// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The camera's usbredir end: the usb-host role of the usbredir protocol (libusbredirparser with
// usbredirparser_fl_usb_host; usbredir's docs/usb-redirection-protocol.md), talking to a QEMU
// usb-redir device's chardev, with the Camera as the device behind it.
//
// After QEMU's hello it announces the device (interface_info, ep_info, device_connect, as
// libusbredirhost orders them). The requests QEMU does not answer itself come in as their own
// packets (set/get configuration and alternate setting, start/stop of streams) or as control
// packets; each is answered at once from the Camera. ep_info follows every change of alternate
// setting, before its status, with each endpoint's payload in bytes (wMaxPacketSize times its
// transactions per microframe), which is what QEMU's ep_info_max_packet_size capability means.
//
// An isochronous IN stream is paced (pacing.h): a thread wakes every millisecond and sends the
// packets that came due, one iso_packet per microframe, empty ones included, as a camera on a bus
// would. The interrupt endpoint is started and stays silent (no status events). When the output
// queue backs up (a slow link), packets are dropped and counted rather than queued without end.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

#include "camera.h"

struct usbredirparser;

namespace virtcam
{

struct Stats {
    std::uint64_t packets = 0;      // iso packets sent
    std::uint64_t data_packets = 0; // of them with frame data (more than a header)
    std::uint64_t bytes = 0;
    std::uint64_t frames = 0;  // frames ended (EOF packets)
    std::uint64_t skipped = 0; // microframes given up after a late wake-up (pacing.h)
    std::uint64_t late = 0;    // late wake-ups
    std::uint64_t dropped = 0; // packets not queued: the output was backed up
    std::uint64_t streams = 0; // streams started
};

class Front
{
  public:
    // fd: a connected stream socket to QEMU's chardev (made non-blocking here)
    Front(int fd, Camera &camera);
    ~Front();
    Front(const Front &) = delete;
    Front &operator=(const Front &) = delete;

    // Serves until the peer closes the connection (0), stop() (0) or an I/O error (1). On stop
    // the device is disconnected first (device_disconnect) and the queue flushed for up to 1 s.
    int run();
    // from any thread or a signal handler
    void stop();
    Stats stats() const;
    bool streaming() const
    {
        return pacing_.load();
    }

    // queued output at which iso packets are dropped instead (bytes)
    static constexpr std::uint64_t BACKLOG_LIMIT = 4u << 20;

    struct Callbacks;

  private:
    friend struct Callbacks;
    void announce();
    void send_interface_info();
    void send_ep_info();
    void start_stream(std::uint8_t ep);
    void stop_stream();
    void pace(std::uint8_t ep, std::uint32_t size, std::uint32_t rate);
    void wake();
    bool flush(int timeout_ms);

    int fd_;
    int wake_fd_;
    Camera &camera_;
    usbredirparser *parser_;
    std::atomic<bool> stopping_{false}, peer_closed_{false}, io_error_{false};
    std::atomic<bool> pacing_{false};
    std::uint8_t stream_ep_ = 0;
    std::thread pacer_;
    mutable std::mutex stats_mutex_;
    Stats stats_;
};

} // namespace virtcam
