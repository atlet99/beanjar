// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "camera.h"

#include <algorithm>
#include <optional>

#include "descriptors.h" // generated: descriptors/gen.py
#include "usbipdcpp/Device.h"
#include "usbipdcpp/protocol.h"
#include "usbipdcpp/utils/StringPool.h"
#include "usbipdcpp/virtual_device/SimpleVirtualDeviceHandler.h"
#include "usbipdcpp/virtual_device/TransferResponder.h"
#include "usbipdcpp/virtual_device/UvcConstants.h"
#include "usbipdcpp/virtual_device/UvcVirtualInterfaceHandler.h"
#include "usbipdcpp/virtual_device/VirtualInterfaceHandler.h"
#include "usbipdcpp/virtual_device/video_sources/VideoSource.h"

using namespace usbipdcpp;

namespace virtcam
{
namespace
{

const std::uint32_t URB_OK = 0;
const std::uint32_t URB_EPIPE = static_cast<std::uint32_t>(UrbStatusType::StatusEPIPE);
constexpr std::uint8_t REQ_GET_DESCRIPTOR = 6;

data_type cut(const unsigned char *bytes, std::size_t size, std::size_t length)
{
    return data_type(bytes, bytes + std::min(size, length));
}

// The responses of a synchronous call into the model (every handler answers before returning).
struct Responder : TransferResponder {
    std::optional<UsbIpResponse::UsbIpRetSubmit> last;
    void submit_ret_submit(UsbIpResponse::UsbIpRetSubmit &&s) override
    {
        last = std::move(s);
    }
    void enqueue_ret_submit(UsbIpResponse::UsbIpRetSubmit &&s) override
    {
        last = std::move(s);
    }
    void submit_ret_unlink(UsbIpResponse::UsbIpRetUnlink &&) override
    {
    }
    void enqueue_ret_unlink(UsbIpResponse::UsbIpRetUnlink &&) override
    {
    }
    void wakeup_sender() override
    {
    }
    void stop_transfer() override
    {
    }
};

// The device: the C920's own descriptors instead of generated ones.
class Device : public SimpleVirtualDeviceHandler
{
  public:
    using SimpleVirtualDeviceHandler::SimpleVirtualDeviceHandler;

  protected:
    data_type get_device_descriptor(std::uint16_t, std::uint16_t length, std::uint32_t *) override
    {
        return cut(virtcam_device_desc, sizeof virtcam_device_desc, length);
    }
    data_type get_configuration_descriptor(std::uint16_t, std::uint16_t length,
                                           std::uint32_t *) override
    {
        return cut(virtcam_config_desc, sizeof virtcam_config_desc, length);
    }
    data_type get_device_qualifier_descriptor(std::uint8_t, std::uint16_t length,
                                              std::uint32_t *) override
    {
        return cut(virtcam_qualifier_desc, sizeof virtcam_qualifier_desc, length);
    }
    data_type get_bos_descriptor(std::uint16_t, std::uint16_t, std::uint32_t *status) override
    {
        *status = URB_EPIPE; // USB 2.00: none
        return {};
    }
    // the C920 has strings 0 (languages), 1 (serial) and 2 (product); iManufacturer is 0
    data_type get_string_descriptor(std::uint8_t index, std::uint16_t length,
                                    std::uint32_t *status) override
    {
        switch (index) {
        case 0:
            return cut(virtcam_string0_desc, sizeof virtcam_string0_desc, length);
        case 1:
            return cut(virtcam_string1_desc, sizeof virtcam_string1_desc, length);
        case 2:
            return cut(virtcam_string2_desc, sizeof virtcam_string2_desc, length);
        default:
            *status = URB_EPIPE;
            return {};
        }
    }
};

// VideoControl: usbipdcpp's processing unit (its unit 2) at the C920's unit ID; the terminals and
// extension units stall each request, as a camera does for a control it lacks. The power line
// frequency control, which the C920 has and usbipdcpp's table lacks, is answered here: LG's camera
// HAL sets it at every open on webOS 24 (W22H), and a control that GET_INFO calls unsettable fails
// that open (VIDIOC_S_CTRL EACCES, camera2 setProperties error 35, getUserMedia fails).
class Control : public UvcVideoControlHandler
{
  public:
    Control(UsbInterface &i, StringPool &pool, std::uint8_t unit)
        : UvcVideoControlHandler(i, pool), unit_(unit)
    {
    }

