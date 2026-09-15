#include "wallbox_ble.h"

#include <algorithm>
#include <cstring>

#include "cJSON.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace wallbox_ble {

static const char *const TAG = "wallbox_ble";


// ============================================================================
// Setup
// ============================================================================

void WallboxBLE::setup()
{
    ESP_LOGCONFIG(TAG, "Setting up Wallbox Pulsar Plus BLE");
    ESP_LOGCONFIG(TAG, "Profile: Zentri");
}


void WallboxBLE::dump_config()
{
    ESP_LOGCONFIG(TAG, "Wallbox BLE:");
    ESP_LOGCONFIG(TAG, "  Profile: Zentri");
    ESP_LOGCONFIG(TAG, "  Service UUID: %s", WALLBOX_SERVICE_UUID);
    ESP_LOGCONFIG(TAG, "  Poll interval: %u ms", this->poll_interval_);
}


// ============================================================================
// Main loop
// ============================================================================

void WallboxBLE::loop()
{
    uint32_t now = millis();

    // Request timeout.
    if (this->request_in_flight_ &&
            now - this->request_started_ > 5000) {

        ESP_LOGW(
            TAG,
            "Request timed out: %s",
            this->pending_method_.c_str()
        );

        this->request_in_flight_ = false;
        this->pending_request_id_ = 0;
        this->pending_method_.clear();

        this->process_next_request_();
    }

    // Normal polling.
    if (this->state_ == State::READY &&
            !this->request_in_flight_ &&
            now - this->last_poll_ >= this->poll_interval_) {

        this->last_poll_ = now;

        this->send_status_request_();

        // r_sta is queued after r_dat and will therefore be sent
        // immediately after r_dat has completed.
        this->send_realtime_status_request_();
        
        // Live session energy feed 
        this->send_live_energy_feed_request_();
    }
    
    // Diagnostic polling.
    if (this->state_ == State::READY &&
            !this->request_in_flight_ &&
            now - this->last_diagnostic_poll_ >= this->diagnostic_poll_interval_) {

        this->last_diagnostic_poll_ = now;
    
        this->send_firmware_version_request_();
    }
}


// ============================================================================
// BLE events
// ============================================================================

void WallboxBLE::gattc_event_handler(
    esp_gattc_cb_event_t event,
    esp_gatt_if_t gattc_if,
    esp_ble_gattc_cb_param_t *param)
{

    switch (event) {

    // ------------------------------------------------------------------------
    // Connected
    // ------------------------------------------------------------------------

    case ESP_GATTC_OPEN_EVT: {

        if (param->open.status == ESP_GATT_OK) {

            ESP_LOGI(TAG, "Connected to Wallbox");

            this->connected_ = true;
            this->set_connected_(true);

            this->state_ = State::DISCOVERING;

        } else {

            ESP_LOGW(
                TAG,
                "Failed to connect: %d",
                param->open.status
            );

            this->reset_connection_();
        }

        break;
    }


    // ------------------------------------------------------------------------
    // Service discovery completed
    // ------------------------------------------------------------------------

    case ESP_GATTC_SEARCH_CMPL_EVT: {

        ESP_LOGI(TAG, "Service discovery complete");

        this->find_characteristics_();

        break;
    }


    // ------------------------------------------------------------------------
    // Notification registration
    // ------------------------------------------------------------------------

    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {

        if (param->reg_for_notify.status == ESP_GATT_OK) {

            ESP_LOGI(TAG, "Notification registration successful");

            this->notifications_enabled_ = true;

            this->set_stream_mode_();

        } else {

            ESP_LOGE(
                TAG,
                "Failed to register notifications: %d",
                param->reg_for_notify.status
            );
        }

        break;
    }


    // ------------------------------------------------------------------------
    // Notification received
    // ------------------------------------------------------------------------

    case ESP_GATTC_NOTIFY_EVT: {

        if (param->notify.handle == this->tx_handle_) {

            this->handle_notification_(
                param->notify.value,
                param->notify.value_len
            );
        }

        break;
    }


    // ------------------------------------------------------------------------
    // Characteristic write completed
    // ------------------------------------------------------------------------

    case ESP_GATTC_WRITE_CHAR_EVT: {

        // The special Zentri STREAM mode write completed.
        if (this->stream_mode_write_pending_) {

            this->stream_mode_write_pending_ = false;

            if (param->write.status != ESP_GATT_OK) {

                ESP_LOGE(
                    TAG,
                    "Failed to enable Zentri stream mode: %d",
                    param->write.status
                );

                break;
            }

            ESP_LOGI(TAG, "Zentri stream mode enabled");

            this->state_ = State::INITIAL_STATUS;

            // Start protocol communication.
            this->send_status_request_();

            break;
        }


        // Normal protocol write.
        if (this->write_in_progress_) {

            if (param->write.status != ESP_GATT_OK) {

                ESP_LOGW(
                    TAG,
                    "BLE write failed: %d",
                    param->write.status
                );

                this->write_queue_.clear();
                this->write_in_progress_ = false;

                break;
            }

            this->write_in_progress_ = false;

            this->write_next_chunk_();
        }

        break;
    }


    // ------------------------------------------------------------------------
    // Disconnected
    // ------------------------------------------------------------------------

    case ESP_GATTC_CLOSE_EVT: {

        ESP_LOGW(TAG, "Disconnected from Wallbox");

        this->reset_connection_();

        break;
    }


    default:
        break;
    }
}


