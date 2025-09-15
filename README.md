# ESP32 Pulse Oximeter

A health monitoring system built using ESP32 that measures temprature and heart rate using a pulse oximeter sensor.

## Features
- Reads temp and pulse rate data from MAX30102 sensor
- Real-time display on OLED screen
- Wireless data transmission over WiFi (HTTP/MQTT)
- Logging support for long-term monitoring

## Hardware
- ESP32 Dev Board
- MAX30102 Pulse Oximeter & Heart Rate Sensor
- OLED Display (SSD1306)

## Setup
1. Flash the firmware to ESP32 using Arduino IDE or PlatformIO.
2. Connect to serial monitor to see real-time readings.
3. Access data remotely through HTTP endpoint or MQTT broker.

## Applications
- Health tracking
- Remote patient monitoring
- Wearable IoT projects
