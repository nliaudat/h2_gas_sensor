"""``MQ_gas_sensors`` - ESPHome sensor platform for MQ gas sensors.

One entry configures one MQ sensor (MQ-2 ... MQ-309A, see ``coefficients.py``).
The analog signal comes either from an existing voltage sampler (``voltage:``,
e.g. an ``adc`` sensor) or from a pin that this component owns (``pin:``, which
creates a hidden internal ``adc`` sensor with the requested attenuation).
"""

import logging

import esphome.codegen as cg
from esphome.components import sensor, voltage_sampler
import esphome.config_validation as cv
from esphome.const import (
    CONF_ATTENUATION,
    CONF_CALIBRATION,
    CONF_DELAY,
    CONF_DURATION,
    CONF_ID,
    CONF_INTERNAL,
    CONF_NAME,
    CONF_PIN,
    CONF_RAW,
    CONF_UPDATE_INTERVAL,
    CONF_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    UNIT_PARTS_PER_MILLION,
)
from esphome.types import ConfigType

from . import (
    CONF_A,
    CONF_ADC_ATTENUATION,
    CONF_ADC_SAMPLES,
    CONF_B,
    CONF_CORRECTION_FACTOR,
    CONF_GAS,
    CONF_MAX_PPM,
    CONF_MIN_PPM,
    CONF_PERSIST,
    CONF_RATIO_IN_CLEAN_AIR,
    CONF_RATIO_MODE,
    CONF_RATIO_SENSOR,
    CONF_REGRESSION_METHOD,
    CONF_RL,
    CONF_RS_SENSOR,
    CONF_R0,
    CONF_SAMPLES,
    CONF_SAMPLE_INTERVAL,
    CONF_SENSOR_TYPE,
    CONF_VCC,
    CONF_VOLTAGE_MULTIPLIER,
    CONF_VOLTAGE_SENSOR,
    CONF_WARMUP_TIME,
    MQGasSensor,
    RATIO_MODES,
    REGRESSION_METHODS,
)
from .coefficients import (
    SENSOR_TYPES,
    label_for,
    normalize_gas,
    normalize_type,
    requires_coefficients,
    resolve_sensor,
    sensor_types,
)

_LOGGER = logging.getLogger(__name__)

AUTO_LOAD = ["sensor", "voltage_sampler", "adc"]

ADC_ATTENUATIONS = ("0db", "2.5db", "6db", "11db", "12db", "auto")


def validate_sensor_type(value: str) -> str:
    """Accept ``mq-8`` / ``MQ_8`` / ``MQ8`` and return the canonical type key."""
    key = normalize_type(value)
    if key not in SENSOR_TYPES:
        raise cv.Invalid(
            f"'{value}' is not a supported MQ sensor type, expected one of: "
            f"{', '.join(sensor_types())}"
        )
    return key


def validate_gas(value: str) -> str:
    """Normalise ``gas:`` to the keys used by the coefficient table (``h2`` -> ``H2``)."""
    return normalize_gas(value)


#: Private key under which the configuration of the generated, internal ``adc``
#: sensor is kept. It has to live inside the validated config (and not in
#: ``CORE.data``) so that ESPHome's ID pass sees the declared component ID.
_KEY_GENERATED_ADC = "_generated_adc"


def _build_internal_adc(config: ConfigType) -> ConfigType:
    """Validate the hidden ``adc`` entry of a ``pin:`` configuration.

    The ADC platform's own schema is used, so `pin:` accepts exactly what a
    ``sensor: - platform: adc`` entry accepts (pin validation, attenuation,
    multisampling, ...). It has to run during config validation - not in
    ``to_code()`` - because ESPHome's validators rely on the config path context,
    which only exists while validating.
    """
    from esphome.components.adc import sensor as adc_sensor

    adc_id = f"{config[CONF_ID].id}_adc"
    adc_config: ConfigType = {
        CONF_ID: adc_id,
        CONF_PIN: config[CONF_PIN],
        CONF_NAME: f"{adc_id} voltage",
        CONF_INTERNAL: True,
        CONF_RAW: False,
        adc_sensor.CONF_SAMPLES: config[CONF_ADC_SAMPLES],
        adc_sensor.CONF_SAMPLING_MODE: "avg",
        CONF_UPDATE_INTERVAL: "never",
    }
    if (attenuation := config.get(CONF_ADC_ATTENUATION)) is not None:
        adc_config[CONF_ATTENUATION] = attenuation

    return adc_sensor.CONFIG_SCHEMA(adc_config)


