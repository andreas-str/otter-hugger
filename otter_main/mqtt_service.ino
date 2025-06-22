unsigned long previousMillis_KA = 0;
bool paired_device_registered = false;
char device_ID[13];
int get_initial_config_index = 0;

void get_dev_id(){
  uint64_t mac = ESP.getEfuseMac();
  snprintf(device_ID, sizeof(device_ID), "%012llX", mac & 0xFFFFFFFFFFFFULL);
}
void mqtt_connect() {
  while (!mqtt_client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (mqtt_client.connect(device_ID)) {
      Serial.println("connected");
      break;
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
int connect_and_request_settings() {
  if (mqtt_client.connected() == true) {
    snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "get_settings");
    snprintf (mqtt_msg, sizeof(mqtt_msg), "%d.%d", 1, sw_version);
    mqtt_client.publish(mqtt_topic, mqtt_msg);
    return 0;
  }
  return -1;
}
void send_alive_signal(){
  unsigned long currentMillis_KA = millis();
  if (currentMillis_KA - previousMillis_KA >= keep_alive_interval) {
    previousMillis_KA = currentMillis_KA;
    if (mqtt_client.connected()) {
      is_alive = !is_alive;
      snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "is_alive");
      snprintf (mqtt_msg, sizeof(mqtt_msg), "%ld", is_alive);
      mqtt_client.publish(mqtt_topic, mqtt_msg);
    }
  }
}
int send_status(int status){
  if (mqtt_client.connected()) {
    snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "status");
    snprintf (mqtt_msg, sizeof(mqtt_msg), "%ld", status);
    mqtt_client.publish(mqtt_topic, mqtt_msg);
    device_status = status;
    return 0;
  }
  return -1;
}
void send_low_battery(){
  if (mqtt_client.connected() && low_battery == 0) {
    snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "low_battery");
    snprintf (mqtt_msg, sizeof(mqtt_msg), "%ld", 1);
    mqtt_client.publish(mqtt_topic, mqtt_msg);
    low_battery = 1;
  }
}
void send_error(int error){
  if (mqtt_client.connected()) {
    snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "error");
    snprintf (mqtt_msg, sizeof(mqtt_msg), "%ld", error);
    mqtt_client.publish(mqtt_topic, mqtt_msg);
  }
}
void advertise_id(){
  if (mqtt_client.connected()) {
    mqtt_client.publish("otter_advertising/new/", device_ID);
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  int firstSlash = t.indexOf('/');
  int secondSlash = t.indexOf('/', firstSlash + 1);
  String deviceId_str = t.substring(0, firstSlash); 
  String topic_str = t.substring(firstSlash + 1, secondSlash); 

  Serial.println("Device ID: " + deviceId_str);
  Serial.println("Topic: " + topic_str);
  Serial.println("Payload: " + message);

  if (topic_str == "paired_id") {
    snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", message.c_str(), "status");
    mqtt_client.subscribe(mqtt_topic);
    /////////////////////////////////////////////////////////////////////CHECK IF ID IS GOOD
    paired_device_id = message;
    paired_device_registered = true;
    get_initial_config_index++;
  } 
  else if(topic_str == "status" && deviceId_str == paired_device_id){
    paired_device_status = message.toInt();
  } 
  else if(topic_str == "OTA_update"){
    if(new_OTA == false && message.toInt() == 1){
    new_OTA = true;
    }
  }
  else if(topic_str == "battery_warn"){
    battery_warning_level = message.toInt();
    get_initial_config_index++;
  }
  else if(topic_str == "empty_battery_reset"){
    if (message.toInt() == 1){
      reset_battery_status = true;
    }
  }
  else if(topic_str == "heartrate"){
    heartrate_ms = message.toInt();
    get_initial_config_index++;
  }
  else if(topic_str == "heartbeat_mode"){
    heartbeat_mode = message.toInt();
    get_initial_config_index++;
  }
  else if(topic_str == "belly_light"){
    belly_light_status = message.toInt();
    get_initial_config_index++;
  }
  else if(topic_str == "feel_option"){
    feel_option_status = message.toInt();
    get_initial_config_index++;
  }
  else if(topic_str == "sleep_after_time"){
    sleep_after_time = message.toInt();
    sleep_after_time = sleep_after_time * 1200.00;
    get_initial_config_index++;
  }
  else
  {
    Serial.println("How did you end up here??");
  }
}

bool pairing_is_done(){
  if (get_initial_config_index == 7 && paired_device_registered == true){
    return true;
  }
  return false;
}

void setup_mqtt_callbacks(){
  snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "OTA_update");
	mqtt_client.subscribe(mqtt_topic);
  snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "paired_id");
  mqtt_client.subscribe(mqtt_topic);
  snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "battery_warn");
  mqtt_client.subscribe(mqtt_topic);
  snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "empty_battery_reset");
  mqtt_client.subscribe(mqtt_topic);
  snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "heartrate");
  mqtt_client.subscribe(mqtt_topic);
  snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "heartbeat_mode");
  mqtt_client.subscribe(mqtt_topic);
  snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "belly_light");
  mqtt_client.subscribe(mqtt_topic);
  snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "feel_option");
  mqtt_client.subscribe(mqtt_topic);
  snprintf(mqtt_topic, sizeof(mqtt_topic), "%s/%s", device_ID, "sleep_after_time");
  mqtt_client.subscribe(mqtt_topic);
}