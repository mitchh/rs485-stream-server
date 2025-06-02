import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome import pins
from esphome.const import (
    CONF_ID,
    CONF_PORT,
    CONF_BUFFER_SIZE, # Standard constant for buffer size
)

# Custom configuration keys
CONF_TX_ENABLE_PIN = "tx_enable_pin"
# CONF_BUFFER_SIZE is already defined in esphome.const, we'll use that.
# If we needed a specific buffer, e.g. for TCP RX before UART TX, we might define
# CONF_TCP_RX_BUFFER_SIZE = "tcp_rx_buffer_size"
# For this example, we'll assume CONF_BUFFER_SIZE refers to the TCP RX buffer.
CONF_CLIENT_TIMEOUT = "client_timeout" # Inactivity timeout for TCP clients

# Namespace and class declaration for C++
rs485_stream_server_ns = cg.esphome_ns.namespace("rs485_stream_server")
RS485StreamServerComponent = rs485_stream_server_ns.class_(
    "RS485StreamServer", cg.Component, uart.UARTDevice
)

CONFIG_SCHEMA = cv.All(
    cv.COMPONENT_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(RS485StreamServerComponent),
            cv.Required(CONF_PORT): cv.port,
            cv.Required(CONF_TX_ENABLE_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_BUFFER_SIZE, default=128): cv.positive_int, # Defaulting to 128 as in C++ header
            cv.Optional(CONF_CLIENT_TIMEOUT, default="5min"): cv.positive_time_period_milliseconds,
        }
    ).extend(uart.UART_DEVICE_SCHEMA), # Inherit standard UART configurations
)

async def to_code(config):
    """
    Translates the YAML configuration into C++ code.
    This function is called by ESPHome to generate the necessary C++
    code for this component.
    """
    # Create a C++ global variable for the component
    var = cg.new_Pvariable(config[CONF_ID])
    # Register the component with ESPHome
    await cg.register_component(var, config)
    # Register the component as a UART device
    await uart.register_uart_device(var, config)

    # Pass configuration values to the C++ object's setter methods
    cg.add(var.set_port(config[CONF_PORT]))

    # Get the GPIO pin object for the TX enable pin
    tx_enable_pin_obj = await cg.gpio_pin_expression(config[CONF_TX_ENABLE_PIN])
    cg.add(var.set_tx_enable_pin(tx_enable_pin_obj))

    # Pass buffer size and client timeout
    if CONF_BUFFER_SIZE in config:
        cg.add(var.set_tcp_rx_buffer_size(config[CONF_BUFFER_SIZE])) # Use a more specific setter name
    if CONF_CLIENT_TIMEOUT in config:
        cg.add(var.set_client_timeout(config[CONF_CLIENT_TIMEOUT]))

    # Add required libraries for AsyncTCP based on platform
    if cg.is_esp32:
        cg.add_library("WiFi", None) # AsyncTCP depends on WiFi
        cg.add_library("FS", None) # For ESPAsyncWebServer, often a dependency or good to have
        cg.add_library("ESPAsyncWebServer-esphome", "3.1.0") # Provides AsyncTCP for ESP32
    elif cg.is_esp8266:
        cg.add_library("ESPAsyncTCP", "1.2.2")

