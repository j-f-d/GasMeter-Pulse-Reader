#include <Arduino.h>
#ifdef ARDUINO_ARCH_ESP8266
#include <ESP8266WiFi.h>
#else
#error "What is the correct header for you platform?"
#endif
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Bounce2.h>
#include <secrets.h>

#ifndef DIM
#define DIM(x) (sizeof(x)/sizeof(x[0]))
#endif

#define USE_DHCP
#define SERIAL_LOGGING
// Enabling MQTT_SUBSCRIBE allows the current meter count to be set by changing an MQTT topic,
// but isn't really desirable because it results in big jumps in the readings.
//#define MQTT_SUBSCRIBE

// Enter your passwords in the secrets.h file. You can use the secrets.h.template file for this purpose.
const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;
#ifndef USE_DHCP
IPAddress ip(192, 168, 188, 103);
IPAddress gateway(192, 168, 188, 34); 
IPAddress subnet(255, 255, 255, 0);
#endif
WiFiClient wifi;
WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;
uint8_t macAddress[WL_MAC_ADDR_LENGTH];
char macAddressStr[3 + (2*WL_MAC_ADDR_LENGTH)];
// This default locally administered MAC address is just used to reserve space in the strings
#define DEFAULT_MAC "0xEE0102010203"


#ifndef USE_DHCP
IPAddress mqtt_server = gateway; // Or wherever.
#else
const char* mqtt_server = "mosquitto.home";
#endif
const char* mqtt_user = SECRET_MQTT_USER;
const char* mqtt_pass = SECRET_MQTT_PASS;
const unsigned int mqtt_port = 1883;
const char clientId[] = "gas_meter";
// https://www.home-assistant.io/integrations/mqtt/
const char sensorPrefix[] = "homeassistant/sensor/";
const char consumption[]  = "consumption";
const char flowRate[]     = "flow_rate";
const char config[]       = "config";
const char state[]        = "state";
String stateTopic;
String consumptionConfigTopic;
String flowRateConfigTopic;
#ifdef MQTT_SUBSCRIBE
// Subscribe to topic used to change the consumption count.
const char status[] = "status";
String statusSubscribeTopic;
static void mqtt_callback(char* topic, byte* payload, unsigned int length);
#endif

static void mqtt_reconnect(void);
static void mqtt_publish_state(double meterVal, double flow_rate);
PubSubClient mqtt_client(mqtt_server, mqtt_port, wifi);
StaticJsonDocument<512> doc;

uint64_t lastReconnectAttempt = 0;
uint64_t lastPulseTime = 0;
bool firstConnect = true;

// The normal units counted are 1/100s of a cubic meter, which is 10L.
unsigned int metercount = 0;
const unsigned int reed_pin = D5; // GPIO14
Bounce reed = Bounce(); // Use a Deboucer to read the reed switch pulses.

static void setup_wifi(){  // Start WiFi-Connection
#ifndef USE_DHCP
  WiFi.config(ip, gateway, subnet); // comment out if your router assigns the IP address automatically via DHCP
#endif
  WiFi.setAutoReconnect(true);

#ifdef SERIAL_LOGGING
  gotIpEventHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& event)
  {
    Serial.print("Station connected, IP: ");
    Serial.println(WiFi.localIP());
  });
#endif
  disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& event)
  {
#ifdef SERIAL_LOGGING
    Serial.println("Station disconnected from WiFi");
#endif
    mqtt_client.disconnect();
    lastReconnectAttempt = 0;
  });
#ifdef SERIAL_LOGGING
  Serial.printf("Connecting to SSID %s\n", ssid);
#endif

  WiFi.begin(ssid, password);
  WiFi.macAddress(macAddress);
  snprintf(macAddressStr, DIM(macAddressStr), "0x%x%x%x%x%x%x",
           macAddress[0], macAddress[1], macAddress[2],
           macAddress[3], macAddress[4], macAddress[5]);
}

void setup() {
#ifdef SERIAL_LOGGING
#ifdef ARDUINO_ARCH_ESP8266
  Serial.begin(74880);
#else
  Serial.begin(115200);
#endif
  Serial.setTimeout(3000);
  while(!Serial) { }
  Serial.println();
  Serial.println();
  Serial.println("Pulse counter starting:");
#endif
  setup_wifi();

  pinMode(LED_BUILTIN_AUX,OUTPUT);
  reed.attach(reed_pin, INPUT_PULLUP);
  reed.interval(5 /*ms*/);

  // Enlarge the MQTT buffer as the config messages can be quite long.
  mqtt_client.setBufferSize(1572);
  // Set up the Topic names that we are using.
  stateTopic = sensorPrefix + String(macAddressStr) + '/' + state;
  consumptionConfigTopic = sensorPrefix + String(macAddressStr) + '/' + consumption + '/' + config;
  flowRateConfigTopic = sensorPrefix + String(macAddressStr) + '/' + flowRate + '/' + config;
#ifdef MQTT_SUBSCRIBE
  statusSubscribeTopic = sensorPrefix + String(macAddressStr) + '/' + status;
  mqtt_client.setCallback(mqtt_callback);
#endif
}

