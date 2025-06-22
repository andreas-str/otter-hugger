#include <WiFiManager.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <EasyButton.h>
#include <Wire.h>
#include <Uptime.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include "Adafruit_DRV2605.h"
#include "driver/rtc_io.h"
#include <Preferences.h>
#include "certs.h"

int sw_version = 2;

//########################### GPIO Definitions #################################################
const int motor_1_pin = 8;
const int motor_2_pin = 9;
const int light_1_pin = 18;
const int light_2_pin = 17;
const int controller_sleep = 5;
const int belly_button_pin = 16;
const int back_button_pin = 36;

//########################### MQTT Configuration #################################################
const char *mqtt_broker = "al8bkn1jra0r7-ats.iot.us-east-2.amazonaws.com";
const int mqtt_port = 8883;
// MQTT values variables
int is_alive = 0;
int device_status = 0;
String paired_device_id;
int paired_device_status = 0;
int battery_warning_level = 1800;
int low_battery = 0;
bool reset_battery_status = false;
int heartrate_ms = 0;
int heartbeat_mode = 0;
int belly_light_status = 0;
int feel_option_status = 0;
int sleep_after_time = 20000;
bool new_OTA = false;
//########################### Initialize Peripherals #################################################
RTC_DATA_ATTR int bootCount = 0;
EasyButton belly_button(belly_button_pin);
EasyButton back_button(back_button_pin);
Adafruit_DRV2605 vibrator;
Adafruit_ADXL345_Unified accelerometer = Adafruit_ADXL345_Unified(12345);
Preferences preferences;
Uptime uptime;
WiFiClient espClient_noSSL;
WiFiClientSecure espClient = WiFiClientSecure();
PubSubClient mqtt_client(espClient);
void mqtt_callback(char *topic, byte *payload, unsigned int length);

//########################### Initialize Wakeup setup #################################################
#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)  // 2 ^ GPIO_NUMBER in hex
#define USE_EXT0_WAKEUP 1                        // 1 = EXT0 wakeup, 0 = EXT1 wakeup
#define WAKEUP_GPIO GPIO_NUM_16                  // Only RTC IO are allowed - ESP32 Pin example
#define uS_TO_S_FACTOR 1000000ULL                // Conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP_FIRST_TIME 600             // Time ESP32 will go to sleep for 10 minutes
#define TIME_TO_SLEEP_LATER 3600                 // Time ESP32 will go to sleep for 60 minutes

//########################### Global Variables #######################################################
int return_status = 0;
int error_status = 0;  //1 = Vib driver error, 2 = accelerometer error, 3 = wifi connection error, 4 = mqtt error, 5 = update aws error, 6 = update esp32 error

//########################### State Machine - States #################################################
enum STATEMACHINE_STATE { STARTUP,
                          CONNECT,
                          PAIRING,
                          IDLE,
                          SYNC_MODE,
                          START_SLEEP_MODE,
                          SLEEP_MODE,
                          SLEEP_MODE_TIMER,
                          UPDATE_MODE,
                          ERROR_MODE,
                          REBOOT_MODE };
STATEMACHINE_STATE CURRENT_STATE = STARTUP;
STATEMACHINE_STATE NEXT_STATE;

enum MQTT_STATUS { CONNECTED = 0,
                   PAIRED = 1,
                   SYNCED = 2,
                   NORMAL_SLEEP = 3,
                   TIMER_SLEEP = 4,
                   UPDATING = 5,
                   UPDATE_DONE = 6,
                   ERROR = 7 };
//MQTT_STATUS STATUS = CONNECTED;

//###################################################################################################################
//########################################################## SETUP ##################################################
//###################################################################################################################
void setup() {
  //########################### Setup all GPIO Pins ##################################################
  Serial.begin(115200);
  run_pin_initialization();
  if (get_wakeup_reason() == 0) {
    breathing_light_controller(true, 128);
  }
  bootCount++;
  belly_button.begin();
  back_button.begin();
  belly_button.onPressed(belly_button_pressed_function);
  belly_button.onPressedFor(2000, belly_button_pressed_long_function);
  back_button.onPressed(back_button_pressed_function);
  run_peripheral_initilization();
  get_dev_id();
  if (start_wifi_manager() == -1) {
    ESP.restart();
  }
  espClient.setCACert(amazonRootCA);
  espClient.setCertificate(certificatePemCrt);
  espClient.setPrivateKey(privatePemKey);
  mqtt_client.setServer(mqtt_broker, mqtt_port);
  mqtt_client.setCallback(mqtt_callback);
  if (!mqtt_client.connected()) {
    mqtt_connect();
  }
}

