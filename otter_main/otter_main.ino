#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include "AdafruitIO_WiFi.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include "Adafruit_DRV2605.h"
#include "driver/rtc_io.h"
#include <EasyButton.h>
#include "EEPROM.h"
#include <Uptime.h>  

//########################### Initialize Wakeup setup #################################################
#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)  // 2 ^ GPIO_NUMBER in hex
#define USE_EXT0_WAKEUP 1                        // 1 = EXT0 wakeup, 0 = EXT1 wakeup
#define WAKEUP_GPIO GPIO_NUM_16                  // Only RTC IO are allowed - ESP32 Pin example
#define uS_TO_S_FACTOR 1000000ULL                // Conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP_FIRST_TIME 600             // Time ESP32 will go to sleep for 10 minutes
#define TIME_TO_SLEEP_LATER 3600                 // Time ESP32 will go to sleep for 60 minutes
RTC_DATA_ATTR int bootCount = 0;
//########################### Initialize OTA Updater #################################################
// S3 Bucket Config
// Variables to validate
// response from S3
long contentLength = 0;
bool isValidContentType = false;
String host = "andreasandlinaotter.s3.us-east-2.amazonaws.com";  // Host => bucket-name.s3.region.amazonaws.com
int port = 80;                                                   // Non https. For HTTPS 443. As of today, HTTPS doesn't work.
String bin = "/otter.ino.bin";                                   // bin file name with a slash in front.
NetworkClient client;

// Utility to extract header value from headers
String getHeaderValue(String header, String headerName) {
  return header.substring(strlen(headerName.c_str()));
}
//########################### Initialize Peripherals #################################################
Adafruit_DRV2605 vibrator;
Adafruit_ADXL345_Unified accelerometer = Adafruit_ADXL345_Unified(12345);
EEPROMClass UPTIME("eeprom0");
Uptime uptime;


//########################### Initialize General IO ##################################################
const int motor_1_pin = 8;
const int motor_2_pin = 9;
const int light_1_pin = 18;
const int light_2_pin = 17;
const int controller_sleep = 5;
const int belly_button_pin = 16;
const int wifi_reset_button_pin = 36;
EasyButton belly_button(belly_button_pin);
EasyButton wifi_reset_button(wifi_reset_button_pin);


//########################### Global Variables #######################################################
unsigned long previousMillis = 0;
sensors_event_t event;
bool is_inactive = false;

int heartrate_ms = 0;
bool heart_mode_quiet = false;
int sleep_after_time = 0;
bool belly_movement = false;
bool heartbeat_movement = true;
bool belly_light_status = false;
uint32_t total_uptime = 0;
bool battery_msg = false;
int battery_warning_level = 1800;


//########################### State Machine - States #################################################
enum STATEMACHINE_STATE { STARTUP,
                          CONFIGURE,
                          CONNECT,
                          IDLE,
                          SYNC_MODE,
                          SLEEP_MODE,
                          SLEEP_MODE_TIMER,
                          UPDATE_MODE };
STATEMACHINE_STATE CURRENT_STATE = STARTUP;
STATEMACHINE_STATE NEXT_STATE;



//####################################################################################################
//################################### Heartbeat Looper (no block) ####################################
//####################################################################################################
void heartbeat_loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= heartrate_ms) {
    previousMillis = currentMillis;
    if (heart_mode_quiet == true) {
      vibrator.setWaveform(0, 46);  // play effect
      vibrator.setWaveform(1, 0);   // end waveform
      vibrator.go();                // play the effect!
    } else {
      vibrator.setWaveform(0, 37);  // play effect
      vibrator.setWaveform(1, 0);   // end waveform
      vibrator.go();                // play the effect!
    }
  }
}

int get_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Wakeup caused by external signal using RTC_IO");
      return 0;
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Wakeup caused by external signal using RTC_CNTL");
      return 0;
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Wakeup caused by timer");
      return 1;
      break;
    default:
      return 0;
      break;
  }
}