void loop() {
  // Use micros64, to avoid potential glitch at 32bit millis roll over.
  uint64_t now = micros64();

  if (mqtt_client.connected()) {
    mqtt_client.loop();
  } else {
    mqtt_reconnect();
  }

  reed.update();   // read the input pin
  if (reed.changed()) {
    bool reed_state=reed.read();
    digitalWrite(LED_BUILTIN_AUX, reed_state);
    if (reed_state) {
      // Rising edge on reed switch.
      double lpm = 0.0; // Litres per minute.
      metercount++;
#ifdef SERIAL_LOGGING
      Serial.printf("Meter count %d\n", metercount);
#endif

      if (lastPulseTime == 0) {
        // First pulse, so don't have history for useful rate calculation, but use 'now'
        // time for next rising edge.
        lastPulseTime = now;
      } else {
        lpm = (10.0*60000000) / (now - lastPulseTime);
        lastPulseTime = now;
      }
      mqtt_publish_state(metercount/(double)100.0, lpm);
    }
  }
  if (lastPulseTime && (now - lastPulseTime > 60*1000000)) {
    // After 1 min of no pulse assume 0.0 flow rate, by going back to init state.
    lastPulseTime = 0;
    mqtt_publish_state(metercount/(double)100.0, 0.0);
  }
}

void mqtt_create_hass_sensor (const char *name, const char *device_class, const char *state_class,
                              const char *units, const char * value_template) {
  // Ensure the doc is clear first.
  doc.clear();
  // https://www.home-assistant.io/integrations/sensor.mqtt/
  doc["name"] = name;
  doc["unique_id"] = name;
  JsonObject device = doc.createNestedObject("device");
  device["name"] = clientId;
  device["manufacturer"] = ARDUINO_BOARD_ID;
  device["model"] = "PlatformIO Pulse Counter";
  JsonArray ids = device.createNestedArray("identifiers");
  ids.add(macAddressStr);
  if (device_class) { doc["device_class"] = device_class; }
  doc["state_class"] = state_class;
  doc["unit_of_measurement"] = units;
  doc["value_template"] = value_template;
  doc["state_topic"] = stateTopic;
}

static void mqtt_reconnect(void) {
    if (WiFi.status() != WL_CONNECTED) {
      return;
    }
  uint64_t now = micros64();
  if ((lastReconnectAttempt == 0) ||
      (now - lastReconnectAttempt > 5000000)) {
#ifdef SERIAL_LOGGING
    Serial.printf("Connecting to MQTT as %s\n", mqtt_user);
#endif
    lastReconnectAttempt = now;
    if (mqtt_client.connect(clientId,mqtt_user,mqtt_pass)) {
#ifdef MQTT_SUBSCRIBE
      mqtt_client.subscribe(statusSubscribeTopic.c_str());
#endif
      {
        uint8_t payload[512];
        size_t payloadLen;
        
        // https://www.home-assistant.io/integrations/sensor.mqtt/
        // Renitialize the current 'doc' for the "gas_consumption" entity.
        mqtt_create_hass_sensor("gas_consumption", "gas", "total_increasing", "m³", "{{ value_json.reading }}");
        payloadLen = serializeJson(doc, payload, DIM(payload));
        mqtt_client.publish(consumptionConfigTopic.c_str(), payload, payloadLen, true);

        // https://www.home-assistant.io/integrations/sensor.mqtt/
        // Renitialize the current 'doc' for the "gas_flow_rate" entity.
        mqtt_create_hass_sensor("gas_flow_rate", NULL, "measurement", "L/minute", "{{ value_json.flow_rate }}");
        payloadLen = serializeJson(doc, payload, DIM(payload));
        mqtt_client.publish(flowRateConfigTopic.c_str(), payload, payloadLen, true);
      }
      if (firstConnect) {
        // Ensure that home assistant gets the correct behaviour for a total_increasing sensor
        // https://developers.home-assistant.io/docs/core/entity/sensor/#available-state-classes
        mqtt_publish_state(0.0, 0.0);
        firstConnect = false;
      }
    }
  }
}

#ifdef MQTT_SUBSCRIBE
static void mqtt_callback(char* topic, byte* payload, unsigned int length) {
#ifdef SERIAL_LOGGING
      Serial.printf("%s=%s\n", topic, payload);
#endif
    if (strcmp(topic,statusSubscribeTopic.c_str()) == 0) {
      String metercount_str;
      for (uint i = 0; i < length; i++) {
        metercount_str += (char)payload[i];
      }
      metercount = metercount_str.toInt();
    }
}
#endif

static void mqtt_publish_state(double meterVal, double flow_rate) {
  if (mqtt_client.connected()) {
    uint8_t payload[128];
    size_t payloadLen;
    doc.clear();
    doc["reading"] = meterVal;
    doc["flow_rate"] = flow_rate;
    payloadLen = serializeJson(doc, payload, DIM(payload));
    mqtt_client.publish(stateTopic.c_str(), payload, payloadLen, false);
  }
}