    void handle_non_standard_request_type_control_urb(std::uint32_t seqnum, const UsbEndpoint &ep,
                                                      std::uint32_t flags, std::uint32_t length,
                                                      const SetupPacket &setup,
                                                      TransferHandle transfer,
                                                      std::error_code &ec) override
    {
        const std::uint8_t entity = setup.index >> 8;
        if (entity == 0)
            return UvcVideoControlHandler::handle_non_standard_request_type_control_urb(
                seqnum, ep, flags, length, setup, std::move(transfer), ec);
        if (unit_ && entity == unit_ && (setup.value >> 8) == PU_POWER_LINE_FREQUENCY)
            return power_line(seqnum, setup, length, std::move(transfer));
        if (unit_ && entity == unit_) {
            SetupPacket s = setup;
            s.index = std::uint16_t(ENTITY_PROCESSING_UNIT << 8 | (setup.index & 0xff));
            return UvcVideoControlHandler::handle_non_standard_request_type_control_urb(
                seqnum, ep, flags, length, s, std::move(transfer), ec);
        }
        responder->submit_ret_submit(
            UsbIpResponse::UsbIpRetSubmit::create_ret_submit_epipe_without_data(seqnum, 0));
    }

  private:
    // One byte: 0 disabled, 1 50 Hz, 2 60 Hz (the C920's default).
    void power_line(std::uint32_t seqnum, const SetupPacket &setup, std::uint32_t length,
                    TransferHandle transfer)
    {
        auto *trx = GenericTransfer::from_handle(transfer.get());
        std::vector<std::uint8_t> bytes;
        switch (setup.request) {
        case GET_CUR:
            bytes = {power_line_};
            break;
        case GET_MIN:
            bytes = {0};
            break;
        case GET_MAX:
        case GET_DEF:
            bytes = {2};
            break;
        case GET_RES:
            bytes = {1};
            break;
        case GET_LEN:
            bytes = {1, 0};
            break;
        case GET_INFO:
            bytes = {0x03}; // GET and SET
            break;
        case SET_CUR:
            if (!trx->data.empty() && trx->data[0] <= 2)
                power_line_ = trx->data[0];
            responder->submit_ret_submit(
                UsbIpResponse::UsbIpRetSubmit::create_ret_submit_with_status_and_no_data(
                    seqnum, URB_OK, length));
            return;
        default:
            responder->submit_ret_submit(
                UsbIpResponse::UsbIpRetSubmit::create_ret_submit_epipe_without_data(seqnum, 0));
            return;
        }
        const std::size_t n = std::min<std::size_t>(bytes.size(), length);
        trx->data.assign(bytes.begin(), bytes.begin() + n);
        trx->actual_length = n;
        responder->submit_ret_submit(
            UsbIpResponse::UsbIpRetSubmit::create_ret_submit_with_status_and_no_iso(
                seqnum, URB_OK, std::uint32_t(n), std::move(transfer)));
    }

    std::uint8_t unit_;
    std::uint8_t power_line_ = 2;
};

// VideoStreaming: probe and commit against the C920's formats and frames, and every alternate
// setting streaming. The commit itself, the frame clock and the packets stay usbipdcpp's.
class Streaming : public UvcVideoStreamingHandler
{
  public:
    Streaming(UsbInterface &i, StringPool &pool, std::unique_ptr<VideoSource> source,
              const Config &config)
        : UvcVideoStreamingHandler(i, pool, std::move(source)), config_(config)
    {
        reset();
    }

    void reset()
    {
        probe_ = default_probe(config_);
        committed_ = {};
        alt_ = 0;
    }
    Probe committed() const
    {
        return committed_;
    }

