#ifndef CERTS_H
#define CERTS_H

//########################### MQTT Configuration #################################################
const char *mqtt_broker = "insert aws iot url";
const int mqtt_port = 8883;
//########################### OTA Configuration #################################################
String host = "<insert aws bucket url";  // Host => bucket-name.s3.region.amazonaws.com
int port = 80;                                                   // Non https. For HTTPS 443. As of today, HTTPS doesn't work.
String bin = "/otter_main.ino.bin";                                   // bin file name with a slash in front.

// Thing certificate and keys
const char* certificatePemCrt = R"EOF(
-----BEGIN CERTIFICATE-----
#####---------INSERT KEY HERE
-----END CERTIFICATE-----
)EOF";

const char* privatePemKey = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
#####---------INSERT KEY HERE
-----END RSA PRIVATE KEY-----
)EOF";

const char* amazonRootCA = R"EOF(
-----BEGIN CERTIFICATE-----
#####---------INSERT KEY HERE
-----END CERTIFICATE-----
)EOF";

#endif // CERTS_H