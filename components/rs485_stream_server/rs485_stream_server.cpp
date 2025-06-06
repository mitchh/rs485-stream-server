#include "rs485_stream_server.h"
#include "esphome/core/log.h"

// Try to include BinaryOutput header if available
#if __has_include("esphome/components/output/binary_output.h")
#include "esphome/components/output/binary_output.h"
#define HAS_BINARY_OUTPUT 1
#else
#define HAS_BINARY_OUTPUT 0
#endif

namespace esphome
{
  namespace rs485_stream_server
  {

    static const char *const TAG = "rs485_stream_server";

    float RS485StreamServer::get_setup_priority() const
    {
      // Ensure UART and WiFi/network are set up before this component
      return esphome::setup_priority::AFTER_WIFI;
    }

    void RS485StreamServer::setup()
    {
      ESP_LOGCONFIG(TAG, "Setting up RS485 Stream Server...");

      // Initialize Transmit Enable Output (optional)
      if (this->tx_enable_output_ != nullptr)
      {
#if HAS_BINARY_OUTPUT
        // Set to receive mode (LOW/false) initially
        this->tx_enable_output_->turn_off();
        ESP_LOGD(TAG, "TX Enable Output initialized to OFF (Receive Mode)");
#else
        ESP_LOGW(TAG, "TX Enable Output configured but BinaryOutput component not available");
#endif
      }
      else
      {
        ESP_LOGD(TAG, "TX Enable Output not configured - using automatic direction control or half-duplex mode");
      }

      // Initialize UART RX Buffer
      this->uart_rx_buffer_.reserve(this->MAX_UART_RX_BUFFER_SIZE_);

      // Initialize TCP Server
#ifdef USE_ESP32
      this->server_ = new AsyncTCPServer(this->port_);
#elif USE_ESP8266
      this->server_ = new AsyncServer(this->port_);
#endif
      if (this->server_ == nullptr)
      {
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
          [this](void *arg, AsyncClient *client)
          { this->handle_new_client(client); },
          nullptr);

      this->server_->begin();
      ESP_LOGCONFIG(TAG, "TCP Server started on port %u", this->port_);
    }

    void RS485StreamServer::loop()
    {
      // Process incoming UART data - read ALL available bytes aggressively
      // Only read from UART if not currently transmitting (TE pin is LOW)
      // and if there's space in our application-level UART RX buffer.
      if (!this->transmitting_ && this->uart_rx_buffer_.size() < this->MAX_UART_RX_BUFFER_SIZE_)
      {
        size_t bytes_available = this->available();

        if (bytes_available > 0)
        {
          ESP_LOGV(TAG, "UART available: %zu bytes", bytes_available);

          // Read in chunks to prevent blocking - yield every 8 bytes
          size_t total_bytes_read = 0;
          const size_t MAX_BYTES_PER_LOOP = 64; // Process max 64 bytes per loop iteration

          // Since read_array() seems broken, let's try reading byte by byte
          // but with byte count limits and frequent yields
          while (this->available() > 0 &&
                 this->uart_rx_buffer_.size() < this->MAX_UART_RX_BUFFER_SIZE_ &&
                 total_bytes_read < MAX_BYTES_PER_LOOP)
          {
            uint8_t single_byte = this->read();
            this->uart_rx_buffer_.push_back(single_byte);
            total_bytes_read++;
            ESP_LOGV(TAG, "Read single byte: 0x%02X (total: %zu)", single_byte, total_bytes_read);

            // Yield every 8 bytes to prevent watchdog issues
            if (total_bytes_read % 8 == 0)
            {
              yield();
            }
          }

          if (total_bytes_read > 0)
          {
            ESP_LOGV(TAG, "UART RX total: %zu bytes read byte-by-byte. %zu bytes still available",
                     total_bytes_read, this->available());
          }
        }
      }

      // If UART RX buffer has data, try to send to all connected TCP clients
      if (!this->uart_rx_buffer_.empty())
      {
        if (this->clients_.empty())
        {
          // No clients connected, discard UART data to prevent buffer overflow
          ESP_LOGD(TAG, "No TCP clients connected, discarding %zu UART bytes", this->uart_rx_buffer_.size());
          this->uart_rx_buffer_.clear();
        }
        else
        {
          bool all_data_sent_to_at_least_one_client = true; // Assume true, set to false if any client fails
          for (AsyncClient *client : this->clients_)
          {
            if (client->connected() && client->canSend())
            {
              size_t space_in_client_buffer = client->space();
              if (space_in_client_buffer >= this->uart_rx_buffer_.size())
              {
                client->add(reinterpret_cast<const char *>(this->uart_rx_buffer_.data()), this->uart_rx_buffer_.size());
                client->send();
                ESP_LOGV(TAG, "Forwarded %zu bytes from UART to TCP client %s", this->uart_rx_buffer_.size(), client->remoteIP().toString().c_str());
              }
              else
              {
                ESP_LOGW(TAG, "TCP client %s send buffer full (space: %zu, needed: %zu). Cannot send UART data yet.",
                         client->remoteIP().toString().c_str(), space_in_client_buffer, this->uart_rx_buffer_.size());
                all_data_sent_to_at_least_one_client = false; // Data not sent to this client
                // Keep data in uart_rx_buffer_ for next attempt.
                // Break to avoid sending to other clients if order is critical or to simplify logic.
                // If order is not critical per client, this break could be removed.
                break;
              }
            }
            else if (client->connected() && !client->canSend())
            {
              ESP_LOGV(TAG, "TCP client %s connected but cannot send now.", client->remoteIP().toString().c_str());
              all_data_sent_to_at_least_one_client = false;
            }
          }
          // Clear the UART buffer only if ALL data was successfully sent to at least one client
          if (all_data_sent_to_at_least_one_client)
          {
            this->uart_rx_buffer_.clear();
          }
        }
      }

      this->cleanup_disconnected_clients(); // Periodically remove dead clients
    }

