#include <Smooth.h>

//########################### Setup Peripherals Variables ############################################
#define SMOOTHED_SAMPLE_SIZE 12
Smooth movement_avg(SMOOTHED_SAMPLE_SIZE);
sensors_event_t acc_event;
unsigned long previousMillis = 0;
unsigned long previousMillis_Acc = 0;
float stable_movement_region;
float time_not_moving = 0.0;
uint8_t general_dim_index = 0;
bool breathing_is_on = false;
bool light_is_on = false;
bool belly_button_pressed_long = false;
bool back_button_pressed = false;

//########################### Initialize all the GPIO Pins ############################################
void run_pin_initialization() {
  rtc_gpio_deinit(WAKEUP_GPIO);
  bootCount++;
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(motor_1_pin, OUTPUT);
  digitalWrite(motor_1_pin, LOW);
  pinMode(motor_2_pin, OUTPUT);
  pinMode(light_1_pin, OUTPUT);
  pinMode(light_2_pin, OUTPUT);
  digitalWrite(light_2_pin, LOW);
  pinMode(controller_sleep, OUTPUT);
  digitalWrite(controller_sleep, HIGH);
}
//########################### Initialize all the Peripherals ############################################
int run_peripheral_initilization() {
  
  //start Preferences and read uptime
  preferences.begin("otter_mem", false);
  total_uptime = preferences.getUInt("uptime", 0);
  Serial.print("uptime from Preferences: ");
  Serial.println(total_uptime);

  //Start Vibrator IO
  unsigned long timeout = millis();
  while (!vibrator.begin(&Wire1)) {
    Serial.println("Could not find DRV2605, trying again");
    delay(100);
    if (millis() - timeout > 2000) {
      error_status = 1;
      break;
      return -1;
    }
  }
  vibrator.selectLibrary(1);
  vibrator.setMode(DRV2605_MODE_INTTRIG);

  //Start Accelerometer
  timeout = millis();
  while (!accelerometer.begin()) {
    Serial.println("Ooops...Could not find ADXL345, trying again");
    delay(100);
    if (millis() - timeout > 2000) {
      error_status = 2;
      return -1;
    }
  }
  accelerometer.setRange(ADXL345_RANGE_4_G);
  return 0;
}

//########################### Belly button pressed function ###########################################
void belly_button_pressed_function() {
  NEXT_STATE = START_SLEEP_MODE;
}
void belly_button_pressed_long_function() {
  belly_button_pressed_long = true;
}

//########################### Back button pressed function ###########################################
void back_button_pressed_function() {
  reset_wifi_config();
}

//########################### Control the breathing motor #############################################
int breathing_motor_controller(bool control_status, int pwm_setting) {
  // Return codes: -1 = Bad input, 0 = operation Done, 1 = Operation in progress
  if (pwm_setting < motor_pwm_limit || pwm_setting > 255) {
    return -1;
  }
  if (control_status == true) {
    if (breathing_is_on == false) {
      breathing_is_on = true;
      analogWrite(motor_2_pin, pwm_setting);
      return 1;
    }
  } else {
    if (breathing_is_on == true) {
      analogWrite(motor_2_pin, 0);
      breathing_is_on = false;
      return 0;
    }
  }
}

//########################### Control the breathing light ###########################################
int breathing_light_controller(bool control_status, int pwm_setting) {
  // Return codes: -1 = Bad input, 0 = operation Done, 1 = Operation in progress
  if (pwm_setting < LED_pwm_limit || pwm_setting > 255) {
    return -1;
  }
  if (control_status == true) {
    if (light_is_on == false) {
      analogWrite(light_1_pin, pwm_setting);
      light_is_on = true;
      return 1;
    }
  } else {
    if (light_is_on == true) {
      analogWrite(light_1_pin, 0);
      light_is_on = false;
      return 1;
    }
  }
}

//################################### Control the heartbeat ########################################
void heartbeat_controller() {
  if (heartrate_ms > 0) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= heartrate_ms) {
      previousMillis = currentMillis;
      if (heartbeat_mode == 0) {
        vibrator.setWaveform(0, 46);  // select effect
        vibrator.setWaveform(1, 0);   // set ending
        vibrator.go();                // play effect
      }
      if (heartbeat_mode == 1) {
        vibrator.setWaveform(0, 37);
        vibrator.setWaveform(1, 0);
        vibrator.go();
      }
    }
  }
}

//###################### Check accelerometer to see if there is movement ############################
bool stillness_detector() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis_Acc >= 50) {
    previousMillis_Acc = currentMillis;
    // returns True if innactive, False if movement has been detected
    accelerometer.getEvent(&acc_event);
    movement_avg.add(acc_event.acceleration.x + acc_event.acceleration.y + acc_event.acceleration.z);
    if (int(movement_avg.get_avg()) > int(stable_movement_region) || int(movement_avg.get_avg()) < int(stable_movement_region)) {
      stable_movement_region = movement_avg.get_avg();
      time_not_moving = 0.0;
    }
    if (time_not_moving > sleep_after_time) {
      return true;
    }
    time_not_moving++;
    //Serial.println(time_not_moving);
    return false;
  }
  return false;
}
//################################### Check wakeup reason function ###################################
int get_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      //Serial.println("Wakeup caused by external signal using RTC_IO");
      return 0;
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      //Serial.println("Wakeup caused by timer");
      return 1;
      break;
    default:
      return 0;
      break;
  }
}
//################################### Battery checker function ######################################
bool is_battery_empty() {
  uptime.calculateUptime();
  int current_uptime = total_uptime + uptime.getMinutes();
  if (current_uptime > battery_warning_level) {
    return true;
  } else {
    return false;
  }
}
//########################### breathing light error controller ######################################
int error_light_controller(int error) {
  analogWrite(light_1_pin, 255);
}
//################################### Save latest uptime value ######################################
void save_uptime() {
  uptime.calculateUptime();
  int current_uptime = total_uptime + uptime.getMinutes();
  preferences.putUInt("uptime", current_uptime);
  preferences.end();
}
//################################### Reset uptime value to 0 #######################################
void reset_uptime() {
  preferences.putUInt("uptime", 0);
  preferences.end();
}