// ============================================================================
// BLE characteristic discovery
// ============================================================================

void WallboxBLE::find_characteristics_()
{

    auto service_uuid =
        esp32_ble_tracker::ESPBTUUID::from_raw(
            WALLBOX_SERVICE_UUID
        );

    auto rx_uuid =
        esp32_ble_tracker::ESPBTUUID::from_raw(
            WALLBOX_RX_UUID
        );

    auto tx_uuid =
        esp32_ble_tracker::ESPBTUUID::from_raw(
            WALLBOX_TX_UUID
        );

    auto mode_uuid =
        esp32_ble_tracker::ESPBTUUID::from_raw(
            WALLBOX_MODE_UUID
        );


    auto *rx =
        this->parent()->get_characteristic(
            service_uuid,
            rx_uuid
        );

    auto *tx =
        this->parent()->get_characteristic(
            service_uuid,
            tx_uuid
        );

    auto *mode =
        this->parent()->get_characteristic(
            service_uuid,
            mode_uuid
        );


    if (rx == nullptr) {

        ESP_LOGE(TAG, "Wallbox RX characteristic not found");

        return;
    }


    if (tx == nullptr) {

        ESP_LOGE(TAG, "Wallbox TX characteristic not found");

        return;
    }


    if (mode == nullptr) {

        ESP_LOGE(TAG, "Wallbox Zentri MODE characteristic not found");

        return;
    }


    this->rx_handle_ = rx->handle;
    this->tx_handle_ = tx->handle;
    this->mode_handle_ = mode->handle;


    ESP_LOGI(TAG, "Wallbox Zentri characteristics found");

    ESP_LOGD(TAG, "RX handle: %u", this->rx_handle_);
    ESP_LOGD(TAG, "TX handle: %u", this->tx_handle_);
    ESP_LOGD(TAG, "MODE handle: %u", this->mode_handle_);


    this->enable_notifications_();
}


// ============================================================================
// Enable notifications
// ============================================================================

void WallboxBLE::enable_notifications_()
{

    this->state_ = State::SUBSCRIBING;

    ESP_LOGI(TAG, "Registering for Wallbox notifications");


    esp_err_t err =
        esp_ble_gattc_register_for_notify(
            this->parent()->get_gattc_if(),
            this->parent()->get_remote_bda(),
            this->tx_handle_
        );


    if (err != ESP_OK) {

        ESP_LOGE(
            TAG,
            "Failed to register notifications: %s",
            esp_err_to_name(err)
        );

        return;
    }
}


