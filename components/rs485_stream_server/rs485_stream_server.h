#ifndef RS485_STREAM_SERVER_H
#define RS485_STREAM_SERVER_H

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h" // For UARTDevice

#include <vector>    // For managing clients
#include <algorithm> // For std::remove_if

#ifdef USE_ESP32
#include <AsyncTCP.h> // From ESPAsyncWebServer-esphome library
#elif USE_ESP8266
#include <ESPAsyncTCP.h>
#endif

#include <IPAddress.h> // For logging client IPs

// Forward declaration to avoid including the header when not needed
namespace esphome
{
      namespace output
      {
            class BinaryOutput;
      }
}

namespace esphome
{
      namespace rs485_stream_server
      {

            class RS485StreamServer : public Component, public uart::UARTDevice
            {
            public:
                  // Lifecycle methods
                  void setup() override;
                  void loop() override;
                  void dump_config() override;
                  float get_setup_priority() const override;
                  void on_shutdown() override;

                  // Configuration setters (called from Python-generated code)
                  void set_port(uint16_t port) { this->port_ = port; }
                  void set_tx_enable_output(output::BinaryOutput *output) { this->tx_enable_output_ = output; }
                  // Buffer for data from TCP client before UART TX
                  void set_tcp_rx_buffer_size(uint32_t buffer_size) { this->tcp_rx_buffer_size_ = buffer_size; }
                  void set_client_timeout(uint32_t timeout) { this->client_timeout_ms_ = timeout; }
                  // RS485 transceiver switching delays
                  void set_tx_enable_delay_us(uint32_t delay) { this->tx_enable_delay_us_ = delay; }
                  void set_tx_disable_delay_us(uint32_t delay) { this->tx_disable_delay_us_ = delay; }

            protected:
                  // TCP Server and client management
#ifdef USE_ESP32
                  AsyncTCPServer *server_{nullptr};
#elif USE_ESP8266
                  AsyncServer *server_{nullptr};
#endif
                  std::vector<AsyncClient *> clients_;
                  void handle_new_client(AsyncClient *client);
                  // ESP8266 and ESP32 have different callback signatures
#ifdef USE_ESP32
                  void handle_data(void *arg, AsyncClient *client, uint8_t *data, size_t len);
#elif USE_ESP8266
                  void handle_data(void *arg, AsyncClient *client, void *data, size_t len);
#endif
                  void handle_disconnect(void *arg, AsyncClient *client);
                  void handle_error(void *arg, AsyncClient *client, int8_t error);
                  void handle_timeout(void *arg, AsyncClient *client, uint32_t time);
                  void cleanup_disconnected_clients();

                  // Configuration
                  uint16_t port_;
                  output::BinaryOutput *tx_enable_output_{nullptr};
                  uint32_t tcp_rx_buffer_size_{128};   // Default value, can be overridden by YAML
                  uint32_t client_timeout_ms_{300000}; // Default 5 minutes

                  // Internal buffer for UART RX data before sending to TCP
                  std::vector<uint8_t> uart_rx_buffer_;
                  // Max size for application-level UART RX buffer.
                  // UART hardware has its own rx_buffer_size (from YAML).
                  const size_t MAX_UART_RX_BUFFER_SIZE_ = 256; // Internal limit for this buffer

                  // RS485 Transmit State
                  bool transmitting_{false}; // Flag to indicate if currently in transmit mode

                  // Temporary buffer for reading from UART in loop()
                  // Sized to match typical small reads, can be adjusted.
                  // Using a fixed-size array on the stack for read_array is generally fine for small, controlled sizes.
                  // If larger, dynamic allocation or a member std::vector might be better.
                  // For this example, a small stack buffer is efficient.
                  // Max read chunk from UART at a time.
                  static const size_t UART_READ_CHUNK_SIZE = 64;
                  uint8_t temp_uart_chunk_buffer_[UART_READ_CHUNK_SIZE];

                  // RS485 transceiver switching delays
                  uint32_t tx_enable_delay_us_{0};  // Delay after enabling TX (in microseconds)
                  uint32_t tx_disable_delay_us_{0}; // Delay after disabling TX (in microseconds)
            };

      } // namespace rs485_stream_server
} // namespace esphome

#endif // RS485_STREAM_SERVER_H
