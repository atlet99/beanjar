// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The camera's picture: GStreamer's videotestsrc (live, a test pattern with the running time on
// it), in the format and size the guest committed, made by a gst-launch-1.0 child that writes the
// frames to a pipe. A child rather than an appsink in this process: the program then links no
// GStreamer (Debian 13's GStreamer development packages are not multi-arch, so the PC could not
// cross-build an arm64 program against them beside its own), and GStreamer is a runtime
// dependency like any tool.
//
// A reader thread keeps the latest whole frame (YUY2: width x height x 2 bytes; MJPEG: one JPEG,
// SOI to EOI). usbipdcpp's streaming handler asks for a frame at most once per frame interval,
// from the pacing thread: it gets the latest at once, the last one again when no new one came.
#pragma once

#include <sys/types.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "usbipdcpp/virtual_device/video_sources/VideoSource.h"

namespace virtcam
{

class GstSource : public usbipdcpp::VideoSource
{
  public:
    // pattern: a videotestsrc pattern name (smpte, ball, snow, ...); the source starts in
    // fourcc/width/height at interval (100 ns units). launcher: the gst-launch-1.0 to run.
    GstSource(std::string pattern, std::uint32_t fourcc, std::uint16_t width, std::uint16_t height,
              std::uint32_t interval, std::string launcher = "gst-launch-1.0");
    ~GstSource() override;

    std::vector<usbipdcpp::VideoFormatInfo> supported_formats() const override;
    usbipdcpp::VideoFormatInfo current_format() const override;
    // YUY2 or MJPG; another FourCC is refused (false) and the format stays
    bool set_format(std::uint32_t fourcc, std::uint16_t width, std::uint16_t height,
                    std::uint32_t interval) override;
    bool get_frame(usbipdcpp::VideoFrame &frame) override;
    std::size_t max_frame_size() const override;
    std::uint32_t frame_interval() const override;

    std::uint64_t samples() const
    {
        return samples_;
    } // new frames served
    std::uint64_t repeats() const
    {
        return repeats_;
    } // frames served again for want of a new one
    // gst-launch-1.0's pipeline for a format, its frames to file descriptor fd
    static std::vector<std::string> pipeline(const std::string &pattern, std::uint32_t fourcc,
                                             std::uint16_t width, std::uint16_t height,
                                             std::uint32_t interval, int fd);
    // the end of the first whole JPEG (SOI ... EOI) in data[0, size), and where it begins; 0 if
    // none
    static std::size_t jpeg_end(const std::uint8_t *data, std::size_t size, std::size_t &begin);

  private:
    void start();
    void stop();
    void read_frames(int fd, std::uint32_t fourcc, std::size_t frame_size);

    std::string pattern_, launcher_;
    std::uint32_t fourcc_;
    std::uint16_t width_, height_;
    std::uint32_t interval_;
    pid_t child_ = -1;
    int pipe_ = -1;
    std::thread reader_;
    std::mutex mutex_;
    std::vector<std::uint8_t> latest_, current_;
    std::uint64_t latest_seq_ = 0, served_seq_ = 0;
    std::atomic<std::uint64_t> samples_{0}, repeats_{0};
};

} // namespace virtcam