// ============================================================================
// Zentri stream mode
// ============================================================================

void WallboxBLE::set_stream_mode_()
{

    this->state_ = State::SETTING_STREAM_MODE;

    uint8_t stream_mode = 0x01;

    ESP_LOGI(TAG, "Enabling Zentri STREAM mode");


    this->stream_mode_write_pending_ = true;


    esp_err_t err =
        esp_ble_gattc_write_char(
            this->parent()->get_gattc_if(),
            this->parent()->get_conn_id(),
            this->mode_handle_,
            1,
            &stream_mode,
            ESP_GATT_WRITE_TYPE_RSP,
            ESP_GATT_AUTH_REQ_NONE
        );


    if (err != ESP_OK) {

        this->stream_mode_write_pending_ = false;

        ESP_LOGE(
            TAG,
            "Failed to set stream mode: %s",
            esp_err_to_name(err)
        );
    }
}


// ============================================================================
// Request queue
// ============================================================================

bool WallboxBLE::request_(
    const std::string &method,
    const std::string &parameter_json)
{

    if (!this->connected_) {

        ESP_LOGW(TAG, "Cannot send request: disconnected");

        return false;
    }


    if (!this->notifications_enabled_) {

        ESP_LOGW(TAG, "Cannot send request: notifications unavailable");

        return false;
    }


    Request request;

    request.id = this->request_counter_++;
    request.method = method;
    request.parameter_json = parameter_json;


    this->request_queue_.push_back(request);

    this->process_next_request_();

    return true;
}


void WallboxBLE::process_next_request_()
{

    if (this->request_in_flight_) {
        return;
    }


    if (this->write_in_progress_) {
        return;
    }


    if (this->request_queue_.empty()) {
        return;
    }


    Request request =
        this->request_queue_.front();

    this->request_queue_.pop_front();


    std::string json =
        "{\"met\":\"" +
        request.method +
        "\",\"par\":" +
        request.parameter_json +
        ",\"id\":" +
        std::to_string(request.id) +
        "}";


    ESP_LOGD(
        TAG,
        "TX JSON: %s",
        json.c_str()
    );


    this->pending_request_id_ = request.id;
    this->pending_method_ = request.method;

    this->request_in_flight_ = true;
    this->request_started_ = millis();


    auto frame =
        this->build_frame_(json);

    this->enqueue_frame_(frame);
}


// ============================================================================
// Status request
// ============================================================================

void WallboxBLE::send_status_request_()
{

    if (this->request_in_flight_) {
        return;
    }


    ESP_LOGD(TAG, "Requesting Wallbox status");

    this->request_("r_dat");
}

// ============================================================================
// Real-time status request
// ============================================================================

void WallboxBLE::send_realtime_status_request_()
{

    if (!this->connected_) {
        return;
    }

    ESP_LOGD(TAG, "Requesting Wallbox real-time status");

    this->request_(
        "r_sta",
        "null"
    );
}

// ============================================================================
// Live session energy feed request
// ============================================================================

void WallboxBLE::send_live_energy_feed_request_()
{

    if (!this->connected_) {
        return;
    }

    ESP_LOGD(TAG, "Requesting Wallbox live session energy feed");

    this->request_(
        "r_lse",
        "null"
    );
}

// ============================================================================
// Firmware version request
// ============================================================================

void WallboxBLE::send_firmware_version_request_()
{

    if (!this->connected_) {
        return;
    }

    ESP_LOGD(TAG, "Requesting Wallbox firmware version");

    this->request_(
        "fw_v_",
        "null"
    );
}

// ============================================================================
// Authentication
// ============================================================================

