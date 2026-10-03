import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import ble_client

CODEOWNERS = ["Sander Stuijk"]

DEPENDENCIES = ["ble_client"]

wallbox_ble_ns = cg.esphome_ns.namespace("wallbox_ble")

WallboxBLE = wallbox_ble_ns.class_(
    "WallboxBLE",
    cg.Component,
    ble_client.BLEClientNode,
)

CONF_POLL_INTERVAL = "poll_interval"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WallboxBLE),

        cv.GenerateID("ble_client_id"): cv.use_id(
            ble_client.BLEClient
        ),

        cv.Optional(
            CONF_POLL_INTERVAL,
            default="10s",
        ): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[cv.CONF_ID])

    await cg.register_component(var, config)

    ble_client_var = await cg.get_variable(
        config["ble_client_id"]
    )

    cg.add(
        ble_client_var.register_ble_node(var)
    )

    cg.add(
        var.set_poll_interval(
            config[CONF_POLL_INTERVAL].total_milliseconds
        )
    )
