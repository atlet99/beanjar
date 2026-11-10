// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "redir.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <usbredirparser.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pacing.h"

namespace virtcam
{
namespace
{

std::uint8_t redir_status(Status s)
{
    return s == Status::Ok      ? usb_redir_success
           : s == Status::Stall ? usb_redir_stall
                                : usb_redir_ioerror;
}

// ep_info's index of endpoint address ep: 0-15 OUT, 16-31 IN
int ep_index(std::uint8_t ep)
{
    return (ep & 0x0f) | ((ep & 0x80) ? 0x10 : 0);
}

} // namespace

struct Front::Callbacks {
    static Front &self(void *priv)
    {
        return *static_cast<Front *>(priv);
    }

    static void log(void *, int level, const char *msg)
    {
        if (level <= usbredirparser_warning)
            std::fprintf(stderr, "beanjar-virtcam: usbredir: %s\n", msg);
    }
    static int read(void *priv, std::uint8_t *data, int count)
    {
        Front &f = self(priv);
        const ssize_t n = ::recv(f.fd_, data, std::size_t(count), 0);
        if (n > 0)
            return int(n);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return 0;
        if (n == 0)
            f.peer_closed_ = true;
        else
            f.io_error_ = true;
        return -1;
    }
    static int write(void *priv, std::uint8_t *data, int count)
    {
        Front &f = self(priv);
        const ssize_t n = ::send(f.fd_, data, std::size_t(count), MSG_NOSIGNAL);
        if (n >= 0)
            return int(n);
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return 0;
        if (errno == EPIPE || errno == ECONNRESET)
            f.peer_closed_ = true;
        else
            f.io_error_ = true;
        return -1;
    }
    static void *alloc_lock()
    {
        return new std::mutex;
    }
    static void lock(void *m)
    {
        static_cast<std::mutex *>(m)->lock();
    }
    static void unlock(void *m)
    {
        static_cast<std::mutex *>(m)->unlock();
    }
    static void free_lock(void *m)
    {
        delete static_cast<std::mutex *>(m);
    }