void WallboxBLE::request_authentication_()
{

    if (this->authenticated_) {
        return;
    }


    if (this->authentication_pending_) {
        return;
    }


    if (this->user_id_ < 0) {

        ESP_LOGW(
            TAG,
            "Cannot authenticate: user ID unavailable"
        );

        return;
    }


    ESP_LOGI(
        TAG,
        "Authenticating Wallbox user ID %d",
        this->user_id_
    );


    this->authentication_pending_ = true;

    this->state_ = State::AUTHENTICATING;


    this->request_(
        "suser",
        std::to_string(this->user_id_)
    );
}


// ============================================================================
// Protocol frame construction
// ============================================================================

std::vector<uint8_t> WallboxBLE::build_frame_(
    const std::string &json)
{

    std::vector<uint8_t> frame;


    // Protocol:
    //
    // 'E' 'a' 'E' LENGTH JSON CHECKSUM

    frame.reserve(json.size() + 5);


    frame.push_back('E');
    frame.push_back('a');
    frame.push_back('E');


    frame.push_back(
        static_cast<uint8_t>(json.size())
    );


    frame.insert(
        frame.end(),
        json.begin(),
        json.end()
    );


    uint32_t checksum = 0;

    for (uint8_t byte : frame) {
        checksum += byte;
    }


    frame.push_back(
        static_cast<uint8_t>(checksum & 0xFF)
    );


    return frame;
}


// ============================================================================
// BLE TX queue
// ============================================================================

void WallboxBLE::enqueue_frame_(
    const std::vector<uint8_t> &frame)
{

    constexpr size_t CHUNK_SIZE = 20;


    for (size_t offset = 0;
            offset < frame.size();
            offset += CHUNK_SIZE) {

        size_t length =
            std::min(
                CHUNK_SIZE,
                frame.size() - offset
            );


        std::vector<uint8_t> chunk(
            frame.begin() + offset,
            frame.begin() + offset + length
        );


        this->write_queue_.push_back(
            std::move(chunk)
        );
    }


    if (!this->write_in_progress_) {
        this->write_next_chunk_();
    }
}


void WallboxBLE::write_next_chunk_()
{

    if (this->write_queue_.empty()) {

        this->write_in_progress_ = false;

        return;
    }


    std::vector<uint8_t> chunk =
        this->write_queue_.front();

    this->write_queue_.pop_front();


    this->write_in_progress_ = true;


    ESP_LOGVV(
        TAG,
        "Writing %u bytes",
        static_cast<unsigned>(chunk.size())
    );


    esp_err_t err =
        esp_ble_gattc_write_char(
            this->parent()->get_gattc_if(),
            this->parent()->get_conn_id(),
            this->rx_handle_,
            static_cast<uint16_t>(chunk.size()),
            chunk.data(),
            ESP_GATT_WRITE_TYPE_RSP,
            ESP_GATT_AUTH_REQ_NONE
        );


    if (err != ESP_OK) {

        ESP_LOGE(
            TAG,
            "BLE write failed: %s",
            esp_err_to_name(err)
        );


        this->write_in_progress_ = false;

        this->write_queue_.clear();

        this->request_in_flight_ = false;
    }
}


// ============================================================================
// RX notifications
// ============================================================================

void WallboxBLE::handle_notification_(
    const uint8_t *data,
    size_t length)
{

    ESP_LOGVV(
        TAG,
        "Received %u bytes",
        static_cast<unsigned>(length)
    );


    this->rx_buffer_.insert(
        this->rx_buffer_.end(),
        data,
        data + length
    );


    this->process_rx_buffer_();
}


// ============================================================================
// RX processing
// ============================================================================

void WallboxBLE::process_rx_buffer_()
{

    while (!this->rx_buffer_.empty()) {

        std::string json;

        if (!this->extract_json_document_(json)) {
            return;
        }


        if (json.empty()) {
            return;
        }


        ESP_LOGD(
            TAG,
            "RX JSON: %s",
            json.c_str()
        );


        this->process_json_(json);
    }
}


