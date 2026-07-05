#include "mqtt_bridge.hpp"

#include <iostream>

namespace rdvc {
namespace {

#if RDVC_HAS_MQTT
std::string json_escape(std::string_view value)
{
    std::string output;
    output.reserve(value.size());

    for (const char ch : value) {
        if (ch == '"' || ch == '\\') {
            output += '\\';
        }
        output += ch;
    }

    return output;
}

std::string status_payload(const DeviceStatus& status)
{
    return "{\"device_id\":\"" + json_escape(status.device_id) + "\",\"state\":\""
        + json_escape(status.state) + "\",\"battery\":" + std::to_string(status.battery_percent)
        + ",\"x\":" + std::to_string(status.x) + ",\"y\":" + std::to_string(status.y)
        + ",\"z\":" + std::to_string(status.z) + "}";
}

std::string status_topic(const DeviceStatus& status)
{
    return "devices/" + status.device_id + "/status";
}
#endif

} // namespace

MqttBridge::MqttBridge() = default;

MqttBridge::~MqttBridge()
{
#if RDVC_HAS_MQTT
    if (client_ != nullptr) {
        mosquitto_loop_stop(client_, true);
        mosquitto_disconnect(client_);
        mosquitto_destroy(client_);
        mosquitto_lib_cleanup();
    }
#endif
}

bool MqttBridge::start(std::string_view host, int port)
{
#if RDVC_HAS_MQTT
    mosquitto_lib_init();
    client_ = mosquitto_new("rdvc-server", true, this);
    if (client_ == nullptr) {
        std::cerr << "MQTT bridge disabled: failed to create client\n";
        return false;
    }

    mosquitto_connect_callback_set(client_, &MqttBridge::handle_connect);
    mosquitto_message_callback_set(client_, &MqttBridge::handle_message);

    const auto host_string = std::string(host);
    const int connect_result = mosquitto_connect(client_, host_string.c_str(), port, 60);
    if (connect_result != MOSQ_ERR_SUCCESS) {
        std::cerr << "MQTT bridge disabled: " << mosquitto_strerror(connect_result)
                  << '\n';
        return false;
    }

    const int loop_result = mosquitto_loop_start(client_);
    if (loop_result != MOSQ_ERR_SUCCESS) {
        std::cerr << "MQTT bridge disabled: " << mosquitto_strerror(loop_result)
                  << '\n';
        return false;
    }

    enabled_ = true;
    std::cout << "MQTT bridge connected to " << host << ':' << port << '\n';
    return true;
#else
    (void)host;
    (void)port;
    std::cout << "MQTT bridge disabled: built without libmosquitto\n";
    return false;
#endif
}

void MqttBridge::publish_status(const DeviceStatus& status)
{
    if (!enabled_) {
        return;
    }

#if RDVC_HAS_MQTT
    const auto topic = status_topic(status);
    const auto payload = status_payload(status);

    // Publish the same latest-state snapshot used by REST/viewer to a stable
    // IoT topic shape. QoS 0 keeps Phase 10 simple and non-blocking.
    const int result = mosquitto_publish(
        client_,
        nullptr,
        topic.c_str(),
        static_cast<int>(payload.size()),
        payload.data(),
        0,
        false);

    if (result != MOSQ_ERR_SUCCESS) {
        std::cerr << "MQTT publish error: " << mosquitto_strerror(result) << '\n';
    }
#else
    (void)status;
#endif
}

bool MqttBridge::enabled() const
{
    return enabled_;
}

#if RDVC_HAS_MQTT
void MqttBridge::handle_connect(mosquitto* client, void*, int result)
{
    if (result != 0) {
        std::cerr << "MQTT connect callback error: " << mosquitto_strerror(result)
                  << '\n';
        return;
    }

    // This command topic is a bridge placeholder: later phases can translate
    // MQTT reset commands into the same command path used by REST.
    mosquitto_subscribe(client, nullptr, "devices/+/commands/reset", 0);
}

void MqttBridge::handle_message(mosquitto*, void*, const mosquitto_message* message)
{
    if (message == nullptr || message->topic == nullptr) {
        return;
    }

    std::cout << "MQTT command received on " << message->topic << '\n';
}
#endif

} // namespace rdvc
