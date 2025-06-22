//General variables
int motor_pwm_limit = 190;
int LED_pwm_limit = 50;
uint32_t total_uptime = 0;
unsigned long keep_alive_interval = 60000;

// OTA configuration
long contentLength = 0;
bool isValidContentType = false;

// MQTT configuration
char mqtt_topic[30];
char mqtt_msg[30];


int get_device_status(){
  return device_status;
}