// ============================================================================
// Extract one complete JSON object from the RX buffer
// ============================================================================

bool WallboxBLE::extract_json_document_(
    std::string &json)
{

    json.clear();


    auto start =
        std::find(
            this->rx_buffer_.begin(),
            this->rx_buffer_.end(),
            static_cast<uint8_t>('{')
        );


    if (start == this->rx_buffer_.end()) {

        // Prevent unlimited growth if the protocol contains unexpected data.
        if (this->rx_buffer_.size() > 4096) {

            ESP_LOGW(
                TAG,
                "RX buffer contains no JSON, clearing"
            );

            this->rx_buffer_.clear();
        }

        return false;
    }


    if (start != this->rx_buffer_.begin()) {

        this->rx_buffer_.erase(
            this->rx_buffer_.begin(),
            start
        );
    }


    int depth = 0;
    bool in_string = false;
    bool escaped = false;


    for (size_t i = 0;
            i < this->rx_buffer_.size();
            i++) {

        char c =
            static_cast<char>(
                this->rx_buffer_[i]
            );


        if (in_string) {

            if (escaped) {

                escaped = false;

            } else if (c == '\\') {

                escaped = true;

            } else if (c == '"') {

                in_string = false;
            }

            continue;
        }


        if (c == '"') {

            in_string = true;

            continue;
        }


        if (c == '{') {

            depth++;

        } else if (c == '}') {

            depth--;


            if (depth == 0) {

                json.assign(
                    this->rx_buffer_.begin(),
                    this->rx_buffer_.begin() + i + 1
                );


                this->rx_buffer_.erase(
                    this->rx_buffer_.begin(),
                    this->rx_buffer_.begin() + i + 1
                );


                return true;
            }
        }
    }


    // JSON object is not complete yet.
    return false;
}


// ============================================================================
// Process protocol JSON
// ============================================================================

void WallboxBLE::process_json_(
    const std::string &json)
{
    std::string cur_request;

    cJSON *root =
        cJSON_Parse(json.c_str());


    if (root == nullptr) {

        ESP_LOGW(TAG, "Invalid JSON received");

        return;
    }


    cJSON *id_item =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "id"
        );


    uint32_t response_id = 0;


    if (cJSON_IsNumber(id_item)) {

        response_id =
            static_cast<uint32_t>(
                id_item->valuedouble
            );
    }


    if (response_id != 0 &&
            response_id != this->pending_request_id_) {

        ESP_LOGD(
            TAG,
            "Ignoring response ID %u (expected %u)",
            response_id,
            this->pending_request_id_
        );

        cJSON_Delete(root);

        return;
    }


    // Mark the current request as completed.
    if (this->request_in_flight_) {

        this->request_in_flight_ = false;

        ESP_LOGD(
            TAG,
            "Response received for %s",
            this->pending_method_.c_str()
        );


        // Authentication request completed.
        if (this->pending_method_ == "suser") {

            this->authentication_pending_ = false;
            this->authenticated_ = true;

            ESP_LOGI(TAG, "Wallbox authenticated");

            this->state_ = State::READY;
        }


        this->pending_request_id_ = 0;
        cur_request = this->pending_method_;
        this->pending_method_.clear();
    }

    if (cur_request == "fw_v_") {
          ESP_LOGW(TAG, "--START PROCESS: %s", cur_request.c_str());
          this->process_firmware_version_(json);
          ESP_LOGW(TAG, "--END PROCESS: %s", cur_request.c_str());
    }


    cJSON *result =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "r"
        );


    if (cJSON_IsObject(result)) {
        char *result_string =
            cJSON_PrintUnformatted(result);

        if (result_string != nullptr) {

            if (cur_request == "r_dat") {
                ESP_LOGW(TAG, "--START PROCESS: %s", cur_request.c_str());
                this->process_status_(std::string(result_string));
                ESP_LOGW(TAG, "--END PROCESS: %s", cur_request.c_str());
            } else if (cur_request == "r_sta")  {
                ESP_LOGW(TAG, "--START PROCESS: %s", cur_request.c_str());
                this->process_real_time_status_(std::string(result_string));
                ESP_LOGW(TAG, "--END PROCESS: %s", cur_request.c_str());
            } else if (cur_request == "r_lse")  {
                ESP_LOGW(TAG, "--START PROCESS: %s", cur_request.c_str());
                this->process_live_energy_feed_(std::string(result_string));
                ESP_LOGW(TAG, "--END PROCESS: %s", cur_request.c_str());
            }
            
            cJSON_free(result_string);
        }
    }


    cJSON_Delete(root);


    this->process_next_request_();
}