    void RS485StreamServer::cleanup_disconnected_clients()
    {
      this->clients_.erase(
          std::remove_if(this->clients_.begin(), this->clients_.end(),
                         [](AsyncClient *c) { // Lambda needs to capture `this` if accessing members, but not needed here.
                           if (!c->connected())
                           {
                             // ESP_LOGV(TAG, "Removing disconnected client: %s", c->remoteIP().toString().c_str()); // Potentially noisy
                             // The client object itself is managed by AsyncTCPServer library,
                             // so no need to `delete c;` here.
                             return true; // Mark for removal from our vector
                           }
                           return false;
                         }),
          this->clients_.end());
    }

    void RS485StreamServer::handle_new_client(AsyncClient *client)
    {
      if (client == nullptr)
      {
        ESP_LOGW(TAG, "handle_new_client called with null client pointer.");
        return;
      }
      ESP_LOGD(TAG, "New TCP client connected: %s (Total clients: %zu)",
               client->remoteIP().toString().c_str(), this->clients_.size() + 1);
      this->clients_.push_back(client);

      // Set up callbacks for this specific client
#ifdef USE_ESP32
      client->onData(
          [this](void *arg, AsyncClient *aclient, uint8_t *data, size_t len)
          {
            this->handle_data(arg, aclient, data, len);
          },
          nullptr);
#elif USE_ESP8266
      client->onData(
          [this](void *arg, AsyncClient *aclient, void *data, size_t len)
          {
            this->handle_data(arg, aclient, data, len);
          },
          nullptr);
#endif

      client->onDisconnect(
          [this](void *arg, AsyncClient *aclient)
          { this->handle_disconnect(arg, aclient); }, nullptr);

      client->onError(
          [this](void *arg, AsyncClient *aclient, int8_t error)
          {
            this->handle_error(arg, aclient, error);
          },
          nullptr);

      client->onTimeout(
          [this](void *arg, AsyncClient *aclient, uint32_t time)
          {
            this->handle_timeout(arg, aclient, time);
          },
          nullptr);

      if (this->client_timeout_ms_ > 0)
      {
        client->setRxTimeout(this->client_timeout_ms_ / 1000); // AsyncTCP timeout is in seconds for setRxTimeout
        ESP_LOGD(TAG, "Set RxTimeout to %u ms for client %s", this->client_timeout_ms_, client->remoteIP().toString().c_str());
      }
      // client->setAckTimeout(this->client_timeout_ms_); // Consider if ACK timeout is needed
    }

// Handle data from TCP clients - ESP8266 and ESP32 have different callback signatures
#ifdef USE_ESP32
    void RS485StreamServer::handle_data(void *arg, AsyncClient *client, uint8_t *tcp_data, size_t len)
    {
#elif USE_ESP8266
    void RS485StreamServer::handle_data(void *arg, AsyncClient *client, void *data, size_t len)
    {
      uint8_t *tcp_data = static_cast<uint8_t *>(data);
#endif
      ESP_LOGV(TAG, "TCP RX from %s: %zu bytes", client->remoteIP().toString().c_str(), len);

      if (this->transmitting_)
      {
        ESP_LOGW(TAG, "Already transmitting UART data, dropping %zu bytes from TCP client %s", len, client->remoteIP().toString().c_str());
        // Optionally, buffer this TCP data if UART is busy. For now, drop.
        // A more robust solution might queue TCP data or signal busy to the client if possible.
        return;
      }
      this->transmitting_ = true;

      // RS485 Transmit Enable Logic (optional)
      if (this->tx_enable_output_ != nullptr)
      {
#if HAS_BINARY_OUTPUT
        this->tx_enable_output_->turn_on();
        ESP_LOGV(TAG, "Set TX_ENABLE_OUTPUT ON for UART TX");

        // Optional delay for transceiver to switch to TX mode
        if (this->tx_enable_delay_us_ > 0)
        {
          delayMicroseconds(this->tx_enable_delay_us_);
          ESP_LOGV(TAG, "TX enable delay: %u microseconds", this->tx_enable_delay_us_);
        }
#else
        ESP_LOGW(TAG, "TX Enable Output configured but BinaryOutput component not available");
#endif
      }

      this->write_array(tcp_data, len);
      ESP_LOGV(TAG, "Wrote %zu bytes to UART", len);

      // CRITICAL: Wait for UART transmission to complete.
      this->flush(); // Waits for TX buffer and shift register to empty.
      ESP_LOGV(TAG, "UART flush() complete");

      if (this->tx_enable_output_ != nullptr)
      {
#if HAS_BINARY_OUTPUT
        // Optional delay for bus settling or transceiver turnaround
        if (this->tx_disable_delay_us_ > 0)
        {
          delayMicroseconds(this->tx_disable_delay_us_);
          ESP_LOGV(TAG, "TX disable delay: %u microseconds", this->tx_disable_delay_us_);
        }

        this->tx_enable_output_->turn_off();
        ESP_LOGV(TAG, "Set TX_ENABLE_OUTPUT OFF, back to UART RX mode");
#endif
      }

      this->transmitting_ = false;
    }

    void RS485StreamServer::handle_disconnect(void *arg, AsyncClient *client)
    {
      ESP_LOGD(TAG, "TCP client disconnected: %s", client->remoteIP().toString().c_str());
      // Client will be removed from clients_ vector by cleanup_disconnected_clients() in the next loop() call.
      // No need to delete client here; AsyncTCPServer manages its lifecycle.
    }

    void RS485StreamServer::handle_error(void *arg, AsyncClient *client, int8_t error)
    {
      ESP_LOGW(TAG, "TCP client error %s: %s (code %d)", client->remoteIP().toString().c_str(), client->errorToString(error), error);
      // Client will likely disconnect or be closed by the library. Cleanup will handle removal.
    }

    void RS485StreamServer::handle_timeout(void *arg, AsyncClient *client, uint32_t time)
    {
      ESP_LOGD(TAG, "TCP client timeout %s after %u seconds of inactivity.", client->remoteIP().toString().c_str(), time);
      client->close(true); // Close the timed-out client connection immediately.
      // Cleanup will handle removal from our vector.
    }

    void RS485StreamServer::dump_config()
    {
      ESP_LOGCONFIG(TAG, "RS485 Stream Server:");
      ESP_LOGCONFIG(TAG, "  TCP Port: %u", this->port_);
      if (this->tx_enable_output_ != nullptr)
      {
        ESP_LOGCONFIG(TAG, "  TX Enable Output: Configured");
        ESP_LOGCONFIG(TAG, "  TX Enable Delay: %uus", this->tx_enable_delay_us_);
        ESP_LOGCONFIG(TAG, "  TX Disable Delay: %uus", this->tx_disable_delay_us_);
      }
      else
      {
        ESP_LOGCONFIG(TAG, "  TX Enable Output: Not configured (automatic direction control)");
      }
      ESP_LOGCONFIG(TAG, "  Configured TCP RX Buffer Size (for UART TX): %u bytes", this->tcp_rx_buffer_size_);
      ESP_LOGCONFIG(TAG, "  Internal Max UART RX Buffer Size: %zu bytes", this->MAX_UART_RX_BUFFER_SIZE_);
      ESP_LOGCONFIG(TAG, "  Client Inactivity Timeout: %ums", this->client_timeout_ms_);
    }

    void RS485StreamServer::on_shutdown()
    {
      ESP_LOGD(TAG, "Shutting down RS485 Stream Server...");
      for (AsyncClient *client : this->clients_)
      {
        if (client->connected())
        {
          client->stop(); // Gracefully stop/close the client
        }
      }
      this->clients_.clear();

      if (this->server_ != nullptr)
      {
        this->server_->end(); // Stop the TCP server
        delete this->server_; // Clean up the server object
        this->server_ = nullptr;
      }

      if (this->tx_enable_output_ != nullptr)
      {
#if HAS_BINARY_OUTPUT
        this->tx_enable_output_->turn_off(); // Ensure TX enable is off (receive mode) on shutdown
        ESP_LOGD(TAG, "TX Enable Output set to OFF on shutdown");
#endif
      }
      ESP_LOGD(TAG, "RS485 Stream Server shutdown complete.");
    }

  } // namespace rs485_stream_server
} // namespace esphome
