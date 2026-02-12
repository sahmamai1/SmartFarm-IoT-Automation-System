#include <Arduino.h>
#include <ModbusMaster.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

//Pin กำหนดสำหรับเซนเซอร์และอุปกรณ์
#define SOIL_PIN 35
#define LIGHT_PIN 32
#define DOOR_PIN 33
#define BUZZER_PIN 14
#define PUMP_PIN 27
#define FAN_PIN 26
#define WATER_LEVEL_PIN 34 

// สำหรับมอเตอร์ดึงสแลน (ใช้บอร์ดขับมอเตอร์ L298N)
#define MOTOR_SLAN_IN1 12
#define MOTOR_SLAN_IN2 13
#define MOTOR_ENA     15 

// RS485 (ESP32)
#define RS485_RX 16
#define RS485_TX 17
#define RS485_REDE 4

// Modbus Master
ModbusMaster mb;
void preTransmission() { digitalWrite(RS485_REDE, HIGH); } 
void postTransmission() { digitalWrite(RS485_REDE, LOW); }

// Thresholds
const int SOIL_DRY_THRESHOLD   = 400;
const int LIGHT_HIGH_THRESHOLD = 800;
const int LIGHT_LOW_THRESHOLD  = 200;

// Hysteresis พัดลม
bool fanState = false;

// Hysteresis ปั๊มน้ำ
bool pumpState = false;

// มอเตอร์สแลน
unsigned long motorStartTime = 0;   
bool motorRunning = false;           
bool motorDirectionRight = true;     
bool motorTriggered = false; 

// Modbus
uint8_t slaveID = 1; 
uint16_t regAddr = 0;

// WiFi
const char* ssid = "TANINTON";
const char* password = "0895414176";

// MQTT
const char* mqtt_server = "broker.hivemq.com";  
const int mqtt_port = 1883;
const char* mqtt_topic = "esp32/sensor";   

WiFiClient espClient;
PubSubClient client(espClient);

// อ่าน Soil เฉลี่ย
int readSoil() {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(SOIL_PIN);
    delay(5);
  }
  return sum / 10;
}

