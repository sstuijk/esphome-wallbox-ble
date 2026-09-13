import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import number

from . import WallboxBLE, wallbox_ble_ns

WallboxMaxCurrentNumber = wallbox_ble_ns.class_(
    "WallboxMaxCurrentNumber",
    number.Number,
)

CONF_WALLBOX_BLE_ID = "wallbox_ble_id"


CONFIG_SCHEMA = number.number_schema(
    WallboxMaxCurrentNumber,
    unit_of_measurement="A",
    icon="mdi:current-ac",
).extend(
    {
        cv.Required(
            CONF_WALLBOX_BLE_ID
        ): cv.use_id(WallboxBLE),
    }
)


async def to_code(config):

    var = await number.new_number(
        config,
        min_value=6,
        max_value=25,
        step=1,
    )

    parent = await cg.get_variable(
        config[CONF_WALLBOX_BLE_ID]
    )

    cg.add(var.set_parent(parent))

    cg.add(parent.set_max_current_number(var))
