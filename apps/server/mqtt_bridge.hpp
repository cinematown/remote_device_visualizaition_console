#pragma once

#include <string>
#include <string_view>

#if RDVC_HAS_MQTT
#include <mosquitto.h>
#endif

#include "rdvc/protocol/device_status.hpp"

namespace rdvc {

class MqttBridge {
public:
    MqttBridge();
    ~MqttBridge();

    MqttBridge(const MqttBridge&) = delete;
    MqttBridge& operator=(const MqttBridge&) = delete;

    bool start(std::string_view host, int port);
    void publish_status(const DeviceStatus& status);
    bool enabled() const;

private:
#if RDVC_HAS_MQTT
    static void handle_connect(mosquitto* client, void* user_data, int result);
    static void handle_message(
        mosquitto* client,
        void* user_data,
        const mosquitto_message* message);

    mosquitto* client_ = nullptr;
#endif
    bool enabled_ = false;
};

} // namespace rdvc
