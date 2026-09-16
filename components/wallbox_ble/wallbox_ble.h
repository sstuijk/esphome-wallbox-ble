#pragma once

#include <deque>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace wallbox_ble {

// ============================================================================
// Wallbox Pulsar Plus Zentri BLE UUIDs
// ============================================================================

static const char *const WALLBOX_SERVICE_UUID =
    "175f8f23-a570-49bd-9627-815a6a27de2a";

static const char *const WALLBOX_RX_UUID =
    "1cce1ea8-bd34-4813-a00a-c76e028fadcb";

static const char *const WALLBOX_TX_UUID =
    "cacc07ff-ffff-4c48-8fae-a9ef71b75e26";

static const char *const WALLBOX_MODE_UUID =
    "20b9794f-da1a-4d14-8014-a0fb9cefb2f7";


// ============================================================================
// Main Wallbox BLE component
// ============================================================================

class WallboxBLE : public Component,
    public ble_client::BLEClientNode {
public:
    void setup() override;
    void loop() override;
    void dump_config() override;

    float get_setup_priority() const override
    {
        return setup_priority::DATA;
    }

    void gattc_event_handler(
        esp_gattc_cb_event_t event,
        esp_gatt_if_t gattc_if,
        esp_ble_gattc_cb_param_t *param) override;

    void set_poll_interval(uint32_t interval)
    {
        this->poll_interval_ = interval;
    }

    // ==========================================================================
    // Sensors
    // ==========================================================================

    void set_status_sensor(sensor::Sensor *sensor)
    {
        this->status_sensor_ = sensor;
    }

    void set_current_sensor(sensor::Sensor *sensor)
    {
        this->current_sensor_ = sensor;
    }

    void set_max_current_sensor(sensor::Sensor *sensor)
    {
        this->max_current_sensor_ = sensor;
    }

    void set_session_energy_sensor(sensor::Sensor *sensor)
    {
        this->session_energy_sensor_ = sensor;
    }

    void set_charging_power_sensor(sensor::Sensor *sensor)
    {
        this->charging_power_sensor_ = sensor;
    }

    void set_connected_binary_sensor(
        binary_sensor::BinarySensor *sensor)
    {
        this->connected_binary_sensor_ = sensor;
    }

    void set_charging_binary_sensor(
        binary_sensor::BinarySensor *sensor)
    {
        this->charging_binary_sensor_ = sensor;
    }

    void set_status_text_sensor(
        text_sensor::TextSensor *sensor)
    {
        this->status_text_sensor_ = sensor;
    }

    void set_firmware_version_text_sensor(
        text_sensor::TextSensor *sensor)
    {
        this->firmware_version_text_sensor_ = sensor;
    }

    // ==========================================================================
    // Controls
    // ==========================================================================

    void set_max_current_number(number::Number *number)
    {
        this->max_current_number_ = number;
    }

    void set_charging_switch(switch_::Switch *sw)
    {
        this->charging_switch_ = sw;
    }

    void set_lock_switch(switch_::Switch *sw)
    {
        this->lock_switch_ = sw;
    }

    void set_max_current(float value);
    void set_charging(bool enabled);
    void set_lock(bool enabled);

protected:
    // ==========================================================================
    // Connection state
    // ==========================================================================

    enum class State {
        DISCONNECTED,
        DISCOVERING,
        SUBSCRIBING,
        SETTING_STREAM_MODE,
        INITIAL_STATUS,
        AUTHENTICATING,
        READY,
    };

    struct Request {
        uint32_t id;
        std::string method;
        std::string parameter_json;
    };

    State state_{State::DISCONNECTED};

    // ==========================================================================
    // BLE handles
    // ==========================================================================

    uint16_t rx_handle_{0};
    uint16_t tx_handle_{0};
    uint16_t mode_handle_{0};

    bool connected_{false};
    bool notifications_enabled_{false};

    // ==========================================================================
    // Polling
    // ==========================================================================

    uint32_t poll_interval_{10000};
    uint32_t last_poll_{0};

    uint32_t diagnostic_poll_interval_{60000};
    uint32_t last_diagnostic_poll_{0};

    // ==========================================================================
    // Request handling
    // ==========================================================================

    uint32_t request_counter_{1};

    bool request_in_flight_{false};

    uint32_t pending_request_id_{0};

    std::string pending_method_;

    uint32_t request_started_{0};

    std::deque<Request> request_queue_;

    // ==========================================================================
    // TX transport
    // ==========================================================================

    std::deque<std::vector<uint8_t>> write_queue_;

    bool write_in_progress_{false};

    bool stream_mode_write_pending_{false};

    // ==========================================================================
    // RX transport
    // ==========================================================================

    std::vector<uint8_t> rx_buffer_;

    // ==========================================================================
    // Authentication
    // ==========================================================================

    bool authenticated_{false};

    bool authentication_pending_{false};

    int user_id_{-1};

    // ==========================================================================
    // ESPHome entities
    // ==========================================================================

    sensor::Sensor *status_sensor_{nullptr};

    sensor::Sensor *current_sensor_{nullptr};

    sensor::Sensor *max_current_sensor_{nullptr};

    sensor::Sensor *session_energy_sensor_{nullptr};

    sensor::Sensor *charging_power_sensor_{nullptr};

    binary_sensor::BinarySensor *connected_binary_sensor_{nullptr};

    binary_sensor::BinarySensor *charging_binary_sensor_{nullptr};

    text_sensor::TextSensor *status_text_sensor_{nullptr};

    text_sensor::TextSensor *firmware_version_text_sensor_{nullptr};

    number::Number *max_current_number_{nullptr};

    switch_::Switch *charging_switch_{nullptr};
    switch_::Switch *lock_switch_{nullptr};

    // ==========================================================================
    // BLE setup
    // ==========================================================================

    void find_characteristics_();

    void enable_notifications_();

    void set_stream_mode_();

    // ==========================================================================
    // Wallbox protocol
    // ==========================================================================

    bool request_(
        const std::string &method,
        const std::string &parameter_json = "null");

    void process_next_request_();

    void send_status_request_();

    void send_realtime_status_request_();

    void send_live_energy_feed_request_();

    void send_firmware_version_request_();

    void request_authentication_();

    std::vector<uint8_t> build_frame_(
        const std::string &json);

    void enqueue_frame_(
        const std::vector<uint8_t> &frame);

    void write_next_chunk_();

    // ==========================================================================
    // RX processing
    // ==========================================================================

    void handle_notification_(
        const uint8_t *data,
        size_t length);

    void process_rx_buffer_();

    bool extract_json_document_(
        std::string &json);

    void process_json_(
        const std::string &json);

    void process_status_(
        const std::string &json);

    void process_real_time_status_(
        const std::string &json);

    void process_live_energy_feed_(
        const std::string &json);

    void process_firmware_version_(
        const std::string &json);

    // ==========================================================================
    // Lightweight JSON helpers
    //
    // These deliberately avoid ArduinoJson so the component can compile with
    // ESPHome 2026.8.x / ESP-IDF.
    // ==========================================================================

    bool json_get_int_(
        const std::string &json,
        const char *key,
        int &value);

    bool json_get_float_(
        const std::string &json,
        const char *key,
        float &value);

    bool json_get_bool_(
        const std::string &json,
        const char *key,
        bool &value);

    bool json_get_object_(
        const std::string &json,
        const char *key,
        std::string &object);

    // ==========================================================================
    // Helpers
    // ==========================================================================

    const char *status_to_string_(
        int status);

    void set_connected_(
        bool connected);

    void reset_connection_();

    bool is_request_complete_(
        const std::vector<uint8_t> &buffer);
};