    void handle_non_standard_request_type_control_urb(std::uint32_t seqnum, const UsbEndpoint &ep,
                                                      std::uint32_t flags, std::uint32_t length,
                                                      const SetupPacket &setup,
                                                      TransferHandle transfer,
                                                      std::error_code &ec) override
    {
        const std::uint8_t selector = setup.value >> 8;
        const bool klass =
            static_cast<RequestType>(setup.calc_request_type()) == RequestType::Class;
        if (!klass || setup.request == REQ_GET_DESCRIPTOR ||
            (selector != VS_PROBE_CONTROL && selector != VS_COMMIT_CONTROL))
            return UvcVideoStreamingHandler::handle_non_standard_request_type_control_urb(
                seqnum, ep, flags, length, setup, std::move(transfer), ec);
        const bool commit = selector == VS_COMMIT_CONTROL;
        auto *trx = GenericTransfer::from_handle(transfer.get());
        switch (setup.request) {
        case GET_CUR:
            return reply(seqnum, std::move(transfer), (commit ? committed_ : probe_).bytes(),
                         length);
        case GET_MIN:
        case GET_MAX:
            return reply(seqnum, std::move(transfer), probe_.bytes(), length);
        case GET_DEF:
            return reply(seqnum, std::move(transfer), default_probe(config_).bytes(), length);
        case GET_LEN:
            return reply(seqnum, std::move(transfer), {std::uint8_t(Probe::SIZE), 0}, length);
        case GET_INFO:
            return reply(seqnum, std::move(transfer), {0x03}, length); // GET and SET
        case SET_CUR: {
            const Probe p = negotiate(config_, Probe::read(trx->data.data(), trx->data.size()));
            if (!commit) {
                probe_ = p;
                responder->submit_ret_submit(
                    UsbIpResponse::UsbIpRetSubmit::create_ret_submit_with_status_and_no_data(
                        seqnum, URB_OK, length));
                return;
            }
            const Format *fmt = config_.format(p.format);
            const Frame *frame = fmt ? fmt->frame(p.frame) : nullptr;
            if (!frame ||
                !get_source()->set_format(fmt->fourcc, frame->width, frame->height, p.interval)) {
                responder->submit_ret_submit(
                    UsbIpResponse::UsbIpRetSubmit::create_ret_submit_epipe_without_data(seqnum, 0));
                return;
            }
            probe_ = committed_ = p;
            // usbipdcpp's commit, with the values agreed, starts the stream's state over
            auto bytes = p.bytes();
            trx->data = bytes;
            return UvcVideoStreamingHandler::handle_non_standard_request_type_control_urb(
                seqnum, ep, flags, length, setup, std::move(transfer), ec);
        }
        default:
            responder->submit_ret_submit(
                UsbIpResponse::UsbIpRetSubmit::create_ret_submit_epipe_without_data(seqnum, 0));
        }
    }

    // usbipdcpp streams in alternate setting 1 only; here every one with the endpoint does
    void request_set_interface(std::uint16_t alt, std::uint32_t *status) override
    {
        if (alt == 0) {
            UvcVideoStreamingHandler::request_set_interface(0, status);
        } else if (alt < handle_interface.endpoints.size()) {
            UvcVideoStreamingHandler::request_set_interface(1, status);
        } else {
            *status = URB_EPIPE;
            return;
        }
        if (*status == URB_OK)
            alt_ = std::uint8_t(alt);
    }
    std::uint8_t request_get_interface(std::uint32_t *) override
    {
        return alt_;
    }

  private:
    void reply(std::uint32_t seqnum, TransferHandle transfer,
               const std::vector<std::uint8_t> &bytes, std::uint32_t length)
    {
        auto *trx = GenericTransfer::from_handle(transfer.get());
        const std::size_t n = std::min<std::size_t>(bytes.size(), length);
        trx->data.assign(bytes.begin(), bytes.begin() + n);
        trx->actual_length = n;
        responder->submit_ret_submit(
            UsbIpResponse::UsbIpRetSubmit::create_ret_submit_with_status_and_no_iso(
                seqnum, URB_OK, std::uint32_t(n), std::move(transfer)));
    }

    const Config &config_;
    Probe probe_, committed_;
    std::uint8_t alt_ = 0;
};

// The microphone's interfaces: alternate settings taken, nothing streamed, class requests stalled.
class Idle : public VirtualInterfaceHandler
{
  public:
    using VirtualInterfaceHandler::VirtualInterfaceHandler;
};

} // namespace

struct Camera::Model {
    StringPool pool;
    std::shared_ptr<UsbDevice> device;
    std::shared_ptr<Device> handler;
    std::shared_ptr<Streaming> streaming;
    Responder responder;
    GenericTransferOperator op;
    std::uint32_t seqnum = 1;

