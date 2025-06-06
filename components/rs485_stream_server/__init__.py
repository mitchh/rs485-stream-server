import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, output
from esphome.const import (
    CONF_ID,
    CONF_PORT,
    CONF_OUTPUT,
)

DEPENDENCIES = ["uart", "network"]

# Add conditional dependency for output when tx_enable_output is used
def validate_config(config):
    if CONF_TX_ENABLE_OUTPUT in config:
        # If tx_enable_output is configured, we need the output component
        return cv.Schema({**CONFIG_SCHEMA.schema, cv.Required("output"): cv.ensure_list})(config)
    return config

rs485_stream_server_ns = cg.esphome_ns.namespace("rs485_stream_server")
RS485StreamServer = rs485_stream_server_ns.class_(
    "RS485StreamServer", cg.Component, uart.UARTDevice
)

CONF_TX_ENABLE_OUTPUT = "tx_enable_output"
CONF_TCP_PORT = "tcp_port"
CONF_CLIENT_TIMEOUT = "client_timeout"
CONF_TCP_RX_BUFFER_SIZE = "tcp_rx_buffer_size"
CONF_TX_ENABLE_DELAY = "tx_enable_delay"
CONF_TX_DISABLE_DELAY = "tx_disable_delay"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(RS485StreamServer),
            cv.Optional(CONF_TX_ENABLE_OUTPUT): cv.use_id(output.BinaryOutput),
            cv.Optional(CONF_TCP_PORT, default=8080): cv.port,
            cv.Optional(CONF_CLIENT_TIMEOUT, default="30s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TCP_RX_BUFFER_SIZE, default=256): cv.positive_int,
            cv.Optional(CONF_TX_ENABLE_DELAY, default="0us"): cv.positive_time_period_microseconds,
            cv.Optional(CONF_TX_DISABLE_DELAY, default="0us"): cv.positive_time_period_microseconds,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_TX_ENABLE_OUTPUT in config:
        tx_enable_output = await cg.get_variable(config[CONF_TX_ENABLE_OUTPUT])
        cg.add(var.set_tx_enable_output(tx_enable_output))

    cg.add(var.set_port(config[CONF_TCP_PORT]))
    cg.add(var.set_client_timeout(config[CONF_CLIENT_TIMEOUT]))
    cg.add(var.set_tcp_rx_buffer_size(config[CONF_TCP_RX_BUFFER_SIZE]))
    cg.add(var.set_tx_enable_delay_us(config[CONF_TX_ENABLE_DELAY]))
    cg.add(var.set_tx_disable_delay_us(config[CONF_TX_DISABLE_DELAY]))