//###################################################################################################################
//########################################################## LOOP ###################################################
//###################################################################################################################
void loop() {
  //########################### State Machine Controller ###############################################
  if (NEXT_STATE != CURRENT_STATE) {
    CURRENT_STATE = NEXT_STATE;
    Serial.println(CURRENT_STATE);
  }
  if (mqtt_client.connected()) {
    mqtt_client.loop();
  }
  belly_button.read();
  back_button.read();
  if (CURRENT_STATE > STARTUP) {
    send_alive_signal();
  }
  if (is_battery_empty() == true) {
    send_low_battery();
  }
  if (reset_battery_status == true) {
    reset_uptime();
    reset_battery_status = false;
  }
  if (stillness_detector() == true) {
    NEXT_STATE = START_SLEEP_MODE;
  }
  if (new_OTA == true) {
    new_OTA = false;
    return_status = send_status(UPDATING);
    if (return_status == -1) {
      NEXT_STATE = ERROR_MODE;
    } else {
      NEXT_STATE = UPDATE_MODE;
    }
  }
  //########################### Main State Machine #####################################################
  switch (CURRENT_STATE) {
    case STARTUP:
      setup_mqtt_callbacks();
      if (get_wakeup_reason() == 1) {
        NEXT_STATE = START_SLEEP_MODE;
      } else {
        advertise_id();
        NEXT_STATE = CONNECT;
      }
      break;

    case CONNECT:
      return_status = connect_and_request_settings();
      if (return_status == 0) {
        return_status = send_status(CONNECTED);
        if (return_status == -1) {
          NEXT_STATE = ERROR_MODE;
        } else {
          NEXT_STATE = PAIRING;
        }
      }
      break;

    case PAIRING:
      if (pairing_is_done() == true) {
        return_status = send_status(PAIRED);
        if (return_status == -1) {
          NEXT_STATE = ERROR_MODE;
        } else {
          NEXT_STATE = IDLE;
        }
      }
      break;

    case IDLE:
      if (paired_device_status == PAIRED || paired_device_status == SYNCED) {
        return_status = send_status(SYNCED);
        if (return_status == -1) {
          NEXT_STATE = ERROR_MODE;
        } else {
          NEXT_STATE = SYNC_MODE;
        }
      }
      if (paired_device_status == NORMAL_SLEEP) {
        NEXT_STATE = START_SLEEP_MODE;
      }
      break;

    case SYNC_MODE:
      if (belly_light_status == 1) {
        breathing_light_controller(true, 255);
      } else {
        breathing_light_controller(false, 255);
      }
      if (feel_option_status == 1) {
        breathing_motor_controller(true, 255);
      } else {
        breathing_motor_controller(false, 255);
        heartbeat_controller();
      }
      if (paired_device_status >= NORMAL_SLEEP) {
        NEXT_STATE = START_SLEEP_MODE;
      }
      break;
    case START_SLEEP_MODE:
      if (get_wakeup_reason() == 0) {
        return_status = send_status(NORMAL_SLEEP);
        if (return_status == -1) {
          NEXT_STATE = ERROR_MODE;
        } else {
          NEXT_STATE = SLEEP_MODE;
        }
      } else {
        return_status = send_status(TIMER_SLEEP);
        if (return_status == -1) {
          NEXT_STATE = ERROR_MODE;
        } else {
          NEXT_STATE = SLEEP_MODE_TIMER;
        }
      }
      breathing_motor_controller(false, 255);
      breathing_light_controller(false, 255);
      break;

    case SLEEP_MODE:
      delay(300);
      esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0);  //1 = High, 0 = Low
      if (bootCount >= 1) {
        bootCount = 0;
      }
      esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_FIRST_TIME * uS_TO_S_FACTOR);
      rtc_gpio_pullup_en(WAKEUP_GPIO);
      rtc_gpio_pulldown_dis(WAKEUP_GPIO);
      esp_deep_sleep_start();
      break;

    case SLEEP_MODE_TIMER:
      delay(300);
      esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0);  //1 = High, 0 = Low
      if (bootCount >= 1) {
        esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_LATER * uS_TO_S_FACTOR);
      } else {
        esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_FIRST_TIME * uS_TO_S_FACTOR);
      }
      rtc_gpio_pullup_en(WAKEUP_GPIO);
      rtc_gpio_pulldown_dis(WAKEUP_GPIO);
      esp_deep_sleep_start();
      break;

    case UPDATE_MODE:
      breathing_motor_controller(false, 255);
      breathing_light_controller(false, 255);
      digitalWrite(LED_BUILTIN, HIGH);
      return_status = execOTA();
      if (return_status == 0) {
        return_status = send_status(UPDATE_DONE);
        if (return_status == -1) {
          NEXT_STATE = ERROR_MODE;
        }
        digitalWrite(LED_BUILTIN, LOW);
        NEXT_STATE = REBOOT_MODE;
      } else {
        NEXT_STATE = ERROR_MODE;
      }
      break;
    case ERROR_MODE:
      return_status = send_status(ERROR);
      if (return_status == -1) {
        NEXT_STATE = ERROR_MODE;
      }
      NEXT_STATE = ERROR_MODE;
      break;
    case REBOOT_MODE:
      delay(300);
      ESP.restart();
      break;
  }
}
