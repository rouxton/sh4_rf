"""
sh4_rf - ESPHome external component for Tuya SH4 RF module (CBU/BK7231N + CMT2300A).

433.92 MHz OOK, direct GPIO mode and FIFO packet mode.
TX + RX, integrates natively with ESPHome remote_base.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation, pins
from esphome.components import remote_base
from esphome.const import (
    CONF_BUFFER_SIZE,
    CONF_DUMP,
    CONF_FILTER,
    CONF_ID,
    CONF_IDLE,
    CONF_TOLERANCE,
    CONF_TYPE,
    CONF_VALUE,
)
from esphome.core import TimePeriod

# --- Custom config keys ---
CONF_SCLK_PIN            = "sclk_pin"
CONF_SDIO_PIN            = "sdio_pin"
CONF_CSB_PIN             = "csb_pin"
CONF_FCSB_PIN            = "fcsb_pin"
CONF_DATA_PIN            = "data_pin"   # bidirectional TX/RX pin (P20)
CONF_SPI_ENABLED         = "spi_enabled"
CONF_RX_MODE             = "rx_mode"
CONF_RECEIVER_DISABLED   = "receiver_disabled"
CONF_START_PULSE_MIN     = "start_pulse_min"
CONF_START_PULSE_MAX     = "start_pulse_max"
CONF_END_PULSE           = "end_pulse"
CONF_RECEIVER_ID         = "receiver_id"
CONF_LED_PIN             = "led_pin"
CONF_MODE                = "mode"

AUTO_LOAD    = ["remote_base"]
DEPENDENCIES = ["libretiny"]

sh4_rf_ns       = cg.esphome_ns.namespace("sh4_rf")
remote_base_ns  = cg.esphome_ns.namespace("remote_base")

ToleranceMode   = remote_base_ns.enum("ToleranceMode")
RxMode          = sh4_rf_ns.enum("RxMode", is_class=True)

TYPE_PERCENTAGE = "percentage"
TYPE_TIME       = "time"

TOLERANCE_MODE = {
    TYPE_PERCENTAGE: ToleranceMode.TOLERANCE_MODE_PERCENTAGE,
    TYPE_TIME:       ToleranceMode.TOLERANCE_MODE_TIME,
}

TOLERANCE_SCHEMA = cv.typed_schema(
    {
        TYPE_PERCENTAGE: cv.Schema(
            {cv.Required(CONF_VALUE): cv.All(cv.percentage_int, cv.uint32_t)}
        ),
        TYPE_TIME: cv.Schema(
            {
                cv.Required(CONF_VALUE): cv.All(
                    cv.positive_time_period_microseconds,
                    cv.Range(max=TimePeriod(microseconds=4294967295)),
                )
            }
        ),
    },
    lower=True,
    enum=TOLERANCE_MODE,
)

RX_MODE_OPTIONS = {
    "direct": RxMode.DIRECT,
    "fifo":   RxMode.FIFO,
}

SH4RfComponent        = sh4_rf_ns.class_(
    "SH4RfComponent",
    cg.Component,
    remote_base.RemoteTransmitterBase,
    remote_base.RemoteReceiverBase,
)
TurnOnReceiverAction  = sh4_rf_ns.class_("TurnOnReceiverAction",  automation.Action)
TurnOffReceiverAction = sh4_rf_ns.class_("TurnOffReceiverAction", automation.Action)
SetRxModeAction       = sh4_rf_ns.class_("SetRxModeAction",       automation.Action)

ACTION_SCHEMA = cv.Schema(
    {cv.GenerateID(CONF_RECEIVER_ID): cv.use_id(SH4RfComponent)}
)

SET_RX_MODE_SCHEMA = ACTION_SCHEMA.extend(
    {cv.Required(CONF_MODE): cv.enum(RX_MODE_OPTIONS, lower=True)}
)


@automation.register_action("sh4_rf.turn_on_receiver", TurnOnReceiverAction, ACTION_SCHEMA,
                            synchronous=True)
async def turn_on_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_RECEIVER_ID])
    return var


@automation.register_action("sh4_rf.turn_off_receiver", TurnOffReceiverAction, ACTION_SCHEMA,
                            synchronous=True)
async def turn_off_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_RECEIVER_ID])
    return var


@automation.register_action("sh4_rf.set_rx_mode", SetRxModeAction, SET_RX_MODE_SCHEMA,
                            synchronous=True)
async def set_rx_mode_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_RECEIVER_ID])
    cg.add(var.set_mode(config[CONF_MODE]))
    return var


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SH4RfComponent),

        # --- SPI pins (Variant A: SCLK/SDIO connected) ---
        # SDIO is bidirectional but declared as output here; direction is
        # switched at runtime in C++ via pin_mode(FLAG_INPUT / FLAG_OUTPUT).
        cv.Optional(CONF_SCLK_PIN): pins.internal_gpio_output_pin_schema,
        cv.Optional(CONF_SDIO_PIN): pins.internal_gpio_output_pin_schema,

        # --- Chip select pins ---
        cv.Required(CONF_CSB_PIN):  pins.internal_gpio_output_pin_schema,
        cv.Required(CONF_FCSB_PIN): pins.internal_gpio_output_pin_schema,

        # --- Bidirectional data pin (TX and RX share the same pin P20) ---
        cv.Required(CONF_DATA_PIN): pins.internal_gpio_pin_schema,

        # --- Optional status LED ---
        cv.Optional(CONF_LED_PIN): pins.internal_gpio_output_pin_schema,

        # --- Variant selection ---
        cv.Optional(CONF_SPI_ENABLED, default=True): cv.boolean,

        # --- Receive mode ---
        cv.Optional(CONF_RX_MODE, default="direct"): cv.enum(RX_MODE_OPTIONS, lower=True),

        # --- Receiver behaviour ---
        cv.Optional(CONF_RECEIVER_DISABLED, default=False): cv.boolean,
        cv.Optional(CONF_BUFFER_SIZE, default=1000): cv.All(
            cv.uint32_t, cv.Range(min=10, max=100000)
        ),
        cv.Optional(CONF_FILTER, default="50us"): cv.All(
            cv.positive_time_period_microseconds,
            cv.Range(max=TimePeriod(microseconds=100000)),
        ),
        cv.Optional(CONF_TOLERANCE, default="25%"): cv.Any(
            cv.percentage_int, TOLERANCE_SCHEMA
        ),
        cv.Optional(CONF_DUMP, default=[]): remote_base.validate_dumpers,
        cv.Optional(CONF_IDLE, default="10ms"): cv.All(
            cv.positive_time_period_microseconds,
            cv.Range(max=TimePeriod(microseconds=1000000)),
        ),

        # --- RF pulse thresholds ---
        cv.Optional(CONF_START_PULSE_MIN, default="6ms"): cv.All(
            cv.positive_time_period_microseconds,
            cv.Range(max=TimePeriod(microseconds=100000)),
        ),
        cv.Optional(CONF_START_PULSE_MAX, default="10ms"): cv.All(
            cv.positive_time_period_microseconds,
            cv.Range(max=TimePeriod(microseconds=100000)),
        ),
        cv.Optional(CONF_END_PULSE, default="50ms"): cv.All(
            cv.positive_time_period_microseconds,
            cv.Range(max=TimePeriod(microseconds=1000000)),
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    spi_enabled = config.get(CONF_SPI_ENABLED, True)

    sclk = await cg.gpio_pin_expression(config[CONF_SCLK_PIN]) if CONF_SCLK_PIN in config else cg.nullptr
    sdio = await cg.gpio_pin_expression(config[CONF_SDIO_PIN]) if CONF_SDIO_PIN in config else cg.nullptr
    csb  = await cg.gpio_pin_expression(config[CONF_CSB_PIN])
    fcsb = await cg.gpio_pin_expression(config[CONF_FCSB_PIN])
    data = await cg.gpio_pin_expression(config[CONF_DATA_PIN])

    var = cg.new_Pvariable(config[CONF_ID], sclk, sdio, csb, fcsb, data, data)
    await cg.register_component(var, config)

    # ESPHome 2026.x API: build_dumpers(config[CONF_DUMP]) returns a list,
    # each dumper is registered manually; same for triggers/listeners.
    dumpers = await remote_base.build_dumpers(config[CONF_DUMP])
    for dumper in dumpers:
        cg.add(var.register_dumper(dumper))

    triggers = await remote_base.build_triggers(config)
    for trigger in triggers:
        cg.add(var.register_listener(trigger))

    if CONF_LED_PIN in config:
        led = await cg.gpio_pin_expression(config[CONF_LED_PIN])
        cg.add(var.set_led_pin(led))

    cg.add(var.set_spi_enabled(spi_enabled))
    cg.add(var.set_rx_mode(config[CONF_RX_MODE]))
    cg.add(var.set_receiver_disabled(config[CONF_RECEIVER_DISABLED]))
    cg.add(var.set_buffer_size(config[CONF_BUFFER_SIZE]))
    cg.add(var.set_filter_us(config[CONF_FILTER].total_microseconds))
    cg.add(var.set_start_pulse_min_us(config[CONF_START_PULSE_MIN].total_microseconds))
    cg.add(var.set_start_pulse_max_us(config[CONF_START_PULSE_MAX].total_microseconds))
    cg.add(var.set_end_pulse_us(config[CONF_END_PULSE].total_microseconds))

    tol = config[CONF_TOLERANCE]
    if isinstance(tol, int):
        cg.add(var.set_tolerance(tol, remote_base_ns.TOLERANCE_MODE_PERCENTAGE))
    else:
        cg.add(var.set_tolerance(tol[CONF_VALUE], tol[CONF_TYPE]))