// OTA Logic
void execOTA() {
  Serial.println("Connecting to: " + String(host));
  if (client.connect(host.c_str(), port)) {
    Serial.println("Fetching Bin: " + String(bin));
    client.print(String("GET ") + bin + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Cache-Control: no-cache\r\n" + "Connection: close\r\n\r\n");
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        Serial.println("Client Timeout !");
        client.stop();
        return;
      }
    }
    while (client.available()) {
      String line = client.readStringUntil('\n');
      line.trim();
      if (!line.length()) {
        break;  // and get the OTA started
      }
      if (line.startsWith("HTTP/1.1")) {
        if (line.indexOf("200") < 0) {
          Serial.println("Got a non 200 status code from server. Exiting OTA Update.");
          break;
        }
      }
      if (line.startsWith("Content-Length: ")) {
        contentLength = atol((getHeaderValue(line, "Content-Length: ")).c_str());
        Serial.println("Got " + String(contentLength) + " bytes from server");
      }
      if (line.startsWith("Content-Type: ")) {
        String contentType = getHeaderValue(line, "Content-Type: ");
        Serial.println("Got " + contentType + " payload.");
        if (contentType == "application/octet-stream") {
          isValidContentType = true;
        }
      }
    }
  } else {
    Serial.println("Connection to " + String(host) + " failed. Please check your setup");
  }
  Serial.println("contentLength : " + String(contentLength) + ", isValidContentType : " + String(isValidContentType));
  if (contentLength && isValidContentType) {
    bool canBegin = Update.begin(contentLength);
    if (canBegin) {
      Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");
      size_t written = Update.writeStream(client);
      if (written == contentLength) {
        Serial.println("Written : " + String(written) + " successfully");
      } else {
        Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
      }
      if (Update.end()) {
        Serial.println("OTA done!");
        if (Update.isFinished()) {
          Serial.println("Update successfully completed. Rebooting.");
          bootCount = 0;
          check_if_update_is_done();
        } else {
          Serial.println("Update not finished? Something went wrong!");
        }
      } else {
        Serial.println("Error Occurred. Error #: " + String(Update.getError()));
      }
    } else {
      Serial.println("Not enough space to begin OTA");
      client.clear();
    }
  } else {
    Serial.println("There was no content in the response");
    client.clear();
  }
}
void check_if_update_is_done() {
  hug_status_mine->save(3);
  while (status_mine != 3) {
    delay(1);
    io.run();
  }
  Serial.println("update done message sent");
  ESP.restart();
}
void check_if_shudown_notification_is_done(int counter) {
  if (counter > 1) {
    shutdown_reminder->save(shutdown_reminder_signal + 2);
  } else {
    shutdown_reminder->save(shutdown_reminder_signal);
  }
  while (shutdown_reminder_signal != 0) {
    delay(1);
    io.run();
  }
  Serial.println("shutdown notification done");
}
void belly_pressed_function() {
  NEXT_STATE = STOP_EVERYTHING;
}
void wifi_reset_function() {
  UPTIME.writeULong(0, 0);
  UPTIME.commit();
  delay(100);
  //WiFiManager wm;
  //wm.resetSettings();
  ESP.restart();
}
bool is_battery_empty() {
  uptime.calculateUptime();
  int current_uptime = total_uptime + uptime.getMinutes();
  if (current_uptime > battery_warning_level) {
    return true;
  } else {
    return false;
  }
}

