#include <Smooth.h>
#define SMOOTHED_SAMPLE_SIZE 12

Smooth movement_avg(SMOOTHED_SAMPLE_SIZE);
float stable_movement_region;
float time_not_moving = 0.0;
bool breathing_is_on = false;
bool light_is_on = false;

void breathing_motor_starter(int pwm) {
  digitalWrite(controller_sleep, HIGH);
  if (breathing_is_on == false) {
    for (int i = 0; i <= pwm; i += 5) {
      //analogWrite(motor_2_pin, i);
      ledcWrite(motor_2_pin, i);
      delay(10);
    }
    breathing_is_on = true;
  }
}

void breathing_motor_stopper(int pwm) {
  if (breathing_is_on == true) {
    for (int i = pwm; i >= 0; i -= 5) {
      //analogWrite(motor_2_pin, i);
      ledcWrite(motor_2_pin, i);
      delay(10);
    }
    breathing_is_on = false;
  }
}

void breathing_light_enable(int pwm) {
  digitalWrite(controller_sleep, HIGH);
  if (light_is_on == false) {
    for (int i = 0; i <= pwm; i += 1) {
      analogWrite(light_1_pin, i);
      delay(10);
    }
    light_is_on = true;
  }
}

void breathing_light_disable(int pwm) {
  if (light_is_on == true) {
    for (int i = pwm; i >= 0; i -= 1) {
      analogWrite(light_1_pin, i);
      delay(10);
    }
    light_is_on = false;
  }
}

bool check_for_inactivity(float x, float y, float z) {
  movement_avg.add(x + y + z);
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