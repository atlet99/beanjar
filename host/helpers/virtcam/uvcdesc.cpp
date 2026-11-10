// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "uvcdesc.h"

#include "pacing.h"

#include <algorithm>
#include <cstdlib>

namespace virtcam
{
namespace
{

constexpr std::uint8_t DT_CONFIG = 2, DT_INTERFACE = 4, DT_ENDPOINT = 5, CS_INTERFACE = 0x24;
constexpr std::uint8_t CC_VIDEO = 14, SC_VIDEOCONTROL = 1, SC_VIDEOSTREAMING = 2;
constexpr std::uint8_t VC_PROCESSING_UNIT = 5;
// VS descriptor subtypes (UVC 1.1 Table A-6)
constexpr std::uint8_t VS_FORMAT_UNCOMPRESSED = 4, VS_FRAME_UNCOMPRESSED = 5, VS_FORMAT_MJPEG = 6,
                       VS_FRAME_MJPEG = 7, VS_FORMAT_FRAME_BASED = 16, VS_FRAME_FRAME_BASED = 17;

std::uint16_t u16(const std::uint8_t *p)
{
    return std::uint16_t(p[0] | p[1] << 8);
}
std::uint32_t u32(const std::uint8_t *p)
{
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 |
           std::uint32_t(p[3]) << 24;
}
void put16(std::uint8_t *p, std::uint16_t v)
{
    p[0] = std::uint8_t(v);
    p[1] = std::uint8_t(v >> 8);
}
void put32(std::uint8_t *p, std::uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = std::uint8_t(v >> (8 * i));
}

// A frame descriptor of any of the three kinds; false if it is too short for what it says.
bool read_frame(const std::uint8_t *d, std::uint8_t len, Frame &f)
{
    const bool frame_based = d[2] == VS_FRAME_FRAME_BASED;
    if (len < 26)
        return false;
    f.index = d[3];
    f.width = u16(d + 5);
    f.height = u16(d + 7);
    // uncompressed/MJPEG: dwMaxVideoFrameBufferSize at 17, interval at 21, type at 25;
    // frame-based: no buffer size, interval at 17, type at 21, dwBytesPerLine at 22
    const std::size_t at_interval = frame_based ? 17 : 21, at_type = frame_based ? 21 : 25;
    f.max_frame_size = frame_based ? std::uint32_t(f.width) * f.height * 2 : u32(d + 17);
    f.default_interval = u32(d + at_interval);
    const std::uint8_t type = d[at_type];
    f.continuous = type == 0;
    const std::size_t count = type == 0 ? 3 : type;
    if (26 + 4 * count > len)
        return false;
    for (std::size_t i = 0; i < count; i++)
        f.intervals.push_back(u32(d + 26 + 4 * i));
    return true;
}

} // namespace

const Frame *Format::frame(std::uint8_t index) const
{
    for (auto &f : frames)
        if (f.index == index)
            return &f;
    return nullptr;
}

const Interface *Config::interface(std::uint8_t number) const
{
    for (auto &i : interfaces)
        if (i.number == number)
            return &i;
    return nullptr;
}

const Format *Config::format(std::uint8_t index) const
{
    for (auto &f : formats)
        if (f.index == index)
            return &f;
    return nullptr;
}

const Endpoint *Config::alt_endpoint(std::uint8_t alt) const
{
    const Interface *vs = vs_interface ? interface(*vs_interface) : nullptr;
    if (!vs)
        return nullptr;
    for (auto &a : vs->alts)
        if (a.alt == alt)
            for (auto &e : a.endpoints)
                if ((e.attributes & 3) == 1 && (e.address & 0x80))
                    return &e;
    return nullptr;
}

std::uint32_t Config::alt_payload(std::uint8_t alt) const
{
    const Endpoint *e = alt_endpoint(alt);
    return e ? e->payload() : 0;
}

std::uint8_t Config::alt_for(std::uint32_t bytes) const
{
    const Interface *vs = vs_interface ? interface(*vs_interface) : nullptr;
    std::uint8_t best = 0, largest = 0;
    std::uint32_t best_size = 0, largest_size = 0;
    for (auto &a : vs ? vs->alts : std::vector<AltSetting>{}) {
        const Endpoint *e = alt_endpoint(a.alt);
        if (!e || e->interval > 1)
            continue;
        const std::uint32_t p = e->payload();
        if (p > largest_size)
            largest = a.alt, largest_size = p;
        if (p >= bytes && (!best_size || p < best_size))
            best = a.alt, best_size = p;
    }
    return best_size ? best : largest;
}

std::uint8_t Config::sparse_alt_for(std::uint64_t bytes_per_second) const
{
    const Interface *vs = vs_interface ? interface(*vs_interface) : nullptr;
    std::uint8_t best = 0;
    std::uint64_t best_bw = 0;
    for (auto &a : vs ? vs->alts : std::vector<AltSetting>{}) {
        const Endpoint *e = alt_endpoint(a.alt);
        if (!e || e->interval <= 1 || e->payload() <= 2)
            continue;
        const std::uint64_t bw = std::uint64_t(e->payload() - 2) * iso_rate(true, e->interval);
        if (bw >= bytes_per_second && (!best || bw < best_bw))
            best = a.alt, best_bw = bw;
    }
    return best;
}

std::optional<Config> parse_config(const std::uint8_t *data, std::size_t size)
{
    if (size < 9 || data[1] != DT_CONFIG || u16(data + 2) != size)
        return std::nullopt;
    Config c;
    c.configuration_value = data[5];
    Interface *intf = nullptr;
    AltSetting *alt = nullptr;
    Format *fmt = nullptr;
    for (std::size_t off = data[0]; off < size;) {
        const std::uint8_t *d = data + off;
        const std::uint8_t len = d[0];
        if (len < 2 || off + len > size)
            return std::nullopt;
        off += len;
        if (d[1] == DT_INTERFACE) {
            if (len < 9)
                return std::nullopt;
            fmt = nullptr;
            intf = nullptr;
            for (auto &i : c.interfaces)
                if (i.number == d[2])
                    intf = &i;
            if (!intf) {
                c.interfaces.push_back({d[2], {}});
                intf = &c.interfaces.back();
            }
            intf->alts.push_back({d[3], d[5], d[6], d[7], {}});
            alt = &intf->alts.back();
            if (d[5] == CC_VIDEO && d[6] == SC_VIDEOSTREAMING && !c.vs_interface)
                c.vs_interface = d[2];
        } else if (d[1] == DT_ENDPOINT && alt) {
            if (len < 7)
                return std::nullopt;
            alt->endpoints.push_back({d[2], d[3], u16(d + 4), d[6]});
        } else if (d[1] == CS_INTERFACE && alt && alt->cls == CC_VIDEO &&
                   alt->subclass == SC_VIDEOCONTROL && len >= 4 && d[2] == VC_PROCESSING_UNIT) {
            c.processing_unit = d[3];
        } else if (d[1] == CS_INTERFACE && intf && c.vs_interface == intf->number && len >= 3) {
            switch (d[2]) {
            case VS_FORMAT_UNCOMPRESSED:
            case VS_FORMAT_FRAME_BASED:
                // guidFormat at 5 (its first four bytes the FourCC), bDefaultFrameIndex at 22
                if (len < 23)
                    return std::nullopt;
                c.formats.push_back(
                    {d[3],
                     d[2] == VS_FORMAT_UNCOMPRESSED ? Kind::Uncompressed : Kind::FrameBased,
                     u32(d + 5),
                     d[22],
                     {}});
                fmt = &c.formats.back();
                break;
            case VS_FORMAT_MJPEG: // bDefaultFrameIndex at 6
                if (len < 11)
                    return std::nullopt;
                c.formats.push_back({d[3], Kind::Mjpeg, FOURCC_MJPG, d[6], {}});
                fmt = &c.formats.back();
                break;
            case VS_FRAME_UNCOMPRESSED:
            case VS_FRAME_MJPEG:
            case VS_FRAME_FRAME_BASED: {
                Frame f;
                if (!fmt || !read_frame(d, len, f))
                    return std::nullopt;
                fmt->frames.push_back(std::move(f));
                break;
            }
            default:
                break;
            }
        }
    }
    for (auto &i : c.interfaces)
        std::sort(i.alts.begin(), i.alts.end(), [](auto &a, auto &b) { return a.alt < b.alt; });
    return c;
}

Probe Probe::read(const std::uint8_t *d, std::size_t size)
{
    std::uint8_t b[SIZE] = {};
    std::copy_n(d, std::min(size, SIZE), b);
    Probe p;
    p.hint = u16(b);
    p.format = b[2];
    p.frame = b[3];
    p.interval = u32(b + 4);
    p.key_frame_rate = u16(b + 8);
    p.p_frame_rate = u16(b + 10);
    p.comp_quality = u16(b + 12);
    p.comp_window = u16(b + 14);
    p.delay = u16(b + 16);
    p.max_video_frame_size = u32(b + 18);
    p.max_payload = u32(b + 22);
    return p;
}

std::vector<std::uint8_t> Probe::bytes() const
{
    std::vector<std::uint8_t> b(SIZE);
    put16(&b[0], hint);
    b[2] = format;
    b[3] = frame;
    put32(&b[4], interval);
    put16(&b[8], key_frame_rate);
    put16(&b[10], p_frame_rate);
    put16(&b[12], comp_quality);
    put16(&b[14], comp_window);
    put16(&b[16], delay);
    put32(&b[18], max_video_frame_size);
    put32(&b[22], max_payload);
    return b;
}

namespace
{

std::uint32_t nearest_interval(const Frame &f, std::uint32_t want)
{
    if (!want || f.intervals.empty())
        return f.default_interval;
    if (f.continuous) {
        const std::uint32_t lo = f.intervals[0], hi = f.intervals[1],
                            step = f.intervals[2] ? f.intervals[2] : 1;
        const std::uint32_t v = std::clamp(want, lo, hi);
        return lo + (v - lo + step / 2) / step * step;
    }
    std::uint32_t best = f.intervals[0];
    for (auto i : f.intervals)
        if (std::llabs(std::int64_t(i) - want) < std::llabs(std::int64_t(best) - want))
            best = i;
    return best;
}

} // namespace

Probe negotiate(const Config &config, const Probe &request)
{
    Probe p = request;
    const Format *fmt = config.format(request.format);
    if (!fmt || fmt->frames.empty())
        fmt = config.formats.empty() ? nullptr : &config.formats.front();
    if (!fmt || fmt->frames.empty())
        return p;
    const Frame *frame = fmt->frame(request.frame);
    if (!frame)
        frame = fmt->frame(fmt->default_frame);
    if (!frame)
        frame = &fmt->frames.front();
    p.format = fmt->index;
    p.frame = frame->index;
    p.interval = nearest_interval(*frame, request.interval);
    p.max_video_frame_size = frame->max_frame_size;
    p.key_frame_rate = p.p_frame_rate = p.comp_window = p.delay = 0;
    std::uint8_t alt = 0;
    if (fmt->kind == Kind::Uncompressed && p.interval) {
        const std::uint64_t bytes_per_s =
            std::uint64_t(frame->max_frame_size) * 10000000 / p.interval;
        alt = config.alt_for(std::uint32_t((bytes_per_s + 7999) / 8000) + 2);
    } else if (fmt->kind != Kind::Uncompressed && p.interval) {
        const std::uint64_t frame_bytes =
            std::uint64_t(frame->width) * frame->height * COMPRESSED_BITS_PER_PIXEL_X4 / 32;
        alt = config.sparse_alt_for(frame_bytes * 10000000 / p.interval);
    }
    if (!alt)
        alt = config.alt_for(0xffffffff); // the largest that sends every microframe
    p.max_payload = config.alt_payload(alt);
    return p;
}

Probe default_probe(const Config &config)
{
    Probe r;
    if (!config.formats.empty()) {
        r.format = config.formats.front().index;
        r.frame = config.formats.front().default_frame;
    }
    return negotiate(config, r);
}

void fix_payload_header(std::uint8_t *data, std::size_t size)
{
    if (size >= 2 && data[0] >= 2 && (data[1] & 0x80))
        data[1] = std::uint8_t((data[1] & 0x7f) | 0x02);
}

} // namespace virtcam
