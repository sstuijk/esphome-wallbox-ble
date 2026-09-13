import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import binary_sensor
from esphome.const import (
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_BATTERY_CHARGING,
)

from . import WallboxBLE

DEPENDENCIES = ["wallbox_ble"]

CONF_WALLBOX_BLE_ID = "wallbox_ble_id"


CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_WALLBOX_BLE_ID): cv.use_id(WallboxBLE),

        cv.Optional("connected"): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
        ),

        cv.Optional("charging"): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY_CHARGING,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(
        config[CONF_WALLBOX_BLE_ID]
    )

    if "connected" in config:
        sens = await binary_sensor.new_binary_sensor(
            config["connected"]
        )
        cg.add(parent.set_connected_binary_sensor(sens))

    if "charging" in config:
        sens = await binary_sensor.new_binary_sensor(
            config["charging"]
        )
        cg.add(parent.set_charging_binary_sensor(sens))
