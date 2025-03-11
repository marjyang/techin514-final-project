#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define FLOW_SENSOR_PIN 2  // Directly connected to YF-S201 signal pin

// BLE Server Variables
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
unsigned long previousMillis = 0;
const long interval = 1000;  // BLE update interval (1 second)

// Flow Sensor Variables
volatile uint16_t pulseCount = 0;
float flowRate = 0.0;
float totalLiters = 0.0;
unsigned long lastTime = 0;

// BLE UUIDs
#define SERVICE_UUID        "6ffd810a-1f60-43df-aa2f-cb68a815285f"  // Unique service ID
#define CHARACTERISTIC_UUID "7ca0eada-bb21-4d31-8c72-e52221ea4409"  // Unique characteristic ID

// Interrupt Service Routine (ISR) for Flow Sensor
void IRAM_ATTR countPulse() {
    pulseCount++;
}

// BLE Server Callbacks
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
    }
};

void setup() {
    Serial.begin(115200);
    delay(3000); 
    Serial.println("Initializing BLE...");
    // Setup Flow Sensor
    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), countPulse, FALLING);  // Detect pulses

    // Setup BLE Server
    BLEDevice::init("YF-S201_Sensor");
    Serial.println("BLE Device Initialized as: YF-S201_Sensor");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharacteristic->addDescriptor(new BLE2902());

    pService->start();

    // Start advertising BLE service
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    Serial.println("BLE is now advertising...");
    
    Serial.println("BLE Server Started. Waiting for connections...");
}

void loop() {
    unsigned long currentTime = millis();

    if (currentTime - lastTime >= 1000) {  // Every second
        detachInterrupt(FLOW_SENSOR_PIN);

        // Convert pulse count to flow rate in L/min
        flowRate = (pulseCount / 7.5);
        if (flowRate > 50.0) {  // 🚨 Limit max realistic flow rate
            Serial.println("⚠️ Warning: Unrealistic flow rate detected!");
            flowRate = 0.0;  // Ignore this reading
        }
        // Calculate total volume (Liters)
        totalLiters += (flowRate / 60.0); // Convert L/min to liters per second

        Serial.print("Flow Rate: ");
        Serial.print(flowRate);
        Serial.print(" L/min, Total Accumulated: ");
        Serial.print(totalLiters);
        Serial.println(" L");

        pulseCount = 0;  // Reset count
        lastTime = currentTime;

        attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), countPulse, FALLING);

        // Send data via BLE if connected
        if (deviceConnected) {
            char buffer[20];
            memset(buffer, 0, sizeof(buffer)); // Clear buffer to prevent corruption
            snprintf(buffer, sizeof(buffer), "%.2f", totalLiters);

            Serial.print("Sending BLE Data: ");
            Serial.println(buffer);  // Debug print to confirm full message is sent

            pCharacteristic->setValue(buffer);
            pCharacteristic->notify();
        }
    }

    // Manage BLE connection status
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        Serial.println("Restarting BLE Advertising...");
        oldDeviceConnected = deviceConnected;
    }

    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }

    delay(1000);
}

