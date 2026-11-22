/*
 * Copyright (c) 2019 Vestas Wind Systems A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#if defined(CONFIG_BOARD_NATIVE_SIM)
#include <zephyr/drivers/gpio/gpio_emul.h>
#endif
#include <canopennode.h>
#include "io.h"
#include "native_sim_gpio.h"
#define LOG_LEVEL CONFIG_CANOPENNODE_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(io);

static struct gpio_dt_spec led_green_gpio = GPIO_DT_SPEC_GET_OR(
		DT_ALIAS(green_led), gpios, {0});
static struct gpio_dt_spec led_red_gpio = GPIO_DT_SPEC_GET_OR(
		DT_ALIAS(red_led), gpios, {0});

#if defined(CONFIG_BOARD_NATIVE_SIM)
static const struct gpio_dt_spec io_gpio[] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(io1_sw), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(io2_sw), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(io3_sw), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(io4_sw), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(io5_sw), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(io6_sw), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(io7_sw), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(io8_sw), gpios),
};
#else
static const struct device *pcf857x;
static const int inputLookupTable[8] = {8,9,10,11,12,13,14,15};
static const int outputLookupTable[8] = {0,1,2,3,4,5,6,7};
#endif
static uint8_t output[8] = {0};

static void config_relais(void);

/**
 * @brief Callback for setting LED indicator state.
 *
 * @param value true if the LED indicator shall be turned on, false otherwise.
 * @param arg argument that was passed when LEDs were initialized.
 */
static void led_callback(bool value, void *arg)
{
	struct gpio_dt_spec *led_gpio = arg;

	if (!led_gpio || !led_gpio->port) {
		return;
	}

	gpio_pin_set_dt(led_gpio, value);
}

/**
 * @brief Configure LED indicators pins and callbacks.
 *
 * This routine configures the GPIOs for the red and green LEDs (if
 * available).
 *
 * @param nmt CANopenNode NMT object.
 */
static void config_leds(CO_NMT_t *nmt)
{
	int err;

	if (!led_green_gpio.port) {
		LOG_INF("Green LED not available");
	} else if (!gpio_is_ready_dt(&led_green_gpio)) {
		LOG_ERR("Green LED device not ready");
		led_green_gpio.port = NULL;
	} else {
		err = gpio_pin_configure_dt(&led_green_gpio,
					    GPIO_OUTPUT_INACTIVE);
		if (err) {
			LOG_ERR("failed to configure Green LED gpio: %d", err);
			led_green_gpio.port = NULL;
		}
	}

	if (!led_red_gpio.port) {
		LOG_INF("Red LED not available");
	} else if (!gpio_is_ready_dt(&led_red_gpio)) {
		LOG_ERR("Red LED device not ready");
		led_red_gpio.port = NULL;
	} else {
		err = gpio_pin_configure_dt(&led_red_gpio,
					    GPIO_OUTPUT_INACTIVE);
		if (err) {
			LOG_ERR("failed to configure Red LED gpio: %d", err);
			led_red_gpio.port = NULL;
		}
	}

	canopen_leds_init(nmt,
			  led_callback, &led_green_gpio,
			  led_callback, &led_red_gpio);
}

static void config_relais(void) {
#if defined(CONFIG_BOARD_NATIVE_SIM)
	for (size_t i = 0; i < ARRAY_SIZE(io_gpio); i++) {
		/* Active-low OD values: one means the load is off. */
		OD_outputs[i] = 1;
		output[i] = 1;
		if (!gpio_is_ready_dt(&io_gpio[i]) ||
		    gpio_pin_configure_dt(&io_gpio[i], GPIO_OUTPUT_INACTIVE) != 0) {
			LOG_ERR("Unable to initialize I/O output %u", (unsigned int)i + 1U);
		}
	}
	uint8_t gpio_states[ARRAY_SIZE(io_gpio)];
	for (size_t i = 0; i < ARRAY_SIZE(io_gpio); i++) {
		gpio_states[i] = (uint8_t)(gpio_emul_output_get_dt(&io_gpio[i]) > 0);
	}
	native_sim_gpio_publish(46008U, gpio_states, ARRAY_SIZE(gpio_states));
#else
    pcf857x = DEVICE_DT_GET(DT_NODELABEL(pcf8575));
	if (!pcf857x) {
		LOG_INF("device not found");
		return;
	}
	for (int i=0;i<8;i++) {
		gpio_pin_configure(pcf857x, (gpio_pin_t)outputLookupTable[i], GPIO_OUTPUT);
	}
	for (int i=0;i<8;i++) {
		gpio_pin_configure(pcf857x, (gpio_pin_t)inputLookupTable[i], GPIO_INPUT);
	}
#endif
}
void io_write_output(void) {
#if defined(CONFIG_BOARD_NATIVE_SIM)
	for (size_t i = 0; i < ARRAY_SIZE(io_gpio); i++) {
		/* Emulated GPIO is active-high, while OD output values are active-low. */
		if (output[i] != (uint8_t)OD_outputs[i] &&
		    gpio_pin_set_dt(&io_gpio[i], OD_outputs[i] == 0) == 0) {
			output[i] = (uint8_t)OD_outputs[i];
		}
	}
	uint8_t states[ARRAY_SIZE(io_gpio)];
	for (size_t i = 0; i < ARRAY_SIZE(io_gpio); i++) {
		states[i] = (uint8_t)(gpio_emul_output_get_dt(&io_gpio[i]) > 0);
	}
	native_sim_gpio_publish(46008U, states, ARRAY_SIZE(states));
#else
	for (uint8_t i=0;i<8;i++) {
		if (output[i] != (uint8_t)(OD_outputs[i])) {
			int set_value = 0; 
			if ((int)(OD_outputs[i]) != 0) {
				set_value = 1;
			}
			int response = gpio_pin_set(pcf857x, outputLookupTable[i], set_value);
			if (response == 0) {
				output[i] = (uint8_t)(OD_outputs[i]);
			}
		}
	}
#endif
}

#if !defined(CONFIG_BOARD_NATIVE_SIM)
void io_read_input(void) {
	for (uint8_t i=0;i<8;i++) {
		uint8_t read = (uint8_t)(gpio_pin_get(pcf857x, (gpio_pin_t)inputLookupTable[i]));
		uint8_t new_input = 0;
		if (read != 0) {
			new_input = 1;
		}
		if ((uint8_t)(OD_inputs[i]) != new_input) {
			CO_LOCK_OD();
			OD_inputs[i] = (BOOLEAN)(new_input);
			CO_UNLOCK_OD();
		}
	}
}
#endif

void io_init(CO_NMT_t *nmt)
{
	config_leds(nmt);
	config_relais();
}