    static void hello(void *priv, usb_redir_hello_header *h)
    {
        Front &f = self(priv);
        std::fprintf(stderr, "beanjar-virtcam: connected to %.64s\n", h->version);
        f.announce();
    }
    static void reset(void *priv)
    {
        Front &f = self(priv);
        f.stop_stream();
        f.camera_.reset();
    }
    static void set_configuration(void *priv, std::uint64_t id,
                                  usb_redir_set_configuration_header *h)
    {
        Front &f = self(priv);
        f.stop_stream();
        usb_redir_configuration_status_header s{};
        s.status = redir_status(f.camera_.set_configuration(h->configuration));
        s.configuration = f.camera_.configuration();
        // the endpoints of the configuration's alternate settings 0, before its status
        f.send_interface_info();
        f.send_ep_info();
        usbredirparser_send_configuration_status(f.parser_, id, &s);
    }
    static void get_configuration(void *priv, std::uint64_t id)
    {
        Front &f = self(priv);
        usb_redir_configuration_status_header s{usb_redir_success, f.camera_.configuration()};
        usbredirparser_send_configuration_status(f.parser_, id, &s);
    }
    static void set_alt_setting(void *priv, std::uint64_t id, usb_redir_set_alt_setting_header *h)
    {
        Front &f = self(priv);
        for (auto &[intf, ep] : f.camera_.endpoints())
            if (intf == h->interface && ep.address == f.stream_ep_)
                f.stop_stream();
        usb_redir_alt_setting_status_header s{};
        s.status = redir_status(f.camera_.set_alt(h->interface, h->alt));
        s.interface = h->interface;
        s.alt = f.camera_.alt(h->interface);
        f.send_interface_info();
        f.send_ep_info();
        usbredirparser_send_alt_setting_status(f.parser_, id, &s);
    }
    static void get_alt_setting(void *priv, std::uint64_t id, usb_redir_get_alt_setting_header *h)
    {
        Front &f = self(priv);
        const bool known = f.camera_.config().interface(h->interface) != nullptr;
        usb_redir_alt_setting_status_header s{};
        s.status = known ? usb_redir_success : usb_redir_inval;
        s.interface = h->interface;
        s.alt = known ? f.camera_.alt(h->interface) : 0xff;
        usbredirparser_send_alt_setting_status(f.parser_, id, &s);
    }
    static void start_iso_stream(void *priv, std::uint64_t id, usb_redir_start_iso_stream_header *h)
    {
        Front &f = self(priv);
        bool found = false;
        for (auto &[intf, ep] : f.camera_.endpoints())
            found |= ep.address == h->endpoint && (ep.attributes & 3) == 1;
        usb_redir_iso_stream_status_header s{};
        s.status = found ? usb_redir_success : usb_redir_inval;
        s.endpoint = h->endpoint;
        usbredirparser_send_iso_stream_status(f.parser_, id, &s);
        // the video endpoint streams; another (the microphone's) is started and sends nothing
        if (found && f.camera_.config().vs_interface) {
            for (auto &[intf, ep] : f.camera_.endpoints())
                if (ep.address == h->endpoint && intf == *f.camera_.config().vs_interface)
                    f.start_stream(h->endpoint);
        }
    }
    static void stop_iso_stream(void *priv, std::uint64_t id, usb_redir_stop_iso_stream_header *h)
    {
        Front &f = self(priv);
        if (h->endpoint == f.stream_ep_)
            f.stop_stream();
        usb_redir_iso_stream_status_header s{usb_redir_success, h->endpoint};
        usbredirparser_send_iso_stream_status(f.parser_, id, &s);
    }
    static void start_interrupt_receiving(void *priv, std::uint64_t id,
                                          usb_redir_start_interrupt_receiving_header *h)
    {
        Front &f = self(priv);
        usb_redir_interrupt_receiving_status_header s{usb_redir_success, h->endpoint};
        usbredirparser_send_interrupt_receiving_status(f.parser_, id, &s);
    }
    static void stop_interrupt_receiving(void *priv, std::uint64_t id,
                                         usb_redir_stop_interrupt_receiving_header *h)
    {
        Front &f = self(priv);
        usb_redir_interrupt_receiving_status_header s{usb_redir_success, h->endpoint};
        usbredirparser_send_interrupt_receiving_status(f.parser_, id, &s);
    }
    static void alloc_bulk_streams(void *priv, std::uint64_t id,
                                   usb_redir_alloc_bulk_streams_header *h)
    {
        Front &f = self(priv);
        usb_redir_bulk_streams_status_header s{h->endpoints, 0, usb_redir_inval};
        usbredirparser_send_bulk_streams_status(f.parser_, id, &s);
    }
    static void free_bulk_streams(void *priv, std::uint64_t id,
                                  usb_redir_free_bulk_streams_header *h)
    {
        Front &f = self(priv);
        usb_redir_bulk_streams_status_header s{h->endpoints, 0, usb_redir_inval};
        usbredirparser_send_bulk_streams_status(f.parser_, id, &s);
    }
    static void cancel_data_packet(void *, std::uint64_t)
    {
        // control packets are answered as they come: nothing is ever outstanding to cancel
    }
    static void control_packet(void *priv, std::uint64_t id, usb_redir_control_packet_header *h,
                               std::uint8_t *data, int data_len)
    {
        Front &f = self(priv);
        const bool in = h->endpoint & 0x80;
        ControlResult r = f.camera_.control(h->requesttype, h->request, h->value, h->index,
                                            h->length, data, data ? std::size_t(data_len) : 0);
        usb_redir_control_packet_header reply = *h;
        reply.status = redir_status(r.status);
        reply.length = in ? std::uint16_t(r.data.size()) : (r.status == Status::Ok ? h->length : 0);
        usbredirparser_send_control_packet(f.parser_, id, &reply,
                                           in && !r.data.empty() ? r.data.data() : nullptr,
                                           in ? int(r.data.size()) : 0);
        if (data)
            usbredirparser_free_packet_data(f.parser_, data);
    }
    static void bulk_packet(void *priv, std::uint64_t id, usb_redir_bulk_packet_header *h,
                            std::uint8_t *data, int)
    {
        Front &f = self(priv);
        usb_redir_bulk_packet_header reply = *h;
        reply.status = usb_redir_stall;
        reply.length = 0;
        reply.length_high = 0;
        usbredirparser_send_bulk_packet(f.parser_, id, &reply, nullptr, 0);
        if (data)
            usbredirparser_free_packet_data(f.parser_, data);
    }
    static void iso_packet(void *priv, std::uint64_t, usb_redir_iso_packet_header *,
                           std::uint8_t *data, int)
    {
        if (data)
            usbredirparser_free_packet_data(self(priv).parser_, data); // no OUT streams
    }
    static void interrupt_packet(void *priv, std::uint64_t id, usb_redir_interrupt_packet_header *h,
                                 std::uint8_t *data, int)
    {
        Front &f = self(priv);
        usb_redir_interrupt_packet_header reply = *h;
        reply.status = usb_redir_stall;
        reply.length = 0;
        usbredirparser_send_interrupt_packet(f.parser_, id, &reply, nullptr, 0);
        if (data)
            usbredirparser_free_packet_data(f.parser_, data);
    }
};

Front::Front(int fd, Camera &camera) : fd_(fd), camera_(camera)
{
    ::fcntl(fd_, F_SETFL, ::fcntl(fd_, F_GETFL) | O_NONBLOCK);
    wake_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    parser_ = usbredirparser_create();
    auto *p = parser_;
    p->priv = this;
    p->log_func = Callbacks::log;
    p->read_func = Callbacks::read;
    p->write_func = Callbacks::write;
    p->alloc_lock_func = Callbacks::alloc_lock;
    p->lock_func = Callbacks::lock;
    p->unlock_func = Callbacks::unlock;
    p->free_lock_func = Callbacks::free_lock;
    p->hello_func = Callbacks::hello;
    p->reset_func = Callbacks::reset;
    p->set_configuration_func = Callbacks::set_configuration;
    p->get_configuration_func = Callbacks::get_configuration;
    p->set_alt_setting_func = Callbacks::set_alt_setting;
    p->get_alt_setting_func = Callbacks::get_alt_setting;
    p->start_iso_stream_func = Callbacks::start_iso_stream;
    p->stop_iso_stream_func = Callbacks::stop_iso_stream;
    p->start_interrupt_receiving_func = Callbacks::start_interrupt_receiving;
    p->stop_interrupt_receiving_func = Callbacks::stop_interrupt_receiving;
    p->alloc_bulk_streams_func = Callbacks::alloc_bulk_streams;
    p->free_bulk_streams_func = Callbacks::free_bulk_streams;
    p->cancel_data_packet_func = Callbacks::cancel_data_packet;
    p->control_packet_func = Callbacks::control_packet;
    p->bulk_packet_func = Callbacks::bulk_packet;
    p->iso_packet_func = Callbacks::iso_packet;
    p->interrupt_packet_func = Callbacks::interrupt_packet;
    std::uint32_t caps[USB_REDIR_CAPS_SIZE] = {};
    usbredirparser_caps_set_cap(caps, usb_redir_cap_connect_device_version);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_ep_info_max_packet_size);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_64bits_ids);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_32bits_bulk_length);
    usbredirparser_init(parser_, "beanjar-virtcam", caps, USB_REDIR_CAPS_SIZE,
                        usbredirparser_fl_usb_host);
}

