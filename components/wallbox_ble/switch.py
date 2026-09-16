import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import switch

from . import WallboxBLE, wallbox_ble_ns

DEPENDENCIES = ["wallbox_ble"]


WallboxChargingSwitch = wallbox_ble_ns.class_(
    "WallboxChargingSwitch",
    switch.Switch,
)


WallboxLockSwitch = wallbox_ble_ns.class_(
    "WallboxLockSwitch",
    switch.Switch,
)


CONF_WALLBOX_BLE_ID = "wallbox_ble_id"

CONF_CHARGING = "charging"
CONF_LOCK = "lock"


CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_WALLBOX_BLE_ID): cv.use_id(WallboxBLE),

        cv.Optional(CONF_CHARGING): switch.switch_schema(
            WallboxChargingSwitch
        ),

        cv.Optional(CONF_LOCK): switch.switch_schema(
            WallboxLockSwitch
        ),
    }
)


async def to_code(config):

    parent = await cg.get_variable(
        config[CONF_WALLBOX_BLE_ID]
    )


    # Charging switch
    if CONF_CHARGING in config:

        var = cg.new_Pvariable(
            config[CONF_CHARGING]["id"]
        )

        await switch.register_switch(
            var,
            config[CONF_CHARGING]
        )

        cg.add(
            var.set_parent(parent)
        )

        cg.add(
            parent.set_charging_switch(var)
        )


    # Lock switch
    if CONF_LOCK in config:

        var = cg.new_Pvariable(
            config[CONF_LOCK]["id"]
        )

        await switch.register_switch(
            var,
            config[CONF_LOCK]
        )

        cg.add(
            var.set_parent(parent)
        )

        cg.add(
            parent.set_lock_switch(var)
        )
