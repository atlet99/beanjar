// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The camera as a USB device: usbipdcpp's device model and UVC handlers, behind calls shaped like
// the usbredir packets that reach it (host/helpers/virtcam/redir.cpp). Its identity is a Logitech
// C920 (046d:082d) with that camera's own descriptors, byte for byte (descriptors/gen.py), since
// LG's camera service accepts only the USB VID:PID pairs on its list.
//
// usbipdcpp (LGPL-3.0, its shared libraries unmodified) answers the standard requests from the
// descriptors and streams the frames. What its UVC handlers assume of a camera and the C920 does
// not have is overridden here, in subclasses:
//   - the descriptors: the device, configuration and string descriptors are the C920's bytes, not
//     the ones usbipdcpp would generate (one format, one frame, its own units);
//   - probe/commit: usbipdcpp keeps the guest's format and frame index without acting on them;
//     here they are checked against the descriptors (uvcdesc.h, negotiate) and the source switched
//     to the committed format and size;
//   - alternate settings: usbipdcpp's streaming handler takes only 1; the C920 has 1 to 11 by
//     bandwidth, each of which streams here (the packet size is the alternate setting's);
//   - units: the C920's processing unit is unit 3 (usbipdcpp's is 2); its terminals and
//     extension units stall, as unsupported controls do;
//   - the payload header's end-of-frame bit (uvcdesc.h, fix_payload_header).
// Its microphone interfaces are there (the descriptors list them) and stream nothing.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "uvcdesc.h"

namespace usbipdcpp
{
class VideoSource;
}

namespace virtcam
{

enum class Status { Ok, Stall, Error };

struct ControlResult {
    Status status = Status::Stall;
    std::vector<std::uint8_t> data; // IN: at most the request's length
};

struct IsoPackets {
    Status status = Status::Ok;
    std::vector<std::uint8_t> data;     // n slots of size bytes
    std::vector<std::uint16_t> lengths; // each packet's length (0: an empty packet)
    std::uint32_t size = 0;
    unsigned frames_ended = 0; // packets that end a frame (EOF)
};

class Camera
{
  public:
    explicit Camera(std::unique_ptr<usbipdcpp::VideoSource> source);
    ~Camera();

    const Config &config() const
    {
        return config_;
    }
    const std::vector<std::uint8_t> &device_descriptor() const;
    std::uint16_t vendor() const;
    std::uint16_t product() const;
    std::uint16_t bcd_device() const;

    // the standard requests usbredir carries in packets of their own
    Status set_configuration(std::uint8_t value);
    std::uint8_t configuration() const;
    Status set_alt(std::uint8_t interface, std::uint8_t alt);
    std::uint8_t alt(std::uint8_t interface) const;
    // every other control request, as on endpoint 0
    ControlResult control(std::uint8_t requesttype, std::uint8_t request, std::uint16_t value,
                          std::uint16_t index, std::uint16_t length,
                          const std::uint8_t *out = nullptr, std::size_t out_length = 0);
    // n packets of the isochronous IN endpoint ep, each at most size bytes
    IsoPackets iso_in(std::uint8_t ep, unsigned n, std::uint32_t size);
    // the endpoints of each interface's current alternate setting
    std::vector<std::pair<std::uint8_t, Endpoint>> endpoints() const;

    // the committed probe (format 0 before the first commit)
    Probe committed() const;
    void reset();

    struct Model;

  private:
    mutable std::recursive_mutex mutex_;
    Config config_;
    std::unique_ptr<Model> model_;
    std::uint8_t configuration_ = 0;
};

} // namespace virtcam