// ============================================================================
// Process charger status
// ============================================================================

void WallboxBLE::process_status_(
    const std::string &json)
{

    cJSON *root =
        cJSON_Parse(json.c_str());


    if (root == nullptr) {

        ESP_LOGW(TAG, "Could not parse Wallbox status");

        return;
    }


    // --------------------------------------------------------------------------
    // User ID / authentication bootstrap
    // --------------------------------------------------------------------------

    cJSON *user_id =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "usid"
        );


    if (!this->authenticated_ &&
            cJSON_IsNumber(user_id)) {

        this->user_id_ =
            user_id->valueint;


        ESP_LOGI(
            TAG,
            "Wallbox user ID discovered: %d",
            this->user_id_
        );


        this->request_authentication_();
    }

    // --------------------------------------------------------------------------
    // Current
    // --------------------------------------------------------------------------

    const char *current_keys[] = {
        "cur",
        "amp",
        "current"
    };


    for (const char *key : current_keys) {

        cJSON *item =
            cJSON_GetObjectItemCaseSensitive(
                root,
                key
            );


        if (cJSON_IsNumber(item)) {

            float value =
                static_cast<float>(
                    item->valuedouble
                );


            ESP_LOGD(
                TAG,
                "Charging current: %.2f A",
                value
            );


            if (this->current_sensor_ != nullptr) {

                this->current_sensor_->publish_state(
                    value
                );
            }

            if (this->max_current_number_ != nullptr) {

                this->max_current_number_->publish_state(
                    value
                );
            }

            break;
        }
    }

    cJSON_Delete(root);
}

// ============================================================================
// Process charger real-time status
// ============================================================================

void WallboxBLE::process_real_time_status_(
    const std::string &json)
{

    cJSON *root =
        cJSON_Parse(json.c_str());


    if (root == nullptr) {

        ESP_LOGW(TAG, "Could not parse Wallbox real-time status");

        return;
    }

    /*
    {"id":48,"r":{"charger_status":6,"external_meter_status":2,"lock_status":1,"max_available_current":25,"max_charging_current":6,"mid_status":1,"ocpp_status":1,"phases_connection":0,"power_sharing_status":0}}
    */

    // --------------------------------------------------------------------------
    // Status
    // --------------------------------------------------------------------------

    cJSON *status =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "charger_status"
        );


    if (cJSON_IsNumber(status)) {

        int value =
            status->valueint;


        ESP_LOGD(
            TAG,
            "Status: %d (%s)",
            value,
            this->status_to_string_(value)
        );


        if (this->status_sensor_ != nullptr) {
            this->status_sensor_->publish_state(
                value
            );
        }


        if (this->status_text_sensor_ != nullptr) {

            this->status_text_sensor_->publish_state(
                this->status_to_string_(value)
            );
        }


        bool charging =
            (value == 1);


        if (this->charging_binary_sensor_ != nullptr) {

            this->charging_binary_sensor_->publish_state(
                charging
            );
        }


        if (this->charging_switch_ != nullptr) {

            this->charging_switch_->publish_state(
                charging
            );
        }
    }

    // --------------------------------------------------------------------------
    // Maximum available current
    // --------------------------------------------------------------------------

    cJSON *max_available_current =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "max_available_current"
        );

    if (cJSON_IsNumber(max_available_current)) {

        int value =
            max_available_current->valueint;


        ESP_LOGD(TAG, "Max available current: %d", value);

        if (this->max_current_sensor_ != nullptr) {

            this->max_current_sensor_->publish_state(value);
        }
    }

    // --------------------------------------------------------------------------
    // Lock status
    // --------------------------------------------------------------------------
    cJSON *lock_status =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "lock_status"
        );

    if (cJSON_IsNumber(lock_status)) {

        int value =
            lock_status->valueint;

        ESP_LOGD(TAG, "Lock status: %d", value);

        if (this->lock_switch_ != nullptr) {
            if (this->lock_switch_->state != value)
                this->lock_switch_->publish_state(value);
        }
    }

    cJSON_Delete(root);
}

