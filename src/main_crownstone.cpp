/**
 * Author: Anne van Rossum
 * Copyright: Distributed Organisms B.V. (DoBots)
 * Date: 14 Aug., 2014
 * License: LGPLv3+, Apache, or MIT, your choice
 */

/**********************************************************************************************************************
 * Enable the services you want to run on the device
 *********************************************************************************************************************/

#define INDOOR_SERVICE
#define GENERAL_SERVICE
#define POWER_SERVICE

/**********************************************************************************************************************
 * General includes
 *********************************************************************************************************************/

#include "BluetoothLE.h"

#if(NORDIC_SDK_VERSION < 5)
#include "ble_stack_handler.h"
#include "ble_nrf6310_pins.h"
#endif
#include "nrf51_bitfields.h"

extern "C" {
#include "ble_advdata_parser.h"
#include "nrf_delay.h"
#include "app_scheduler.h"
}

#include "nordic_common.h"
#include "nRF51822.h"

#include <stdbool.h>
#include <stdint.h>
#include <cstring>
#include <cstdio>

#include <common/boards.h>

#if(BOARD==PCA10001)
	#include "nrf_gpio.h"
#endif

#include <drivers/nrf_rtc.h>
#include <drivers/nrf_adc.h>
#include <drivers/nrf_pwm.h>
#include <drivers/LPComp.h>
#include <drivers/serial.h>
#include <common/storage.h>

#ifdef INDOOR_SERVICE
#include <services/IndoorLocalisationService.h>
#endif
#ifdef GENERAL_SERVICE
#include <services/GeneralService.h>
#endif
#ifdef POWER_SERVICE
#include <services/PowerService.h>
#endif

using namespace BLEpp;

/**********************************************************************************************************************
 * Main functionality
 *********************************************************************************************************************/

void welcome() {
	nrf_gpio_cfg_output(PIN_GPIO_LED);
	nrf_gpio_pin_set(PIN_GPIO_LED);
	config_uart();
	_log(INFO, "\r\n");
	uint8_t *p = (uint8_t*)malloc(1);
	LOGd("Start of heap %p", p);
	free(p);
	LOGi("Welcome at the nRF51822 code for meshing.");
	LOGi("Compilation time: %s", COMPILATION_TIME);
}

void setName(Nrf51822BluetoothStack &stack) {
	char devicename[32];
	sprintf(devicename, "%s_%s", BLUETOOTH_NAME, COMPILATION_TIME);
	stack.setDeviceName(std::string(devicename)) // max len = ble_gap_devname_max_len (31)
		// controls how device appears in gui.
		.setAppearance(BLE_APPEARANCE_GENERIC_TAG);
}

void configure(Nrf51822BluetoothStack &stack) {
	// set connection parameters.  these trade off responsiveness and range for battery life. 
	// see apple bluetooth accessory guidelines for details.
	// you could omit these; there are reasonable defaults that support medium throughput and long battery life.
	//   interval is set from 20 ms to 40 ms
	//   slave latency is 10
	//   supervision timeout multiplier is 400
	stack.setTxPowerLevel(-4)
		.setMinConnectionInterval(16)
		.setMaxConnectionInterval(32)
		.setConnectionSupervisionTimeout(400)
		.setSlaveLatency(10)
		.setAdvertisingInterval(1600)
		// advertise forever.  you may instead want to have a button press or something begin
		// advertising for a set period of time, in order to save battery.
		.setAdvertisingTimeoutSeconds(0);

}

/**
 * This must be called after the SoftDevice has started.
 */
void config_drivers() {
	// TODO: Dominik, can we connect the adc init call with the characteristic / services
	//   that actually use it? if there is no service that uses it there is no reason to
	//   allocate space for it
//	ADC::getInstance().init(PIN_AIN_ADC); // Moved to PowerService.cpp

	pwm_config_t pwm_config = PWM_DEFAULT_CONFIG;
	pwm_config.num_channels = 1;
	pwm_config.gpio_pin[0] = PIN_GPIO_LED;
	pwm_config.mode = PWM_MODE_LED_255;

	PWM::getInstance().init(&pwm_config);

#if(BOARD==PCA10001)
	nrf_gpio_cfg_output(PIN_GPIO_LED_CON);
#endif
}

int main() {
	welcome();

	// set up the bluetooth stack that controls the hardware.
	Nrf51822BluetoothStack stack;

	// set advertising parameters such as the device name and appearance.  
	setName(stack);

	// configure parameters for the Bluetooth stack
	configure(stack);

	// start up the softdevice early because we need it's functions to configure devices it ultimately controls.
	// in particular we need it to set interrupt priorities.
	stack.init();

	stack.onConnect([&](uint16_t conn_handle) {
			LOGi("onConnect...");
			// todo this signature needs to change
			//NRF51_GPIO_OUTSET = 1 << PIN_LED;
			// first stop, see https://devzone.nordicsemi.com/index.php/about-rssi-of-ble
			// be neater about it... we do not need to stop, only after a disconnect we do...
			sd_ble_gap_rssi_stop(conn_handle);
			sd_ble_gap_rssi_start(conn_handle);

#if(BOARD==PCA10001)
			nrf_gpio_pin_set(PIN_GPIO_LED_CON);
#endif
		})
		.onDisconnect([&](uint16_t conn_handle) {
			LOGi("onDisconnect...");
			//NRF51_GPIO_OUTCLR = 1 << PIN_LED;

			// of course this is not nice, but dirty! we immediately start advertising automatically after being
			// disconnected. but for now this will be the default behaviour.

#if(BOARD==PCA10001)
			nrf_gpio_pin_clear(PIN_GPIO_LED_CON);
#endif

			stack.stopScanning();

#ifdef ibeacon
			stack.startIBeacon();
#else
			stack.startAdvertising();
#endif
		});

	// Create ADC object
	// TODO: make service which enables other services and only init ADC when necessary
	//	ADC::getInstance();
	//	LPComp::getInstance();

	// Scheduler must be initialized before persistent memory
	const uint16_t max_size = 32;
	const uint16_t queue_size = 4;
	APP_SCHED_INIT(max_size, queue_size);

	// Create persistent memory object
	// TODO: make service which enables other services and only init persistent memory when necessary
	//	Storage::getInstance().init(32);

	//	RealTimeClock::getInstance();

	LOGi("Create all services");

#ifdef INDOOR_SERVICE
	// now, build up the services and characteristics.
	//Service& localizationService =
	IndoorLocalizationService::createService(stack);
#endif

#ifdef GENERAL_SERVICE
	// general services, such as internal temperature, setting names, etc.
	GeneralService& generalService = GeneralService::createService(stack);
#endif

#ifdef POWER_SERVICE
	PowerService &powerService = PowerService::createService(stack);
#endif


	// configure drivers
	config_drivers();

	// begin sending advertising packets over the air.
#ifdef IBEACON
	stack.startIBeacon();
#else
	stack.startAdvertising();
#endif
	
	LOGi("Running while loop..");

	while(1) {
		// deliver events from the bluetooth stack to the callbacks defined above.
		//		analogwrite(pin_led, 50);
//#define TEST_BOOTLOADER
//#ifdef TEST_BOOTLOADER
//		sd_power_gpregret_set(1);
//		sd_nvic_SystemReset();
//#endif
		stack.loop();

#ifdef GENERAL_SERVICE
		generalService.loop();
#endif
#ifdef POWER_SERVICE
		powerService.loop();
#endif

		app_sched_execute();

		nrf_delay_ms(50);
	}
}