    UsbIpResponse::UsbIpRetSubmit submit(const UsbEndpoint &ep, std::optional<UsbInterface> intf,
                                         bool in, std::uint32_t length, const SetupPacket &setup,
                                         int iso_packets, const std::uint8_t *out,
                                         std::size_t out_length, std::uint32_t packet_size = 0)
    {
        UsbIpCommand::UsbIpCmdSubmit c{};
        c.header.command = USBIP_CMD_SUBMIT;
        c.header.seqnum = seqnum++;
        c.header.devid = 1;
        c.header.direction = in ? UsbIpDirection::In : UsbIpDirection::Out;
        c.header.ep = ep.address & 0x7f;
        c.transfer_buffer_length = length;
        c.number_of_packets = std::uint32_t(iso_packets);
        c.setup = setup;
        UsbIpHeaderBasic h{};
        h.direction = c.header.direction;
        auto *t =
            GenericTransfer::from_handle(op.alloc_transfer_handle(length, iso_packets, h, setup));
        if (!in && out)
            t->data.assign(out, out + std::min<std::size_t>(out_length, length));
        for (int i = 0; i < iso_packets; i++)
            t->iso_descriptors[i] = {std::uint32_t(i) * packet_size, packet_size, 0, 0};
        c.transfer = TransferHandle(t, &op);
        responder.last.reset();
        std::error_code ec;
        handler->receive_urb(std::move(c), ep, std::move(intf), ec);
        if (!responder.last)
            return UsbIpResponse::UsbIpRetSubmit::create_ret_submit_epipe_without_data(0, 0);
        auto r = std::move(*responder.last);
        responder.last.reset();
        return r;
    }
};

Camera::Camera(std::unique_ptr<VideoSource> source) : model_(std::make_unique<Model>())
{
    auto parsed = parse_config(virtcam_config_desc, sizeof virtcam_config_desc);
    if (!parsed || !parsed->vs_interface)
        throw std::runtime_error("the camera's configuration descriptor does not parse");
    config_ = std::move(*parsed);

    std::vector<UsbInterface> interfaces;
    for (auto &i : config_.interfaces) {
        UsbInterface u{};
        u.interface_class = i.alts.front().cls;
        u.interface_subclass = i.alts.front().subclass;
        u.interface_protocol = i.alts.front().protocol;
        u.interface_number = i.number;
        for (auto &a : i.alts) {
            std::vector<UsbEndpoint> eps;
            for (auto &e : a.endpoints)
                eps.push_back(UsbEndpoint{e.address, e.attributes, e.max_packet_size, e.interval});
            u.endpoints.push_back(std::move(eps));
        }
        interfaces.push_back(std::move(u));
    }
    const std::uint8_t *dd = virtcam_device_desc;
    auto &m = *model_;
    m.device = UsbDevice::make("1-1", vendor(), product(), std::move(interfaces), 1, 1, dd[4], {},
                               UsbSpeed::High, Version(bcd_device()), dd[5], dd[6],
                               config_.configuration_value, dd[17]);
    m.handler = m.device->with_handler<Device>(m.pool);
    std::shared_ptr<Control> control;
    for (auto &u : m.device->interfaces) {
        if (u.interface_number == *config_.vs_interface) {
            m.streaming = u.with_handler<Streaming>(m.pool, std::move(source), config_);
        } else if (u.interface_class == CC_VIDEO && u.interface_subclass == SC_VIDEOCONTROL) {
            control = u.with_handler<Control>(m.pool, config_.processing_unit);
        } else {
            u.with_handler<Idle>(m.pool);
        }
    }
    if (control) {
        control->set_vs_handler(m.streaming.get());
        m.streaming->set_vc_handler(control.get());
    }
    m.handler->setup_interface_handlers();
    std::error_code ec;
    m.handler->on_new_connection(m.responder, ec);
}

Camera::~Camera()
{
    std::error_code ec;
    if (model_ && model_->handler)
        model_->handler->on_disconnection(ec);
}

const std::vector<std::uint8_t> &Camera::device_descriptor() const
{
    static const std::vector<std::uint8_t> d(virtcam_device_desc,
                                             virtcam_device_desc + sizeof virtcam_device_desc);
    return d;
}
std::uint16_t Camera::vendor() const
{
    return std::uint16_t(virtcam_device_desc[8] | virtcam_device_desc[9] << 8);
}
std::uint16_t Camera::product() const
{
    return std::uint16_t(virtcam_device_desc[10] | virtcam_device_desc[11] << 8);
}
std::uint16_t Camera::bcd_device() const
{
    return std::uint16_t(virtcam_device_desc[12] | virtcam_device_desc[13] << 8);
}

ControlResult Camera::control(std::uint8_t requesttype, std::uint8_t request, std::uint16_t value,
                              std::uint16_t index, std::uint16_t length, const std::uint8_t *out,
                              std::size_t out_length)
{
    std::lock_guard lock(mutex_);
    const bool in = requesttype & 0x80;
    SetupPacket setup{.request_type = requesttype,
                      .request = request,
                      .value = value,
                      .index = index,
                      .length = length};
    auto &m = *model_;
    auto r = m.submit(in ? m.device->ep0_in : m.device->ep0_out, std::nullopt, in, length, setup, 0,
                      out, out_length);
    ControlResult result;
    result.status = r.status == URB_OK      ? Status::Ok
                    : r.status == URB_EPIPE ? Status::Stall
                                            : Status::Error;
    if (result.status == Status::Ok && in && r.transfer) {
        auto *t = GenericTransfer::from_handle(r.transfer.get());
        const std::size_t n =
            std::min({std::size_t(r.actual_length), t->data.size(), std::size_t(length)});
        result.data.assign(t->data.begin(), t->data.begin() + n);
    }
    return result;
}

Status Camera::set_configuration(std::uint8_t value)
{
    std::lock_guard lock(mutex_);
    if (value != 0 && value != config_.configuration_value)
        return Status::Stall;
    if (control(0x00, 9, value, 0, 0).status != Status::Ok)
        return Status::Stall;
    if (configuration_ != value) {
        for (auto &u : model_->device->interfaces)
            if (u.current_altsetting)
                control(0x01, 11, 0, u.interface_number, 0);
    }
    configuration_ = value;
    return Status::Ok;
}

std::uint8_t Camera::configuration() const
{
    std::lock_guard lock(mutex_);
    return configuration_;
}

Status Camera::set_alt(std::uint8_t interface, std::uint8_t alt)
{
    std::lock_guard lock(mutex_);
    const Interface *i = config_.interface(interface);
    if (!i || std::none_of(i->alts.begin(), i->alts.end(), [&](auto &a) { return a.alt == alt; }))
        return Status::Stall;
    return control(0x01, 11, alt, interface, 0).status;
}

std::uint8_t Camera::alt(std::uint8_t interface) const
{
    std::lock_guard lock(mutex_);
    for (auto &u : model_->device->interfaces)
        if (u.interface_number == interface)
            return u.current_altsetting;
    return 0;
}

std::vector<std::pair<std::uint8_t, Endpoint>> Camera::endpoints() const
{
    std::lock_guard lock(mutex_);
    std::vector<std::pair<std::uint8_t, Endpoint>> out;
    for (auto &u : model_->device->interfaces)
        for (auto &e : u.current_endpoints())
            out.push_back({u.interface_number,
                           Endpoint{e.address, e.attributes, e.max_packet_size, e.interval}});
    return out;
}

IsoPackets Camera::iso_in(std::uint8_t ep, unsigned n, std::uint32_t size)
{
    std::lock_guard lock(mutex_);
    IsoPackets p;
    p.size = size;
    p.data.assign(std::size_t(n) * size, 0);
    p.lengths.assign(n, 0);
    auto &m = *model_;
    auto found = m.device->find_ep(ep);
    if (!found || !found->second || n == 0 || size < 2)
        return p; // not in the current alternate setting: empty packets
    auto r = m.submit(found->first, found->second, true, n * size, {}, int(n), nullptr, 0, size);
    if (r.status != URB_OK || !r.transfer)
        return p; // no frame yet (or the stream not committed): empty packets
    auto *t = GenericTransfer::from_handle(r.transfer.get());
    for (unsigned i = 0; i < n && i < t->iso_descriptors.size(); i++) {
        const auto &d = t->iso_descriptors[i];
        const std::uint32_t len = std::min(d.actual_length, size);
        if (!len || d.offset + len > t->data.size())
            continue;
        std::uint8_t *dst = &p.data[std::size_t(i) * size];
        std::copy_n(&t->data[d.offset], len, dst);
        fix_payload_header(dst, len);
        if (dst[1] & 0x02)
            p.frames_ended++;
        p.lengths[i] = std::uint16_t(len);
    }
    return p;
}

Probe Camera::committed() const
{
    std::lock_guard lock(mutex_);
    return model_->streaming->committed();
}

void Camera::reset()
{
    std::lock_guard lock(mutex_);
    for (auto &u : model_->device->interfaces)
        if (u.current_altsetting)
            control(0x01, 11, 0, u.interface_number, 0);
    model_->streaming->reset();
    configuration_ = 0;
}

} // namespace virtcam
