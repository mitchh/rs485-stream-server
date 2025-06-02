import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import (
    CONF_ID,
    CONF_PORT,
)
from esphome import pins

DEPENDENCIES = ["uart", "network"]

rs485_stream_server_ns = cg.esphome_ns.namespace("rs485_stream_server")
RS485StreamServer = rs485_stream_server_ns.class_(
    "RS485StreamServer", cg.Component, uart.UARTDevice
)

CONF_TX_ENABLE_PIN = "tx_enable_pin"
CONF_TCP_PORT = "tcp_port"
CONF_CLIENT_TIMEOUT = "client_timeout"
CONF_TCP_RX_BUFFER_SIZE = "tcp_rx_buffer_size"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(RS485StreamServer),
            cv.Required(CONF_TX_ENABLE_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_TCP_PORT, default=8080): cv.port,
            cv.Optional(CONF_CLIENT_TIMEOUT, default="30s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TCP_RX_BUFFER_SIZE, default=256): cv.positive_int,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    tx_enable_pin = await cg.gpio_pin_expression(config[CONF_TX_ENABLE_PIN])
    cg.add(var.set_tx_enable_pin(tx_enable_pin))
    
    cg.add(var.set_port(config[CONF_TCP_PORT]))
    cg.add(var.set_client_timeout(config[CONF_CLIENT_TIMEOUT]))
    cg.add(var.set_tcp_rx_buffer_size(config[CONF_TCP_RX_BUFFER_SIZE]))