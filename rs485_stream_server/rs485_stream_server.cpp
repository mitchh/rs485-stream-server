#include "rs485_stream_server.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h" // For App.scheduler (not directly used here but good include for components)

namespace esphome {
namespace rs485_stream_server {

static const char *const TAG = "rs485_stream_server";

float RS485StreamServer::get_setup_priority() const {
  // Ensure UART and WiFi/network are set up before this component
  return esphome::setup_priority::AFTER_WIFI;
}

void RS485StreamServer::setup() {
  ESP_LOGCONFIG(TAG, "Setting up RS485 Stream Server...");

  // Initialize Transmit Enable Pin
  if (this->tx_enable_pin_ == nullptr) {
    ESP_LOGE(TAG, "TX Enable Pin is not configured!");
    this->mark_failed();
    return;
  }
  this->tx_enable_pin_->setup();
  this->tx_enable_pin_->digital_write(false); // Default to receive mode (LOW)
  ESP_LOGD(TAG, "TX Enable Pin initialized to LOW (Receive Mode)");

  // Initialize UART RX Buffer
  this->uart_rx_buffer_.reserve(this->MAX_UART_RX_BUFFER_SIZE_);

  // Initialize TCP Server
#ifdef USE_ESP32
  this->server_ = new AsyncTCPServer(this->port_);
#elif USE_ESP8266
  this->server_ = new AsyncServer(this->port_);
#endif
  if (this->server_ == nullptr) {
#ifdef USE_ESP32
    ESP_LOGE(TAG, "Failed to create AsyncTCPServer object!");
#elif USE_ESP8266
    ESP_LOGE(TAG, "Failed to create AsyncServer object!");
#endif
    this->mark_failed();
    return;
  }

  // Lambda for new client connection
  this->server_->onClient(
      [this](void *arg, AsyncClient *client) { this->handle_new_client(client); },
      nullptr);

  this->server_->begin();
  ESP_LOGCONFIG(TAG, "TCP Server started on port %u", this->port_);
}

void RS485StreamServer::loop() {
  // Process incoming UART data
  // Only read from UART if not currently transmitting (TE pin is LOW)
  // and if there's space in our application-level UART RX buffer.
  if (!this->transmitting_ && this->uart_rx_buffer_.size() < this->MAX_UART_RX_BUFFER_SIZE_) {
    size_t available_uart = this->available();
    if (available_uart > 0) {
      size_t max_read = std::min(available_uart, UART_READ_CHUNK_SIZE);
      max_read = std::min(max_read, this->MAX_UART_RX_BUFFER_SIZE_ - this->uart_rx_buffer_.size());
      
      if (max_read > 0) {
        size_t len_read = this->read_array(this->temp_uart_chunk_buffer_, max_read);
        if (len_read > 0) {
          ESP_LOGV(TAG, "UART RX: %zu bytes", len_read);
          this->uart_rx_buffer_.insert(this->uart_rx_buffer_.end(), this->temp_uart_chunk_buffer_, this->temp_uart_chunk_buffer_ + len_read);
        }
      }
    }
  }

  // If UART RX buffer has data, try to send to all connected TCP clients
  if (!this->uart_rx_buffer_.empty()) {
    if (this->clients_.empty()) {
        // No clients connected, discard UART data to prevent buffer overflow
        ESP_LOGD(TAG, "No TCP clients connected, discarding %zu UART bytes", this->uart_rx_buffer_.size());
        this->uart_rx_buffer_.clear();
    } else {
        bool all_data_sent_to_at_least_one_client = true; // Assume true, set to false if any client fails
        for (AsyncClient *client : this->clients_) {
            if (client->connected() && client->canSend()) {
                size_t space_in_client_buffer = client->space();
                if (space_in_client_buffer >= this->uart_rx_buffer_.size()) {
                    client->add(reinterpret_cast<const char*>(this->uart_rx_buffer_.data()), this->uart_rx_buffer_.size());
                    client->send();
                    ESP_LOGV(TAG, "Forwarded %zu bytes from UART to TCP client %s", this->uart_rx_buffer_.size(), client->remoteIP().toString().c_str());
                } else {
                    ESP_LOGW(TAG, "TCP client %s send buffer full (space: %zu, needed: %zu). Cannot send UART data yet.",
                             client->remoteIP().toString().c_str(), space_in_client_buffer, this->uart_rx_buffer_.size());
                    all_data_sent_to_at_least_one_client = false; // Data not sent to this client
                    // Keep data in uart_rx_buffer_ for next attempt.
                    // Break to avoid sending to other clients if order is critical or to simplify logic.
                    // If order is not critical per client, this break could be removed.
                    break; 
                }
            } else if (client->connected() && !client->canSend()) {
                 ESP_LOGV(TAG, "TCP client %s connected but cannot send now.", client->remoteIP().toString().c_str());
                 all_data_sent_to_at_least_one_client = false;
                 break; // Similar to buffer full, wait.
            }
        }
        
        // If data was successfully added to all clients that could accept it (or if only one client, it took it)
        // then clear the buffer.
        if (all_data_sent_to_at_least_one_client) {
            this->uart_rx_buffer_.clear();
        } else if (this->uart_rx_buffer_.size() > this->MAX_UART_RX_BUFFER_SIZE_ * 2) { // Heuristic to prevent unbounded growth
             ESP_LOGW(TAG, "UART RX buffer too large (%zu bytes) and not all clients can receive; discarding data to prevent overflow.", this->uart_rx_buffer_.size());
            this->uart_rx_buffer_.clear();
        }
    }
  }

  this->cleanup_disconnected_clients(); // Periodically remove dead clients
}

void RS485StreamServer::cleanup_disconnected_clients() {
  this->clients_.erase(
    std::remove_if(this->clients_.begin(), this->clients_.end(),
                  [](AsyncClient *c) { // Lambda needs to capture `this` if accessing members, but not needed here.
                     if (!c->connected()) {
                       // ESP_LOGV(TAG, "Removing disconnected client: %s", c->remoteIP().toString().c_str()); // Potentially noisy
                       // The client object itself is managed by AsyncTCPServer library,
                       // so no need to `delete c;` here.
                       return true; // Mark for removal from our vector
                     }
                     return false;
                   }),
    this->clients_.end());
}

void RS485StreamServer::handle_new_client(AsyncClient *client) {
  if (client == nullptr) {
    ESP_LOGW(TAG, "handle_new_client called with null client pointer.");
    return;
  }
  ESP_LOGD(TAG, "New TCP client connected: %s (Total clients: %zu)",
           client->remoteIP().toString().c_str(), this->clients_.size() + 1);
  this->clients_.push_back(client);

  // Set up callbacks for this specific client
#ifdef USE_ESP32
  client->onData(
      [this](void *arg, AsyncClient *aclient, uint8_t *data, size_t len) {
        this->handle_data(arg, aclient, data, len);
      },
      nullptr);
#elif USE_ESP8266
  client->onData(
      [this](void *arg, AsyncClient *aclient, void *data, size_t len) {
        this->handle_data(arg, aclient, data, len);
      },
      nullptr);
#endif

  client->onDisconnect(
      [this](void *arg, AsyncClient *aclient) { this->handle_disconnect(arg, aclient); }, nullptr);

  client->onError(
      [this](void *arg, AsyncClient *aclient, int8_t error) {
        this->handle_error(arg, aclient, error);
      },
      nullptr);

  client->onTimeout(
      [this](void *arg, AsyncClient *aclient, uint32_t time) {
        this->handle_timeout(arg, aclient, time);
      },
      nullptr);
  
  if (this->client_timeout_ms_ > 0) {
    client->setRxTimeout(this->client_timeout_ms_ / 1000); // AsyncTCP timeout is in seconds for setRxTimeout
    ESP_LOGD(TAG, "Set RxTimeout to %u ms for client %s", this->client_timeout_ms_, client->remoteIP().toString().c_str());
  }
  // client->setAckTimeout(this->client_timeout_ms_); // Consider if ACK timeout is needed
}

// Handle data from TCP clients - ESP8266 and ESP32 have different callback signatures
#ifdef USE_ESP32
void RS485StreamServer::handle_data(void *arg, AsyncClient *client, uint8_t *tcp_data, size_t len) {
#elif USE_ESP8266  
void RS485StreamServer::handle_data(void *arg, AsyncClient *client, void *data, size_t len) {
  uint8_t *tcp_data = static_cast<uint8_t*>(data);
#endif
  ESP_LOGV(TAG, "TCP RX from %s: %zu bytes", client->remoteIP().toString().c_str(), len);

  if (this->tx_enable_pin_ == nullptr) {
    ESP_LOGE(TAG, "TX Enable Pin not set, cannot transmit to UART!");
    return;
  }

  if (this->transmitting_) {
    ESP_LOGW(TAG, "Already transmitting UART data, dropping %zu bytes from TCP client %s", len, client->remoteIP().toString().c_str());
    // Optionally, buffer this TCP data if UART is busy. For now, drop.
    // A more robust solution might queue TCP data or signal busy to the client if possible.
    return;
  }
  this->transmitting_ = true;

  // RS485 Transmit Enable Logic
  this->tx_enable_pin_->digital_write(true);
  ESP_LOGV(TAG, "Set TX_ENABLE_PIN HIGH for UART TX");

  // Optional short delay for transceiver to switch mode. Usually not needed.
  // For example: App.scheduler.set_timeout(nullptr, "rs485_tx_delay", 1, []() { /* continue */ });
  // However, direct delayMicroseconds might be simpler if extremely short and acceptable.
  // delayMicroseconds(10); // Use with caution, can block. ESP-IDF vTaskDelay(1) is safer if > few us.

  this->write_array(tcp_data, len);
  ESP_LOGV(TAG, "Wrote %zu bytes to UART", len);

  // CRITICAL: Wait for UART transmission to complete.
  this->flush(); // Waits for TX buffer and shift register to empty.
  ESP_LOGV(TAG, "UART flush() complete");

  // Optional short delay for bus settling or transceiver turnaround.
  // delayMicroseconds(10); // Use with caution.

  this->tx_enable_pin_->digital_write(false);
  ESP_LOGV(TAG, "Set TX_ENABLE_PIN LOW, back to UART RX mode");

  this->transmitting_ = false;
}

void RS485StreamServer::handle_disconnect(void* arg, AsyncClient *client) {
  ESP_LOGD(TAG, "TCP client disconnected: %s", client->remoteIP().toString().c_str());
  // Client will be removed from clients_ vector by cleanup_disconnected_clients() in the next loop() call.
  // No need to delete client here; AsyncTCPServer manages its lifecycle.
}

void RS485StreamServer::handle_error(void* arg, AsyncClient *client, int8_t error) {
  ESP_LOGW(TAG, "TCP client error %s: %s (code %d)", client->remoteIP().toString().c_str(), client->errorToString(error), error);
  // Client will likely disconnect or be closed by the library. Cleanup will handle removal.
}

void RS485StreamServer::handle_timeout(void* arg, AsyncClient *client, uint32_t time) {
  ESP_LOGD(TAG, "TCP client timeout %s after %u seconds of inactivity.", client->remoteIP().toString().c_str(), time);
  client->close(true); // Close the timed-out client connection immediately.
  // Cleanup will handle removal from our vector.
}

void RS485StreamServer::dump_config() {
  ESP_LOGCONFIG(TAG, "RS485 Stream Server:");
  ESP_LOGCONFIG(TAG, "  TCP Port: %u", this->port_);
  LOG_PIN("  TX Enable Pin: ", this->tx_enable_pin_);
  ESP_LOGCONFIG(TAG, "  Configured TCP RX Buffer Size (for UART TX): %u bytes", this->tcp_rx_buffer_size_);
  ESP_LOGCONFIG(TAG, "  Internal Max UART RX Buffer Size: %zu bytes", this->MAX_UART_RX_BUFFER_SIZE_);
  ESP_LOGCONFIG(TAG, "  Client Inactivity Timeout: %ums", this->client_timeout_ms_);
  // Log UART settings manually since LOG_UART_DEVICE might not be available
  ESP_LOGCONFIG(TAG, "  UART Baud Rate: %u", this->get_baud_rate());
}

void RS485StreamServer::on_shutdown() {
  ESP_LOGD(TAG, "Shutting down RS485 Stream Server...");
  for (AsyncClient *client : this->clients_) {
    if (client->connected()) {
      client->stop(); // Gracefully stop/close the client
    }
  }
  this->clients_.clear();

  if (this->server_ != nullptr) {
    this->server_->end(); // Stop the TCP server
    delete this->server_; // Clean up the server object
    this->server_ = nullptr;
  }

  if (this->tx_enable_pin_ != nullptr) {
    this->tx_enable_pin_->digital_write(false); // Ensure TE pin is low on shutdown
    ESP_LOGD(TAG, "TX Enable Pin set to LOW on shutdown");
  }
  ESP_LOGD(TAG, "RS485 Stream Server shutdown complete.");
}

}  // namespace rs485_stream_server
}  // namespace esphome
