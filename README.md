# RS485 Stream Server ESPHome Component

A custom ESPHome component that creates a TCP-to-RS485 bridge, allowing network clients to communicate with RS485 devices through an ESP32.

## Development Setup

### Prerequisites

1. **ESPHome installed**: `pip3 install esphome`
2. **PlatformIO** (optional, for advanced debugging)
3. **clang-format** for C++ code formatting: `brew install clang-format`
4. **black** for Python code formatting: `pip3 install black`

### VS Code Setup

This workspace is configured with:

- C++ IntelliSense for ESP32/ESPHome development
- Python linting and formatting
- ESPHome-specific tasks and debugging
- Proper file associations and formatting rules

### Available Tasks

Press `Cmd+Shift+P` (macOS) and type "Tasks: Run Task" to access:

1. **ESPHome: Validate Component** - Test ESPHome imports
2. **Format C++ Code** - Format .cpp and .h files
3. **Format Python Code** - Format .py files

### Component Structure

- `rs485_stream_server.h` - Component header with class definition
- `rs485_stream_server.cpp` - Component implementation
- `__init__.py` - ESPHome configuration schema and code generation

### Usage in ESPHome YAML

```yaml
external_components:
  - source: github://mitchh/rs485-stream-server
    components: [ rs485_stream_server ]

uart:
  id: uart_bus
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 9600

output:
  - platform: gpio
    id: flow_control_output
    pin: GPIO4

rs485_stream_server:
  port: 6683
  tx_enable_output flow_control_output
  uart_id: uart_bus
  buffer_size: 256
  client_timeout: 5min
```

### Development Workflow

1. Edit component files
2. Run formatting tasks to maintain code style
3. Test with a real ESPHome configuration
4. Use `esphome compile` to verify compilation

### Testing

Create a test YAML configuration in a separate directory:

```bash
mkdir test-config
cd test-config
# Create your test .yaml file with the component
esphome compile test-device.yaml
```
