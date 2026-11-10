"""Pixel Weather: renders weather screens and uploads them to an iDotMatrix 16x16 panel over BLE."""

import esphome.codegen as cg
from esphome.components import ble_client, sensor, text_sensor, time
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIME_ID

DEPENDENCIES = ["ble_client", "time"]
CODEOWNERS = []

pixel_weather_ns = cg.esphome_ns.namespace("pixel_weather")
PixelWeather = pixel_weather_ns.class_("PixelWeather", cg.PollingComponent, ble_client.BLEClientNode)

SENSORS = ["temperature", "high", "low", "uv_index", "lightning_distance"]
TEXT_SENSORS = ["condition", "rain_forecast", "sun", "last_lightning"]

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PixelWeather),
            cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            cv.Required("temperature"): cv.use_id(sensor.Sensor),
            **{cv.Optional(k): cv.use_id(sensor.Sensor) for k in SENSORS if k != "temperature"},
            **{cv.Optional(k): cv.use_id(text_sensor.TextSensor) for k in TEXT_SENSORS},
        }
    )
    .extend(cv.polling_component_schema("15min"))
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    clock = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_time(clock))
    for key in SENSORS:
        if key in config:
            s = await cg.get_variable(config[key])
            cg.add(getattr(var, f"set_{key}_sensor")(s))
    for key in TEXT_SENSORS:
        if key in config:
            s = await cg.get_variable(config[key])
            cg.add(getattr(var, f"set_{key}_text_sensor")(s))
