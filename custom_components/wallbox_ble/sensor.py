import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_KILOWATT_HOURS,
)

from . import WallboxBLE

DEPENDENCIES = ["wallbox_ble"]

CONF_WALLBOX_BLE_ID = "wallbox_ble_id"

CONF_STATUS = "status"
CONF_CHARGING_CURRENT = "charging_current"
CONF_MAX_CHARGING_CURRENT = "max_charging_current"
CONF_SESSION_ENERGY = "session_energy"


CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_WALLBOX_BLE_ID): cv.use_id(WallboxBLE),

        cv.Optional(CONF_STATUS): sensor.sensor_schema(
            accuracy_decimals=0,
        ),

        cv.Optional(CONF_CHARGING_CURRENT): sensor.sensor_schema(
            unit_of_measurement=UNIT_AMPERE,
            device_class=DEVICE_CLASS_CURRENT,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
        ),

        cv.Optional(CONF_MAX_CHARGING_CURRENT): sensor.sensor_schema(
            unit_of_measurement=UNIT_AMPERE,
            device_class=DEVICE_CLASS_CURRENT,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=1,
        ),

        cv.Optional(CONF_SESSION_ENERGY): sensor.sensor_schema(
            unit_of_measurement=UNIT_KILOWATT_HOURS,
            device_class=DEVICE_CLASS_ENERGY,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=2,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(
        config[CONF_WALLBOX_BLE_ID]
    )

    if CONF_STATUS in config:
        sens = await sensor.new_sensor(
            config[CONF_STATUS]
        )
        cg.add(parent.set_status_sensor(sens))

    if CONF_CHARGING_CURRENT in config:
        sens = await sensor.new_sensor(
            config[CONF_CHARGING_CURRENT]
        )
        cg.add(parent.set_current_sensor(sens))

    if CONF_MAX_CHARGING_CURRENT in config:
        sens = await sensor.new_sensor(
            config[CONF_MAX_CHARGING_CURRENT]
        )
        cg.add(parent.set_max_current_sensor(sens))

    if CONF_SESSION_ENERGY in config:
        sens = await sensor.new_sensor(
            config[CONF_SESSION_ENERGY]
        )
        cg.add(parent.set_session_energy_sensor(sens))
