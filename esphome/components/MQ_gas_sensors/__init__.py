"""MQ gas sensor library for ESPHome.

A native ESPHome port of the MQUnifiedsensor PPM model used by the
`MQSensorsLib` / `esp-iot-solution MQSensorLIB` ESP-IDF component
(https://github.com/RapportTecnologia/esp-iot-solution/tree/MQSensorLib/components/sensors/gas/MQSensorLIB).

It supports every MQ sensor of the MQUnifiedsensor family:

    MQ-2, MQ-3, MQ-4, MQ-5, MQ-6, MQ-7, MQ-8, MQ-9, MQ-131, MQ-135,
    MQ-136, MQ-137, MQ-138, MQ-214, MQ-303A, MQ-309A and CUSTOM.

The analog signal is either taken from an existing ESPHome voltage-sampler
sensor (``voltage:``) or from a pin owned by this component (``pin:``, which
creates a hidden, internal ``adc`` sensor for you).
"""

import esphome.codegen as cg
from esphome.components import sensor

CODEOWNERS = ["@nl"]
AUTO_LOAD = ["sensor", "voltage_sampler"]

mq_gas_sensors_ns = cg.esphome_ns.namespace("mq_gas_sensors")

# C++ class: class MQGasSensor : public sensor::Sensor, public PollingComponent
MQGasSensor = mq_gas_sensors_ns.class_(
    "MQGasSensor", sensor.Sensor, cg.PollingComponent
)

# ---------------------------------------------------------------------------
# Configuration keys
# ---------------------------------------------------------------------------
CONF_SENSOR_TYPE = "sensor_type"
CONF_GAS = "gas"
CONF_VOLTAGE_MULTIPLIER = "voltage_multiplier"
CONF_ADC_ATTENUATION = "adc_attenuation"
CONF_ADC_SAMPLES = "adc_samples"
CONF_RL = "rl"
CONF_R0 = "r0"
CONF_VCC = "vcc"
CONF_A = "a"
CONF_B = "b"
CONF_REGRESSION_METHOD = "regression_method"
CONF_RATIO_MODE = "ratio_mode"
CONF_RATIO_IN_CLEAN_AIR = "ratio_in_clean_air"
CONF_SAMPLES = "samples"
CONF_SAMPLE_INTERVAL = "sample_interval"
CONF_WARMUP_TIME = "warmup_time"
CONF_MIN_PPM = "min_ppm"
CONF_MAX_PPM = "max_ppm"
CONF_CORRECTION_FACTOR = "correction_factor"
CONF_PERSIST = "persist"
CONF_RATIO_SENSOR = "ratio_sensor"
CONF_RS_SENSOR = "rs_sensor"
CONF_VOLTAGE_SENSOR = "voltage_sensor"

# MQUnifiedsensor::setRegressionMethod() values.
REGRESSION_METHODS = {
    "exponential": 1,  # _PPM = a * ratio^b
    "linear": 2,  # log10(_PPM) = (log10(ratio) - b) / a
}

# MQUnifiedsensor uses R0/RS (see readSensorR0Rs(), "INVERTED for MQ-131"),
# while the published datasheet coefficients are fitted against RS/R0.
RATIO_MODES = {
    "rs_r0": 0,  # datasheet convention (default)
    "r0_rs": 1,  # MQUnifiedsensor readSensorR0Rs() convention
}
