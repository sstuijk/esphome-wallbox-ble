import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import text_sensor

from . import WallboxBLE

DEPENDENCIES = ["wallbox_ble"]

CONF_WALLBOX_BLE_ID = "wallbox_ble_id"

CONF_STATUS = "status"
CONF_FIRMWARE_VERSION = "firmware_version"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_WALLBOX_BLE_ID): cv.use_id(WallboxBLE),

        cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(),
        
        cv.Optional(CONF_FIRMWARE_VERSION): text_sensor.text_sensor_schema(
            icon="mdi:memory"),
    }
)


async def to_code(config):
    parent = await cg.get_variable(
        config[CONF_WALLBOX_BLE_ID]
    )

    if CONF_STATUS in config:
        sens = await text_sensor.new_text_sensor(
            config[CONF_STATUS]
        )
        cg.add(
            parent.set_status_text_sensor(sens)
        )
    
    if CONF_FIRMWARE_VERSION in config:
        sens = await text_sensor.new_text_sensor(
            config[CONF_FIRMWARE_VERSION]
        )
        cg.add(
            parent.set_firmware_version_text_sensor(sens)
        )
