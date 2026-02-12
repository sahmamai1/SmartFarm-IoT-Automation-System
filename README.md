# Smart Farm IoT Automation System

ESP32-based smart greenhouse automation system using MQTT and Modbus RTU.


##  Project Overview

This project is an IoT-based smart farm system built using ESP32.  
It monitors environmental conditions such as temperature, humidity, soil moisture, and light intensity, and automatically controls irrigation, ventilation, and roof shading mechanisms.

Sensor data is transmitted via MQTT to a HiveMQ broker and visualized using Node-RED dashboard.


##  System Architecture

Sensors → ESP32 → Control Logic → Actuators  
                    ↓  
                MQTT (HiveMQ)  
                    ↓  
               Node-RED Dashboard  


##  Hardware Components

- ESP32 Dev Board  
- XY-MD02 (Modbus RS485 Temperature & Humidity Sensor)  
- Soil Moisture Sensor  
- TEMT6000 Light Sensor  
- Relay Module  
- L298N Motor Driver  
- Mini Water Pump  
- DC Fan  
- DC Gear Motor  


##  Communication Protocols

- WiFi (TCP/IP)  
- MQTT (Publish / Subscribe Model)  
- Modbus RTU (RS485)  
- JSON Data Format  


##  Control Logic

- Pump ON when soil moisture < 40%  
- Pump OFF when soil moisture > 60% (Hysteresis Control)  
- Fan ON when temperature ≥ 28°C  
- Roof adjusts based on light intensity  


##  Sample MQTT JSON Payload

```json
{
  "temperature": 29.8,
  "humidity": 71.4,
  "soil": 67,
  "light": 120,
  "door": "OPEN",
  "water_level": "LOW"
}


## System Demo

### Physical Prototype

![Smart Farm Model 1](images/smartfarm1.jpg)

![Smart Farm Model 2](images/smartfarm2.jpg)

The physical prototype built using ESP32 with integrated sensors, actuators, and IoT communication system.