CALIBRATION_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_RATIO_IN_CLEAN_AIR): cv.positive_not_null_float,
        cv.Optional(CONF_DELAY, default="60s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_DURATION, default="0s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SAMPLES, default=50): cv.int_range(min=1, max=1000),
        cv.Optional(CONF_PERSIST, default=True): cv.boolean,
    }
)


def _validate_config(config: ConfigType) -> ConfigType:
    """Cross-check the type/gas/coefficient combination at compile time."""
    type_key = config[CONF_SENSOR_TYPE]
    label = label_for(type_key)

    try:
        resolved = resolve_sensor(
            type_key,
            config.get(CONF_GAS),
            a=config.get(CONF_A),
            b=config.get(CONF_B),
            method=config.get(CONF_REGRESSION_METHOD),
            ratio_in_clean_air=config.get(CONF_RATIO_IN_CLEAN_AIR),
            rl=config.get(CONF_RL),
            vcc=config.get(CONF_VCC),
            min_ppm=config.get(CONF_MIN_PPM),
            max_ppm=config.get(CONF_MAX_PPM),
        )
    except ValueError as err:
        raise cv.Invalid(str(err)) from err

    if resolved.max_ppm <= resolved.min_ppm:
        raise cv.Invalid(
            f"max_ppm ({resolved.max_ppm}) must be greater than "
            f"min_ppm ({resolved.min_ppm})"
        )

    if CONF_R0 in config and CONF_CALIBRATION in config:
        _LOGGER.warning(
            "%s: both 'r0:' and 'calibration:' are set - the fixed R0 value wins and "
            "automatic calibration is disabled",
            label,
        )

    if CONF_R0 not in config and CONF_CALIBRATION not in config:
        _LOGGER.warning(
            "%s: neither 'r0:' nor 'calibration:' is set - the PPM output stays "
            "unknown until R0 is known (calibrate the sensor in clean air first)",
            label,
        )

    if resolved.heater_note:
        _LOGGER.info("%s: %s", label, resolved.heater_note)

    if requires_coefficients(type_key):
        _LOGGER.warning(
            "%s has no built-in coefficients in the reference library - make sure the "
            "'a:'/'b:' values you supplied match your sensor and target gas",
            label,
        )

    if CONF_PIN in config:
        config[_KEY_GENERATED_ADC] = _build_internal_adc(config)

    return config


CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(
        MQGasSensor,
        unit_of_measurement=UNIT_PARTS_PER_MILLION,
        accuracy_decimals=1,
        state_class=STATE_CLASS_MEASUREMENT,
        icon="mdi:molecule",
    )
    .extend(
        {
            cv.Required(CONF_SENSOR_TYPE): cv.All(cv.string, validate_sensor_type),
            cv.Optional(CONF_GAS): cv.All(cv.string, validate_gas),
            cv.Optional(CONF_VOLTAGE): cv.use_id(voltage_sampler.VoltageSampler),
            cv.Optional(CONF_PIN): cv.valid,
            cv.SplitDefault(CONF_ADC_ATTENUATION, esp32="12db"): cv.All(
                cv.only_on_esp32, cv.one_of(*ADC_ATTENUATIONS, lower=True)
            ),
            cv.Optional(CONF_ADC_SAMPLES, default=1): cv.int_range(min=1, max=255),
            cv.Optional(
                CONF_VOLTAGE_MULTIPLIER, default=1.0
            ): cv.positive_not_null_float,
            cv.Optional(CONF_VCC, default=5.0): cv.positive_not_null_float,
            cv.Optional(CONF_RL, default=10.0): cv.positive_not_null_float,
            cv.Optional(CONF_R0): cv.positive_not_null_float,
            cv.Optional(CONF_A): cv.float_,
            cv.Optional(CONF_B): cv.float_,
            cv.Optional(CONF_REGRESSION_METHOD): cv.one_of(
                *REGRESSION_METHODS, lower=True
            ),
            cv.Optional(CONF_RATIO_MODE, default="rs_r0"): cv.one_of(
                *RATIO_MODES, lower=True
            ),
            cv.Optional(CONF_RATIO_IN_CLEAN_AIR): cv.positive_not_null_float,
            cv.Optional(CONF_SAMPLES, default=2): cv.int_range(min=1, max=255),
            cv.Optional(
                CONF_SAMPLE_INTERVAL, default="20ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_WARMUP_TIME, default="0s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_MIN_PPM): cv.float_,
            cv.Optional(CONF_MAX_PPM): cv.positive_not_null_float,
            cv.Optional(CONF_CORRECTION_FACTOR, default=0.0): cv.float_,
            cv.Optional(CONF_CALIBRATION): CALIBRATION_SCHEMA,
            cv.Optional(CONF_RATIO_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_RS_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_VOLTAGE_SENSOR): cv.use_id(sensor.Sensor),
        }
    )
    .extend(cv.polling_component_schema("60s")),
    cv.has_exactly_one_key(CONF_VOLTAGE, CONF_PIN),
    _validate_config,
)


