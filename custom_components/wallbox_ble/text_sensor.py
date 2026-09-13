import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import text_sensor

from . import WallboxBLE

DEPENDENCIES = ["wallbox_ble"]

CONF_WALLBOX_BLE_ID = "wallbox_ble_id"


CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_WALLBOX_BLE_ID): cv.use_id(WallboxBLE),

        cv.Optional("status"): text_sensor.text_sensor_schema(),
    }
)


async def to_code(config):
    parent = await cg.get_variable(
        config[CONF_WALLBOX_BLE_ID]
    )

    if "status" in config:
        sens = await text_sensor.new_text_sensor(
            config["status"]
        )

        cg.add(
            parent.set_status_text_sensor(sens)
        )