//###################################################################################################################
//###################################################################################################################
//########################################################## SETUP ##################################################
//###################################################################################################################
//###################################################################################################################
void setup() {
  //########################### Setup all GPIO Pins ##################################################
  Serial.begin(115200);
  rtc_gpio_deinit(WAKEUP_GPIO);
  bootCount++;
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(motor_1_pin, OUTPUT);
  digitalWrite(motor_1_pin, LOW);
  ledcAttach(motor_2_pin, 100000, 8);
  //pinMode(motor_2_pin, OUTPUT);
  pinMode(light_1_pin, OUTPUT);
  pinMode(light_2_pin, OUTPUT);
  digitalWrite(light_2_pin, LOW);
  pinMode(controller_sleep, OUTPUT);
  belly_button.begin();
  wifi_reset_button.begin();
  belly_button.onPressed(belly_pressed_function);
  wifi_reset_button.onPressed(wifi_reset_function);
  if (get_wakeup_reason() == 0) {
    digitalWrite(LED_BUILTIN, HIGH);
    breathing_light_enable(20);  //Enable the LED
  }
  //########################### Start WiFi Manager ###################################################
  WiFiManager wm;
  bool res = wm.autoConnect("Otter", "");
  if (!res) {
    Serial.println("Failed to connect");
    ESP.restart();
  } else {
    Serial.println("connected...yeey :)");
  }

  //########################### Start EEPROM ##########################################################
  if (!UPTIME.begin(0x500)) {
    Serial.println("Failed to initialize UPTIME EEPROM");
  }
  total_uptime = UPTIME.readULong(0);
  Serial.print("uptime from EEPROM: ");
  Serial.println(total_uptime);

  //########################### Start Vibrator IO ####################################################
  while (!vibrator.begin(&Wire1)) {
    Serial.println("Could not find DRV2605, trying again");
    delay(100);
  }
  vibrator.selectLibrary(1);
  vibrator.setMode(DRV2605_MODE_INTTRIG);

  //########################### Start Accelerometer ##################################################
  while (!accelerometer.begin()) {
    Serial.println("Ooops...Could not find ADXL345, trying again");
    delay(100);
  }
  accelerometer.setRange(ADXL345_RANGE_4_G);

  //########################### Connect to Adafruit IO ###############################################
  Serial.printf("Connecting to Adafruit IO with User: %s\n", IO_USERNAME);
  io.connect();
  feel->onMessage(handleMessage);
  belly_light->onMessage(handleMessage);
  hug_status_mine->onMessage(handleMessage);
  hug_status_them->onMessage(handleMessage);
  heart_rate->onMessage(handleMessage);
  heart_mode->onMessage(handleMessage);
  sleep_after->onMessage(handleMessage);
  shutdown_reminder->onMessage(handleMessage);
  while ((io.status() < AIO_CONNECTED)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("Connected to Adafruit IO.");
  feel->get();
  belly_light->get();
  heart_rate->get();
  heart_mode->get();
  sleep_after->get();
  hug_status_them->get();
  hug_status_mine->get();
  digitalWrite(LED_BUILTIN, LOW);
}
//###################################################################################################################
//###################################################################################################################
//########################################################## LOOP ###################################################
//###################################################################################################################
//###################################################################################################################
void loop() {
  //########################### State Machine Controller ###############################################
  if (NEXT_STATE != CURRENT_STATE) {
    CURRENT_STATE = NEXT_STATE;
    Serial.println(CURRENT_STATE);
  }
  belly_button.read();
  wifi_reset_button.read();
  //########################### Main State Machine #####################################################
  switch (CURRENT_STATE) {
    //########################### Startup ##############################################################
    //########################### Get Latest Message of this otter status #############################
    case STARTUP:
      if (start_checker == 7) {
        if (get_wakeup_reason() == 1) {
          NEXT_STATE = TIMER_SHUTDOWN;
        }
        if (get_wakeup_reason() == 0) {
          delay(5);
          if (status_mine == 0) {
            status_mine = 1;
            NEXT_STATE = SEND_SIGNAL;
          } else {
            NEXT_STATE = SEND_SIGNAL;
          }
        }
      }
      break;
    //########################### Send Signal ##########################################################
    //########################### Send out that we are active ##########################################
    case SEND_SIGNAL:
      hug_status_mine->save(1);
      status_mine = 1;
      NEXT_STATE = CHECK_SIGNAL;
      break;
    //########################### Check Signal ########################################################
    //########################### Check if other otter is online ######################################
    case CHECK_SIGNAL:
      accelerometer.getEvent(&event);
      is_inactive = check_for_inactivity(event.acceleration.x, event.acceleration.y, event.acceleration.z);
      if (status_them == 1) {
        breathing_light_disable(20);
        NEXT_STATE = RUN_SYNCED;
      }
      if (is_inactive == true) {
        NEXT_STATE = STOP_EVERYTHING;
      }
      if (status_mine == 2) {
        NEXT_STATE = UPDATE_SW;
      }
      if(is_battery_empty() == true && battery_msg == false){
        battery_msg = true;
        hug_status_mine->save(4);
      }
      break;
    //########################### Run Synced ###########################################################
    //########################### Start the hearbeat and run in sync ###################################
    case RUN_SYNCED:
      accelerometer.getEvent(&event);
      is_inactive = check_for_inactivity(event.acceleration.x, event.acceleration.y, event.acceleration.z);
      if (status_them == 0) {
        NEXT_STATE = STOP_EVERYTHING;
      }
      if (is_inactive == true) {
        NEXT_STATE = STOP_EVERYTHING;
      }
      if (status_mine == 0) {
        NEXT_STATE = STOP_EVERYTHING;
      }
      if (belly_light_status) {
        breathing_light_enable(225);
      } else {
        breathing_light_disable(225);
      }
      if (belly_movement) {
        breathing_motor_starter(255);
      } else {
        breathing_motor_stopper(255);
      }
      if (heartbeat_movement) {
        heartbeat_loop();
      }
      if(is_battery_empty() == true && battery_msg == false){
        battery_msg = true;
        hug_status_mine->save(4);
      }
      break;
    //########################### Stop Everything ######################################################
    //########################### Stop all the things ##################################################
    case STOP_EVERYTHING:
      hug_status_mine->save(0);
      uptime.calculateUptime();
      total_uptime = total_uptime + uptime.getMinutes();
      UPTIME.writeULong(0, total_uptime);
      UPTIME.commit();
      Serial.print("Total uptime is: ");
      Serial.println(total_uptime);
      if (belly_light_status) {
        breathing_light_disable(225);
      }
      if (belly_movement) {
        breathing_motor_stopper(255);
      }
      digitalWrite(controller_sleep, LOW);
      NEXT_STATE = GO_TO_SLEEP;
      break;
    //########################### Go to sleep ##########################################################
    //########################### Simple things here ###################################################
    case GO_TO_SLEEP:
      esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0);  //1 = High, 0 = Low
      if (bootCount >= 1) {
        bootCount = 0;
      }
      esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_FIRST_TIME * uS_TO_S_FACTOR);
      rtc_gpio_pullup_en(WAKEUP_GPIO);
      rtc_gpio_pulldown_dis(WAKEUP_GPIO);
      delay(500);
      esp_deep_sleep_start();
      break;
    //########################### Go to sleep after a timer reboot #####################################
    //########################### only send a message out and go back to sleep #########################
    case TIMER_SHUTDOWN:
      Serial.print("timer shutdown state, counter is at:");
      Serial.println(bootCount);
      check_if_shudown_notification_is_done(bootCount);
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
    //############################## Update the software ###############################################
    //##################################################################################################
    case UPDATE_SW:
      hug_status_mine->save(2);
      execOTA();
      break;
  }
  //########################### Runs Adafruit IO ######################################################
  io.run();
}
