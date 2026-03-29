import esphome.codegen as cg
import esphome.config_validation as cv

spi_scan_ns = cg.esphome_ns.namespace("spi_scan")
SpiScanComponent = spi_scan_ns.class_("SpiScanComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SpiScanComponent),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[cv.CONF_ID])
    await cg.register_component(var, config)
