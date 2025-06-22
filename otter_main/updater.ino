// Utility to extract header value from headers
String getHeaderValue(String header, String headerName) {
  return header.substring(strlen(headerName.c_str()));
}

// OTA Logic
int execOTA() {
  Serial.println("Connecting to: " + String(host));
  if (espClient_noSSL.connect(host.c_str(), port)) {
    Serial.println("Fetching Bin: " + String(bin));
    espClient_noSSL.print(String("GET ") + bin + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Cache-Control: no-cache\r\n" + "Connection: close\r\n\r\n");
    unsigned long timeout = millis();
    while (espClient_noSSL.available() == 0) {
      if (millis() - timeout > 5000) {
        Serial.println("Client Timeout !");
        espClient_noSSL.stop();
        error_status = 5;
        return -1;
      }
    }
    while (espClient_noSSL.available()) {
      String line = espClient_noSSL.readStringUntil('\n');
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
    error_status = 5;
  }
  Serial.println("contentLength : " + String(contentLength) + ", isValidContentType : " + String(isValidContentType));
  if (contentLength && isValidContentType) {
    bool canBegin = Update.begin(contentLength);
    if (canBegin) {
      Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");
      size_t written = Update.writeStream(espClient_noSSL);
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
          return 0;
        } else {
          error_status = 6;
          Serial.println("Update not finished? Something went wrong!");
        }
      } else {
        error_status = 6;
        Serial.println("Error Occurred. Error #: " + String(Update.getError()));
      }
    } else {
      error_status = 6;
      Serial.println("Not enough space to begin OTA");
      espClient_noSSL.clear();
      return -1;
    }
  } else {
    error_status = 6;
    Serial.println("There was no content in the response");
    espClient_noSSL.clear();
    return -1;
  }
  return 0;
}