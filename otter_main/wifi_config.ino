
//########################### Start WiFi Manager ###################################################
int start_wifi_manager(){
  WiFiManager wm;
  bool res = wm.autoConnect("Otter", "");
  if (!res) {
    Serial.println("Failed to connect");
    error_status = 3;
    return -1;
  } else {
    Serial.println("connected...yeey :)");
    return 1;
  }
}

void reset_wifi_config(){
  WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}