// --- WiFi ---
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("📡 Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("connecting...");
  }

  Serial.println("\n✅ WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

// --- MQTT reconnect ---
void reconnect() {
  while (!client.connected()) {
    Serial.print("🔄 Attempting MQTT connection...");

   
    String clientId = "ESP32Client-";
    clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("✅ Connected to MQTT Broker");
    } else {
      Serial.print("❌ Failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(DOOR_PIN, INPUT_PULLDOWN); 
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(WATER_LEVEL_PIN, INPUT);
  pinMode(MOTOR_SLAN_IN1, OUTPUT);
  pinMode(MOTOR_SLAN_IN2, OUTPUT);
  pinMode(MOTOR_ENA, OUTPUT);

  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(BUZZER_PIN, HIGH);
  digitalWrite(MOTOR_SLAN_IN1, LOW);
  digitalWrite(MOTOR_SLAN_IN2, LOW);
  analogWrite(MOTOR_ENA, 250); // ความเร็วมอเตอร์เริ่มต้น

  pinMode(RS485_REDE, OUTPUT);
  digitalWrite(RS485_REDE, LOW);
  Serial2.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);

  mb.begin(slaveID, Serial2);
  mb.preTransmission(preTransmission);
  mb.postTransmission(postTransmission);

  Serial.println("✅ XY-MD02 Auto Scan Start");

  // WiFi & MQTT
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // --- 1. อ่านค่า Modbus XY-MD02 ---
  float tempC = NAN, humid = NAN;
  uint8_t result = mb.readInputRegisters(regAddr, 2); 
  if (result == mb.ku8MBSuccess) { 
    uint16_t humid_raw = mb.getResponseBuffer(1); 
    uint16_t temp_raw  = mb.getResponseBuffer(0); 
    humid = humid_raw / 10.0f; 
    tempC = temp_raw / 10.0f; 
  } else { 
    if (regAddr == 0x0000) regAddr = 0x0001;
    else if (regAddr == 0x0001) regAddr = 0x0100;
    else {
      regAddr = 0x0000;
      slaveID++;
      if (slaveID > 5) slaveID = 1;
      mb.begin(slaveID, Serial2);
    }
  }

  // --- 2. อ่านเซนเซอร์อื่น ๆ ---
  int soilRaw = readSoil();
  int soilPct = map(soilRaw, 0, 4095, 100, 0); 
  int light = analogRead(LIGHT_PIN);
  int door = digitalRead(DOOR_PIN);
  int waterLevel = digitalRead(WATER_LEVEL_PIN);
  
  // --- 3. แสดงผล Serial ---
  Serial.println("================================");
  Serial.println("--------- Sensor Data ---------");
  if (!isnan(tempC)) {
    Serial.printf("🌡️ Temperature: %.2f °C\n", tempC);
    Serial.printf("💧 Humidity: %.2f %%\n", humid);
  } else {
    Serial.println("⚠️ Modbus read failed (XY-MD02)");
  }
  Serial.printf("🌱 Soil Moisture: %d %%\n", soilPct);
  Serial.printf("☀️ Light: %d\n", light);
  Serial.printf("🚪 Door: %s\n", (door == LOW? "Open" : "Closed"));

  
  Serial.println("--- Actuator Status ---");

  // ปั๊มน้ำ: เปิดเมื่อความชื้น <45% และปิดเมื่อ >65%
  if (soilPct < 40 && !pumpState) {
    pumpState = true;
    digitalWrite(PUMP_PIN, HIGH);
    Serial.println("💧 Pump: ON (Soil moisture low)");
  } else if (soilPct > 60 && pumpState) {
    pumpState = false;
    digitalWrite(PUMP_PIN, LOW);
    Serial.println("💧 Pump: OFF (Soil moisture ok)");
  } else {
    Serial.printf("💧 Pump: %s\n", pumpState ? "ON" : "OFF");
  }

  // พัดลม
  if (!isnan(tempC)) {
    if (!fanState && tempC >= 28.0) {
      fanState = true;
      digitalWrite(FAN_PIN, HIGH);
      Serial.println("🌬️ Fan: ON");
    } else if (fanState && tempC <= 27.0) {
      fanState = false;
      digitalWrite(FAN_PIN, LOW);
      Serial.println("🌬️ Fan: OFF");
    } else {
      Serial.printf("🌬️ Fan: %s\n", fanState ? "ON" : "OFF");
    }
  }

  // น้ำต่ำ
  if (waterLevel == LOW) {
    Serial.println("📢 Warning: Water level is LOW!");
  } else {
    Serial.println("ระดับน้ำ : ปกติ");
  }

  // ตรวจจับประตู
  if (door == HIGH) {
    Serial.println("🚪 Door is CLOSED."); 
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    Serial.println("📢 Warning: Door is OPEN!");
    digitalWrite(BUZZER_PIN, LOW);
  }

  // --- 5. ควบคุมมอเตอร์สแลนด้วย light ---
  if (!motorRunning) {
    if (light > 2000 && !motorTriggered) {
        digitalWrite(MOTOR_SLAN_IN1, HIGH);
        digitalWrite(MOTOR_SLAN_IN2, LOW);
        motorDirectionRight = true;
        motorRunning = true;
        motorStartTime = millis();
        motorTriggered = true;
        
    } else if (light <= 2000 && !motorTriggered) {
        digitalWrite(MOTOR_SLAN_IN1, LOW);
        digitalWrite(MOTOR_SLAN_IN2, HIGH);
        motorDirectionRight = false;
        motorRunning = true;
        motorStartTime = millis();
        motorTriggered = true;
    }
  } 

  if (motorRunning && millis() - motorStartTime >= 5000) {
      digitalWrite(MOTOR_SLAN_IN1, LOW);
      digitalWrite(MOTOR_SLAN_IN2, LOW);
      motorRunning = false;
  }

  if (motorTriggered) {
      if ((motorDirectionRight && light <= 200) || (!motorDirectionRight && light > 200)) {
          motorTriggered = false;
      }
  }

  if (light >= 200.0) {
    Serial.println("หลังคาปิด");
  } else {
    Serial.println("หลังคาเปิด");
  }

  // --- 6. MQTT JSON Publish ---
  StaticJsonDocument<256> doc;
  doc["temperature"] = isnan(tempC) ? 0 : tempC;
  doc["humidity"] = isnan(humid) ? 0 : humid;
  doc["soil"] = soilPct;
  doc["light"] = light;
  doc["door"] = (door == LOW ? "OPEN" : "CLOSED");
  doc["water_level"] = (waterLevel == LOW ? "LOW" : "NORMAL");
  doc["fan"] = fanState ? "ON" : "OFF";
  doc["pump"] = pumpState ? "ON" : "OFF";
  doc["roof"] = (light > 200 ? "CLOSED" : "OPEN");

  char buffer[256];
  serializeJson(doc, buffer);

  if (client.publish(mqtt_topic, buffer)) {
    Serial.println("📤 Sent to MQTT:");
    Serial.println(buffer);
  } else {
    Serial.println("⚠️ Failed to publish message!");
  }

  delay(200); 
}