// ============================================================================
// Number entity: Maximum charging current
// ============================================================================

class WallboxMaxCurrentNumber : public number::Number {
public:
    void set_parent(WallboxBLE *parent)
    {
        this->parent_ = parent;
    }

protected:
    void control(float value) override
    {
        if (this->parent_ != nullptr) {
            this->parent_->set_max_current(value);
            this->publish_state(value);
        }
    }

    WallboxBLE *parent_{nullptr};
};


// ============================================================================
// Switch entity: Start/stop charging
// ============================================================================

class WallboxChargingSwitch : public switch_::Switch {
public:
    void set_parent(WallboxBLE *parent)
    {
        this->parent_ = parent;
    }

protected:
    void write_state(bool state) override
    {
        if (this->parent_ != nullptr) {
            this->parent_->set_charging(state);
        }

        // The state is intentionally not published here.
        //
        // It will be updated when the Wallbox reports its actual status.
    }

    WallboxBLE *parent_{nullptr};
};

// ============================================================================
// Switch entity: lock/unlock Wallbox
// ============================================================================

class WallboxLockSwitch : public switch_::Switch {
public:
    void set_parent(WallboxBLE *parent)
    {
        this->parent_ = parent;
    }

protected:
    void write_state(bool state) override
    {
        if (this->parent_ != nullptr) {
            this->parent_->set_lock(state);
        }

        // Do not publish here.
        //
        // The actual state should be updated from the Wallbox status response.
    }

    WallboxBLE *parent_{nullptr};
};

}  // namespace wallbox_ble
}  // namespace esphome