Front::~Front()
{
    stop_stream();
    usbredirparser_destroy(parser_);
    ::close(wake_fd_);
}

void Front::announce()
{
    send_interface_info();
    send_ep_info();
    const auto &d = camera_.device_descriptor();
    usb_redir_device_connect_header c{};
    c.speed = usb_redir_speed_high;
    c.device_class = d[4];
    c.device_subclass = d[5];
    c.device_protocol = d[6];
    c.vendor_id = camera_.vendor();
    c.product_id = camera_.product();
    c.device_version_bcd = camera_.bcd_device();
    usbredirparser_send_device_connect(parser_, &c);
}

void Front::send_interface_info()
{
    usb_redir_interface_info_header h{};
    const auto &cfg = camera_.config();
    for (auto &i : cfg.interfaces) {
        if (h.interface_count >= 32)
            break;
        const std::uint8_t alt = camera_.alt(i.number);
        const AltSetting *a = &i.alts.front();
        for (auto &x : i.alts)
            if (x.alt == alt)
                a = &x;
        h.interface[h.interface_count] = i.number;
        h.interface_class[h.interface_count] = a->cls;
        h.interface_subclass[h.interface_count] = a->subclass;
        h.interface_protocol[h.interface_count] = a->protocol;
        h.interface_count++;
    }
    usbredirparser_send_interface_info(parser_, &h);
}

void Front::send_ep_info()
{
    usb_redir_ep_info_header h{};
    std::memset(h.type, usb_redir_type_invalid, sizeof h.type);
    h.type[0] = h.type[16] = usb_redir_type_control;
    h.max_packet_size[0] = h.max_packet_size[16] = camera_.device_descriptor()[7];
    for (auto &[intf, ep] : camera_.endpoints()) {
        const int i = ep_index(ep.address);
        h.type[i] = ep.attributes & 3; // control, iso, bulk, interrupt: as usbredir numbers them
        h.interval[i] = ep.interval;
        h.interface[i] = intf;
        h.max_packet_size[i] = std::uint16_t(ep.payload());
    }
    usbredirparser_send_ep_info(parser_, &h);
}