// ============================================================================
// Live session energy feed
// ============================================================================

void WallboxBLE::process_live_energy_feed_(
    const std::string &json)
{

    cJSON *root =
        cJSON_Parse(json.c_str());


    if (root == nullptr) {

        ESP_LOGW(TAG, "Could not parse Wallbox live session energy feed");

        return;
    }

    /*
      {"id":11,"r":{"active_feature" {"feature":0,"feature_detail":11},"charged_energy":0,"charging_power":0,"charging_time":0,
      "control_mode":0,"discharged_energy":0,"discharging_time":0,"green_energy":0,"grid_energy":0,"start_time":1,"user_id":1}}    
    */

    // --------------------------------------------------------------------------
    // Charged energy
    // --------------------------------------------------------------------------

    cJSON *energy =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "charged_energy"
        );


    if (cJSON_IsNumber(energy)) {

        int value =  energy->valueint;

        ESP_LOGD(TAG, "Energy: %f", value);

        if (this->session_energy_sensor_ != nullptr) {
            this->session_energy_sensor_->publish_state(
                value
            );
        }
    }

    // --------------------------------------------------------------------------
    // Charging power
    // --------------------------------------------------------------------------

    cJSON *power =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "charging_power"
        );

    if (cJSON_IsNumber(power)) {

        int value = power->valueint;

        ESP_LOGD(TAG, "Power: %f", value);

        if (this->charging_power_sensor_ != nullptr) {
            this->charging_power_sensor_->publish_state(
                value
            );
        }
    }

    cJSON_Delete(root);
}

// ============================================================================
// Firmware version
// ============================================================================

void WallboxBLE::process_firmware_version_(
    const std::string &json)
{

    cJSON *root =
        cJSON_Parse(json.c_str());


    if (root == nullptr) {

        ESP_LOGW(TAG, "Could not parse Wallbox live session energy feed");

        return;
    }

    /*
      {"c":"CM3","cf":250,"db":111,"fw":871,"gm":-1,"id":26,"p":"prj08-pulsar-plus","r":871,"s":"6.7.43","sx":-1}
    */

    // --------------------------------------------------------------------------
    // Software version
    // --------------------------------------------------------------------------
    cJSON *fw =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "s"
        );
    char *sw = fw->valuestring;
    
    ESP_LOGD(TAG, "FW: %s", sw);

    if (this->firmware_version_text_sensor_ != nullptr) {
        this->firmware_version_text_sensor_->publish_state(
            sw
        );
    }

    cJSON_Delete(root);
}

// ============================================================================
// Set maximum charging current
// ============================================================================

void WallboxBLE::set_max_current(
    float value)
{

    if (!this->authenticated_) {

        ESP_LOGW(
            TAG,
            "Ignoring maximum current command: not authenticated"
        );

        return;
    }


    ESP_LOGI(
        TAG,
        "Setting maximum charging current to %.1f A",
        value
    );


    this->request_(
        "w_mxI",
        std::to_string(value)
    );
}


// ============================================================================
// Start / stop charging
// ============================================================================

