// main.ino
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Toggle depending on sensor used
#define USE_MAX30102 true

#if USE_MAX30102
  // MAX30102 libs (install SparkFun MAX3010x or Adafruit MAX30102 libs)
  #include <Wire.h>
  #include "MAX30105.h"         // SparkFun MAX30105 library
  MAX30105 particleSensor;
#else
  // PulseSensor simple analog sensor
  #include <PulseSensorPlayground.h>
  PulseSensorPlayground pulseSensor;
  const int PULSE_PIN = 34; // ADC pin
#endif

// WiFi / MQTT config
const char* SSID = "YOUR_SSID";
const char* PASS = "YOUR_WIFI_PASS";
const char* MQTT_SERVER = "mqtt"; // with docker-compose service name 'mqtt' (or IP)
const int MQTT_PORT = 1883;
const char* MQTT_USER = "mqtt_user";
const char* MQTT_PASS = "mqtt_pass";
const char* DEVICE_ID = "esp32-device-001";

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 2000; // ms

// Simple BPM detection storage
volatile int bpm = 0;
volatile bool have_bpm = false;

void connectWiFi() {
  WiFi.begin(SSID, PASS);
  Serial.print("Connecting WiFi");
  int tries=0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  Serial.println();
  if(WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect failed");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // not used currently; placeholder for control topics
}

void connectMQTT() {
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(mqttCallback);
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect(DEVICE_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("connected");
      // Subscribe if needed
      // client.subscribe("hr/device/+/cmd");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2s");
      delay(2000);
    }
  }
}

#if USE_MAX30102

// Tiny BPM measurement based on sample window & peak detection.
// For production, use a tested library for HR detection (e.g., SparkFun examples)
const int BUFFER_SIZE = 100;
uint32_t irBuffer[BUFFER_SIZE];
int bufferIdx = 0;

void setupMAX() {
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30105 was not found. Check wiring/power.");
    // don't block; still allow testing analog mode maybe
  } else {
    particleSensor.setup(); // configure with default values
    particleSensor.setPulseAmplitudeRed(0x0A); // LED power
    particleSensor.setPulseAmplitudeIR(0x0A);
    Serial.println("MAX30105 initialized");
  }
}

float computeBPMFromIRSamples() {
  // naive approach: compute peaks in IR buffer => crude BPM
  // Count peaks over buffer duration
  int peaks = 0;
  for (int i = 1; i < BUFFER_SIZE-1; ++i) {
    if (irBuffer[i] > irBuffer[i-1] && irBuffer[i] > irBuffer[i+1] && irBuffer[i] > 5000) {
      peaks++;
    }
  }
  // buffer spans roughly (BUFFER_SIZE * samplePeriod). With default sensor sample ~100Hz
  float durationSeconds = (float)BUFFER_SIZE / 100.0;
  if (durationSeconds <= 0) return 0;
  float bpmLocal = (peaks / durationSeconds) * 60.0;
  return bpmLocal;
}

#endif

void publishReading(int bpmVal) {
  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["ts"] = millis();
  doc["bpm"] = bpmVal;
  doc["confidence"] = 0.9; // placeholder
  char out[256];
  size_t n = serializeJson(doc, out);
  // topic
  String topic = String("hr/device/") + DEVICE_ID + "/reading";
  if (client.connected()) {
    client.publish(topic.c_str(), out, n);
    Serial.print("Published: "); Serial.println(out);
  } else {
    Serial.println("MQTT not connected");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  connectWiFi();

#if USE_MAX30102
  setupMAX();
#else
  // PulseSensor init
  pulseSensor.analogInput(PULSE_PIN);
  pulseSensor.begin();
  pulseSensor.setThreshold(550);
#endif

  connectMQTT();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!client.connected()) connectMQTT();
  client.loop();

  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL) {
#if USE_MAX30102
    // read IR value and maintain buffer
    if (particleSensor.available()) {
      long ir = particleSensor.getIR();
      irBuffer[bufferIdx++] = ir;
      if (bufferIdx >= BUFFER_SIZE) {
        bufferIdx = 0;
        float computed = computeBPMFromIRSamples();
        int reported = (int)round(computed);
        if (reported > 30 && reported < 220) {
          publishReading(reported);
        } else {
          // publish 0 or skip. We'll publish 0 as placeholder
          publishReading(0);
        }
      }
      particleSensor.nextSample();
    } else {
      // no sample available; still attempt publish last known or skip
      publishReading(0);
    }
#else
    // PulseSensor library: get BPM
    if (pulseSensor.sawStartOfBeat()) {
      int myBPM = pulseSensor.getBeatsPerMinute();
      if (myBPM > 30 && myBPM < 220) {
        publishReading(myBPM);
      } else {
        publishReading(0);
      }
    } else {
      // no beat detected in this window; optionally publish 0
      publishReading(0);
    }
#endif
    lastPublish = now;
  }
}