void Front::start_stream(std::uint8_t ep)
{
    stop_stream();
    std::uint32_t size = 0;
    std::uint8_t interval = 1;
    for (auto &[intf, e] : camera_.endpoints())
        if (e.address == ep)
            size = e.payload(), interval = e.interval;
    if (!size)
        return;
    stream_ep_ = ep;
    pacing_ = true;
    {
        std::lock_guard lock(stats_mutex_);
        stats_.streams++;
    }
    pacer_ = std::thread(&Front::pace, this, ep, size, iso_rate(true, interval));
}

void Front::stop_stream()
{
    pacing_ = false;
    if (pacer_.joinable())
        pacer_.join();
    stream_ep_ = 0;
}

void Front::pace(std::uint8_t ep, std::uint32_t size, std::uint32_t rate)
{
    using clock = std::chrono::steady_clock;
    Pacer pacer(rate, rate * 30 / 1000); // at most 30 ms of packets at once
    std::uint64_t id = 0;
    const auto start = clock::now();
    std::uint64_t skipped0 = 0, late0 = 0;
    for (std::uint64_t tick = 1; pacing_; tick++) {
        std::this_thread::sleep_until(start + std::chrono::milliseconds(tick));
        auto due = pacer.due(clock::now() - start);
        Stats add;
        add.skipped = pacer.skipped() - skipped0;
        add.late = pacer.late() - late0;
        skipped0 = pacer.skipped();
        late0 = pacer.late();
        while (due.send && pacing_) {
            const unsigned n = std::min(due.send, 32u);
            due.send -= n;
            if (usbredirparser_get_bufferered_output_size(parser_) > BACKLOG_LIMIT) {
                add.dropped += n;
                continue;
            }
            IsoPackets p = camera_.iso_in(ep, n, size);
            for (unsigned i = 0; i < n; i++) {
                usb_redir_iso_packet_header h{ep, usb_redir_success, p.lengths[i]};
                usbredirparser_send_iso_packet(
                    parser_, id++, &h, p.lengths[i] ? &p.data[std::size_t(i) * size] : nullptr,
                    p.lengths[i]);
                add.packets++;
                add.bytes += p.lengths[i];
                if (p.lengths[i] > 2)
                    add.data_packets++;
            }
            add.frames += p.frames_ended;
        }
        {
            std::lock_guard lock(stats_mutex_);
            stats_.packets += add.packets;
            stats_.data_packets += add.data_packets;
            stats_.bytes += add.bytes;
            stats_.frames += add.frames;
            stats_.skipped += add.skipped;
            stats_.late += add.late;
            stats_.dropped += add.dropped;
        }
        wake();
    }
}

void Front::wake()
{
    const std::uint64_t one = 1;
    [[maybe_unused]] auto r = ::write(wake_fd_, &one, sizeof one);
}

void Front::stop()
{
    stopping_ = true;
    wake();
}

Stats Front::stats() const
{
    std::lock_guard lock(stats_mutex_);
    return stats_;
}

bool Front::flush(int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (usbredirparser_has_data_to_write(parser_)) {
        if (usbredirparser_do_write(parser_) < 0 || peer_closed_)
            return false;
        if (!usbredirparser_has_data_to_write(parser_))
            break;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - std::chrono::steady_clock::now())
                              .count();
        if (left <= 0)
            return false;
        pollfd p{fd_, POLLOUT, 0};
        ::poll(&p, 1, int(left));
    }
    return true;
}

int Front::run()
{
    while (!stopping_ && !peer_closed_ && !io_error_) {
        pollfd fds[2] = {
            {fd_, short(POLLIN | (usbredirparser_has_data_to_write(parser_) ? POLLOUT : 0)), 0},
            {wake_fd_, POLLIN, 0}};
        if (::poll(fds, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            io_error_ = true;
            break;
        }
        if (fds[1].revents & POLLIN) {
            std::uint64_t v;
            [[maybe_unused]] auto r = ::read(wake_fd_, &v, sizeof v);
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            const int r = usbredirparser_do_read(parser_);
            if (r == usbredirparser_read_parse_error)
                std::fprintf(stderr,
                             "beanjar-virtcam: usbredir: a packet did not parse; skipped\n");
            else if (r == usbredirparser_read_io_error && !peer_closed_)
                io_error_ = true;
        }
        if (usbredirparser_has_data_to_write(parser_) && usbredirparser_do_write(parser_) < 0 &&
            !peer_closed_)
            io_error_ = true;
    }
    stop_stream();
    if (stopping_ && !peer_closed_ && !io_error_) {
        usbredirparser_send_device_disconnect(parser_);
        flush(1000);
    }
    return io_error_ && !peer_closed_ ? 1 : 0;
}

} // namespace virtcam