void WallboxBLE::set_charging(
    bool enabled)
{

    if (!this->authenticated_) {

        ESP_LOGW(
            TAG,
            "Ignoring charging command: not authenticated"
        );

        return;
    }


    ESP_LOGI(
        TAG,
        "%s charging",
        enabled ? "Starting" : "Stopping"
    );


    this->request_(
        "w_cha",
        enabled ? "1" : "0"
    );
}

// ============================================================================
// Lock / unlock charger
// ============================================================================
void WallboxBLE::set_lock(
    bool enabled)
{

    if (!this->authenticated_) {

        ESP_LOGW(
            TAG,
            "Ignoring lock command: not authenticated"
        );

        return;
    }

    ESP_LOGI(
        TAG,
        "%s Wallbox",
        enabled ? "Locking" : "UnLocking"
    );

    this->request_(
        "w_lck",
        enabled ? "1" : "0"
    );
}

// ============================================================================
// Status conversion
// ============================================================================

const char *WallboxBLE::status_to_string_(
    int status)
{

    switch (status) {

    case 0:
        return "Ready";

    case 1:
        return "Charging";

    case 2:
        return "Connected waiting for car";

    case 3:
        return "Connected waiting for schedule";

    case 4:
        return "Paused";

    case 5:
        return "Schedule end";

    case 6:
        return "Locked";

    case 7:
        return "Error";

    case 8:
        return "Waiting current assignment";

    case 9:
        return "Unconfigured power sharing";

    case 10:
        return "Queue by power boost";

    case 11:
        return "Discharging";

    case 17:
        return "Updating";

    case 18:
        return "Queue by ECO smart";

    case 19:
        return "Locked, Car connected";

    default:
        return "Unknown";
    }
}


// ============================================================================
// Connected state
// ============================================================================

void WallboxBLE::set_connected_(
    bool connected)
{

    if (this->connected_binary_sensor_ != nullptr) {

        this->connected_binary_sensor_->publish_state(
            connected
        );
    }
}


// ============================================================================
// Reset connection
// ============================================================================

void WallboxBLE::reset_connection_()
{

    this->connected_ = false;

    this->notifications_enabled_ = false;

    this->authenticated_ = false;

    this->authentication_pending_ = false;

    this->user_id_ = -1;


    this->request_in_flight_ = false;

    this->pending_request_id_ = 0;

    this->pending_method_.clear();


    this->write_in_progress_ = false;

    this->stream_mode_write_pending_ = false;


    this->rx_handle_ = 0;

    this->tx_handle_ = 0;

    this->mode_handle_ = 0;


    this->request_queue_.clear();

    this->write_queue_.clear();

    this->rx_buffer_.clear();


    this->state_ = State::DISCONNECTED;


    this->set_connected_(false);


    if (this->charging_binary_sensor_ != nullptr) {

        this->charging_binary_sensor_->publish_state(
            false
        );
    }


    if (this->charging_switch_ != nullptr) {

        this->charging_switch_->publish_state(
            false
        );
    }
}


// ============================================================================
// Compatibility helper
// ============================================================================

bool WallboxBLE::is_request_complete_(
    const std::vector<uint8_t> &buffer)
{

    int depth = 0;

    bool started = false;

    bool in_string = false;

    bool escaped = false;


    for (uint8_t byte : buffer) {

        char c =
            static_cast<char>(byte);


        if (!started) {

            if (c == '{') {

                started = true;

                depth = 1;
            }

            continue;
        }


        if (in_string) {

            if (escaped) {

                escaped = false;

            } else if (c == '\\') {

                escaped = true;

            } else if (c == '"') {

                in_string = false;
            }

            continue;
        }


        if (c == '"') {

            in_string = true;

        } else if (c == '{') {

            depth++;

        } else if (c == '}') {

            depth--;

            if (depth == 0) {
                return true;
            }
        }
    }


    return false;
}

}  // namespace wallbox_ble
}  // namespace esphome