async def _create_internal_adc(config: ConfigType):
    """Instantiate the hidden, internal ``adc`` sensor used by ``pin:`` configurations.

    The entry was validated by :func:`_build_internal_adc`, here the ADC platform's
    own ``to_code`` is reused so attenuation, sampling count, calibration and
    platform specifics behave exactly like a hand written
    ``sensor: - platform: adc`` entry.
    """
    from esphome.components.adc import sensor as adc_sensor

    adc_config = config.get(_KEY_GENERATED_ADC)
    if adc_config is None:  # pragma: no cover - would be a bug in this component
        raise ValueError(
            f"internal adc sensor for '{config[CONF_ID].id}' was not generated, "
            "the 'pin:' configuration could not be validated"
        )

    await adc_sensor.to_code(adc_config)
    return await cg.get_variable(adc_config[CONF_ID])


async def to_code(config: ConfigType) -> None:
    resolved = resolve_sensor(
        config[CONF_SENSOR_TYPE],
        config.get(CONF_GAS),
        a=config.get(CONF_A),
        b=config.get(CONF_B),
        method=config.get(CONF_REGRESSION_METHOD),
        ratio_in_clean_air=config.get(CONF_RATIO_IN_CLEAN_AIR),
        rl=config.get(CONF_RL),
        vcc=config.get(CONF_VCC),
        min_ppm=config.get(CONF_MIN_PPM),
        max_ppm=config.get(CONF_MAX_PPM),
    )

    calibration = config.get(CONF_CALIBRATION)
    ratio_in_clean_air = resolved.ratio_in_clean_air
    if calibration is not None and CONF_RATIO_IN_CLEAN_AIR in calibration:
        ratio_in_clean_air = calibration[CONF_RATIO_IN_CLEAN_AIR]

    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)

    cg.add(var.set_type(resolved.label))
    cg.add(var.set_gas(resolved.gas))
    cg.add(var.set_a(resolved.a))
    cg.add(var.set_b(resolved.b))
    cg.add(var.set_regression_method(REGRESSION_METHODS[resolved.method]))
    cg.add(var.set_ratio_mode(RATIO_MODES[config[CONF_RATIO_MODE]]))
    cg.add(var.set_rl(resolved.rl))
    cg.add(var.set_vcc(resolved.vcc))
    cg.add(var.set_ratio_in_clean_air(ratio_in_clean_air))
    cg.add(var.set_min_ppm(resolved.min_ppm))
    cg.add(var.set_max_ppm(resolved.max_ppm))
    cg.add(var.set_voltage_multiplier(config[CONF_VOLTAGE_MULTIPLIER]))
    cg.add(var.set_samples(config[CONF_SAMPLES]))
    cg.add(var.set_sample_interval(config[CONF_SAMPLE_INTERVAL]))
    cg.add(var.set_warmup_time(config[CONF_WARMUP_TIME]))
    cg.add(var.set_correction_factor(config[CONF_CORRECTION_FACTOR]))

    if (r0 := config.get(CONF_R0)) is not None:
        cg.add(var.set_r0(r0))

    if calibration is not None and CONF_R0 not in config:
        cg.add(
            var.set_calibration(
                True,
                ratio_in_clean_air,
                calibration[CONF_DELAY],
                calibration[CONF_DURATION],
                calibration[CONF_SAMPLES],
                calibration[CONF_PERSIST],
            )
        )

    if source_id := config.get(CONF_VOLTAGE):
        cg.add(var.set_source(await cg.get_variable(source_id)))
    else:
        cg.add(var.set_source(await _create_internal_adc(config)))

    for key, setter in (
        (CONF_RATIO_SENSOR, var.set_ratio_sensor),
        (CONF_RS_SENSOR, var.set_rs_sensor),
        (CONF_VOLTAGE_SENSOR, var.set_voltage_sensor),
    ):
        if (target := config.get(key)) is not None:
            cg.add(setter(await cg.get_variable(target)))

    _LOGGER.debug(
        "%s %s: a=%s b=%s method=%s ratio_in_clean_air=%s rl=%s vcc=%s range=%s..%s ppm",
        resolved.label,
        resolved.gas,
        resolved.a,
        resolved.b,
        resolved.method,
        ratio_in_clean_air,
        resolved.rl,
        resolved.vcc,
        resolved.min_ppm,
        resolved.max_ppm,
    )


