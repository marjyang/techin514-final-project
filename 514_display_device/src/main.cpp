#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// **OLED Configuration**
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1  
#define I2C_ADDRESS   0x3C

#define SDA_PIN 6
#define SCL_PIN 7

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// **Stepper Motor Configuration**
#define MOTOR_PIN_1 0  
#define MOTOR_PIN_2 1  
#define MOTOR_PIN_3 2  
#define MOTOR_PIN_4 3  

int stepPosition = 0;    // Tracks current stepper position
const int maxSteps = 160; // Maximum stepper position
const int minSteps = 0;   // Minimum stepper position

// **Stepper Motor Step Sequence**
const int stepSequence[4][4] = {
    {1, 0, 1, 0},  
    {0, 1, 1, 0},  
    {0, 1, 0, 1},  
    {1, 0, 0, 1}   
};

// **Water Consumption Variables**
float numerator = 0.0;    // Water consumed
float denominator = 30.0; // Water consumption goal
float initialOffset = 0.0; // Offset to subtract from received values
bool firstDataReceived = false; // Flag to track if we've received initial data

// **Button Pins**
#define BUTTON_UP 8
#define BUTTON_DOWN 9

// **LED Pin**
#define LED_PIN 10  

// **BLE UUIDs**
#define SERVICE_UUID        "6ffd810a-1f60-43df-aa2f-cb68a815285f"
#define CHARACTERISTIC_UUID "7ca0eada-bb21-4d31-8c72-e52221ea4409"

static BLEUUID serviceUUID(SERVICE_UUID);
static BLEUUID charUUID(CHARACTERISTIC_UUID);
static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLEAdvertisedDevice* myDevice;
BLEClient* pClient = NULL;
BLEScan* pBLEScan = NULL;

// **Function Prototypes**
void resetStepper();
void resetStepperToZero();
void moveStepperToPosition(int targetStep);
void moveStepperForward(int steps = 1);
void moveStepperBackward(int steps = 1);
void updateDisplay();
void resetVariables();
bool connectToServer();
void handleButtonPress();

// Reset all variables to starting values
void resetVariables() {
    Serial.println("Resetting variables for new connection");
    initialOffset = 0.0;     // Clear the offset
    numerator = 0.0;         // Reset water consumption
    firstDataReceived = false; // Reset first data flag
    updateDisplay();         // Update the display to show zero
}

// **BLE Client Callback**
class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pClient) { 
        connected = true; 
        Serial.println("✅ Connected to BLE Server!");
    }
    
    void onDisconnect(BLEClient* pClient) { 
        connected = false; 
        Serial.println("❌ Disconnected from BLE Server!");
        // Reset variables when disconnected
        resetVariables();
        doScan = true;  // Restart scanning when disconnected
    }
};

// **BLE Scan Callback**
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        // Print basic device info
        Serial.print("🔍 BLE Device found: ");
        Serial.print("Name: \"");
        Serial.print(advertisedDevice.getName().c_str());
        Serial.print("\", Address: ");
        Serial.print(advertisedDevice.getAddress().toString().c_str());
        
        // Show RSSI
        Serial.print(", RSSI: ");
        Serial.print(advertisedDevice.getRSSI());
        
        // Check for service UUIDs and print them if available
        if (advertisedDevice.haveServiceUUID()) {
            Serial.print(", Service UUIDs: ");
            for (int i = 0; i < advertisedDevice.getServiceUUIDCount(); i++) {
                Serial.print(advertisedDevice.getServiceUUID(i).toString().c_str());
                Serial.print(" ");
            }
            
            // Extra diagnostic for our specific service UUID
            Serial.print(" [Looking for: ");
            Serial.print(serviceUUID.toString().c_str());
            Serial.print("]");
            
            // Check if device has our service UUID
            if (advertisedDevice.isAdvertisingService(serviceUUID)) {
                Serial.print(" ✓ MATCH FOUND!");
            } else {
                Serial.print(" ✗ No match");
            }
        } else {
            Serial.print(", No Service UUIDs advertised");
        }
        
        Serial.println();
        
        // Connect if it's our device
        if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
            BLEDevice::getScan()->stop();
            myDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnect = true;
            doScan = false;
            Serial.println("🎯 Found our water tracker device! Connecting...");
        }
    }
};

// **BLE Notification Callback**
static void notifyCallback(
    BLERemoteCharacteristic* pRemoteCharacteristic,
    uint8_t* pData,
    size_t length,
    bool isNotify) {

    Serial.println("\n📥 BLE Notification Received! 📥");
    Serial.print("Characteristic UUID: ");
    Serial.println(pRemoteCharacteristic->getUUID().toString().c_str());
    Serial.print("Data Length: ");
    Serial.println(length);

    // Print raw byte data in multiple formats
    Serial.println("🔍 RAW DATA:");
    Serial.print("   HEX: ");
    for (size_t i = 0; i < length; i++) {
        Serial.print(pData[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    
    Serial.print("   DEC: ");
    for (size_t i = 0; i < length; i++) {
        Serial.print((int)pData[i]);
        Serial.print(" ");
    }
    Serial.println();
    
    Serial.print("   ASCII: ");
    for (size_t i = 0; i < length; i++) {
        if (pData[i] >= 32 && pData[i] <= 126) { // Printable ASCII
            Serial.print((char)pData[i]);
        } else {
            Serial.print(".");
        }
    }
    Serial.println();

    // Convert raw bytes to a string
    String receivedData = "";
    for (size_t i = 0; i < length; i++) {
        receivedData += (char)pData[i];
    }

    Serial.print("📝 String Representation: '");
    Serial.print(receivedData);
    Serial.println("'");

    // Try to convert the received string to a float
    float totalLiters = 0.0;
    bool validData = true;
    
    // Basic validation to check if data is a valid number
    for (size_t i = 0; i < receivedData.length(); i++) {
        char c = receivedData.charAt(i);
        if (!isdigit(c) && c != '.' && c != '-') {
            validData = false;
            Serial.print("Invalid character detected: '");
            Serial.print(c);
            Serial.print("' at position ");
            Serial.println(i);
            break;
        }
    }
    
    if (validData) {
        totalLiters = atof(receivedData.c_str());
        Serial.print("✅ Parsed as Number: ");
        Serial.println(totalLiters);
    } else {
        Serial.println("⚠️ Invalid data format received! Could not parse as number.");
        return;
    }

    if (totalLiters >= 0) {  
        // If this is the first data received, set the offset
        if (!firstDataReceived) {
            initialOffset = totalLiters;
            firstDataReceived = true;
            Serial.print("📏 Set initial offset to: ");
            Serial.println(initialOffset);
        }
        
        // Apply offset to the received value
        float adjustedLiters = totalLiters - initialOffset;
        if (adjustedLiters < 0) adjustedLiters = 0;  // Ensure we don't go negative
        
        Serial.print("✅ Raw Water Consumption: ");
        Serial.print(totalLiters);
        Serial.println(" L");
        
        Serial.print("✅ Adjusted Water Consumption: ");
        Serial.print(adjustedLiters);
        Serial.println(" L");

        numerator = adjustedLiters;  // Store water consumed as float
        Serial.print("Current Goal: ");
        Serial.print(denominator);
        Serial.println(" L");

        // Calculate the stepper position based on the water consumption ratio
        int targetStep = map(numerator * 10, 0, denominator * 10, minSteps, maxSteps); // Multiply by 10 for better precision
        targetStep = constrain(targetStep, minSteps, maxSteps); 

        Serial.print("🚀 Moving Stepper to Step: ");
        Serial.print(targetStep);
        Serial.print(" (from ");
        Serial.print(stepPosition);
        Serial.println(")");

        moveStepperToPosition(targetStep);
        updateDisplay();
    } else {
        Serial.println("⚠️ Failed to process BLE Data! Negative value received.");
    }
}

// **BLE Connection Function**
bool connectToServer() {
    Serial.println("\n🔌 CONNECTING TO BLE SERVER 🔌");
    Serial.print("Device Address: ");
    Serial.println(myDevice->getAddress().toString().c_str());
    Serial.print("Device Name: ");
    Serial.println(myDevice->getName().c_str());
    
    // List all advertised services
    Serial.println("Advertised Services:");
    if (myDevice->haveServiceUUID()) {
        for (int i = 0; i < myDevice->getServiceUUIDCount(); i++) {
            Serial.print("  - ");
            Serial.println(myDevice->getServiceUUID(i).toString().c_str());
        }
    } else {
        Serial.println("  No services advertised");
    }

    // Clean up previous client if it exists
    if (pClient != NULL) {
        delete pClient;
    }
    
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remote BLE Server
    Serial.println("Attempting connection...");
    if (!pClient->connect(myDevice)) {
        Serial.println("❌ Failed to connect to BLE server.");
        return false;
    }
    Serial.println("Connected to device!");

    // Discover all services on device
    Serial.println("\n📋 DISCOVERING SERVICES 📋");
    pClient->getServices();
    
    // Obtain a reference to the service we are after
    Serial.print("Looking for service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        Serial.println("❌ Failed to find our target service!");
        Serial.println("Available services on device:");
        
        // List all available services for debugging
        std::map<std::string, BLERemoteService*>* serviceMap = pClient->getServices();
        for(std::map<std::string, BLERemoteService*>::iterator it = serviceMap->begin(); it != serviceMap->end(); ++it) {
            Serial.print("  - ");
            Serial.println(it->first.c_str());
        }
        
        pClient->disconnect();
        return false;
    }
    Serial.println("✅ Found our service!");

    // Find all characteristics for this service
    Serial.println("\n🔍 DISCOVERING CHARACTERISTICS 🔍");
    Serial.print("Looking for characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    
    // List all characteristics of the service
    std::map<std::string, BLERemoteCharacteristic*>* charMap = pRemoteService->getCharacteristics();
    Serial.println("Available characteristics:");
    for(std::map<std::string, BLERemoteCharacteristic*>::iterator it = charMap->begin(); it != charMap->end(); ++it) {
        Serial.print("  - ");
        Serial.print(it->first.c_str());
        
        // Show properties
        BLERemoteCharacteristic* pChar = it->second;
        Serial.print(" (Properties: ");
        if(pChar->canRead()) Serial.print("READ ");
        if(pChar->canWrite()) Serial.print("WRITE ");
        if(pChar->canNotify()) Serial.print("NOTIFY ");
        if(pChar->canIndicate()) Serial.print("INDICATE ");
        Serial.println(")");
    }
    
    // Obtain a reference to our target characteristic
    BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
        Serial.println("❌ Failed to find our target characteristic!");
        pClient->disconnect();
        return false;
    }
    Serial.println("✅ Found our characteristic!");

    // Read initial value if possible
    if (pRemoteCharacteristic->canRead()) {
        std::string value = pRemoteCharacteristic->readValue();
        Serial.println("\n📊 INITIAL DATA READING 📊");
        Serial.print("Raw value: '");
        Serial.print(value.c_str());
        Serial.println("'");
        
        // Show bytes in hex for debugging
        Serial.print("Hex: ");
        for (int i = 0; i < value.length(); i++) {
            Serial.print((uint8_t)value[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
    } else {
        Serial.println("❗ Characteristic is not readable");
    }

    // Register for notifications if supported
    if (pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
        Serial.println("✅ Successfully registered for notifications");
    } else {
        Serial.println("❗ Characteristic does not support notifications");
    }

    Serial.println("\n✅ BLE CONNECTION COMPLETE ✅");
    connected = true;
    return true;
}

// **Initialize stepper to zero position - turns back 170 steps**
void resetStepperToZero() {
    Serial.println("🔄 Initializing stepper motor - moving backward 170 steps");
    digitalWrite(LED_PIN, HIGH);  // Turn on LED during reset

    // Move backward 170 steps regardless of current position
    for (int i = 0; i < 170; i++) {
        moveStepperBackward(1);
        delay(10);  // Small delay between steps
    }
    
    // Reset position counter
    stepPosition = 0;
    
    digitalWrite(LED_PIN, LOW);  // Turn off LED when done
    Serial.println("✅ Stepper reset complete");
}

// **Reset Stepper Based on Water Consumption Ratio**
void resetStepper() {
    Serial.println("Resetting stepper to match water consumed ratio...");
    digitalWrite(LED_PIN, HIGH);

    // Calculate target step based on current water ratio
    int targetStep = map(numerator * 10, 0, denominator * 10, minSteps, maxSteps);
    targetStep = constrain(targetStep, minSteps, maxSteps);
    
    moveStepperToPosition(targetStep); 

    digitalWrite(LED_PIN, LOW);
    updateDisplay();
}

// **Move Stepper to Specific Position**
void moveStepperToPosition(int targetStep) {
    Serial.print("🚀 Moving Stepper to Position: ");
    Serial.println(targetStep);

    targetStep = constrain(targetStep, minSteps, maxSteps);

    // More efficient movement by calculating number of steps needed
    int stepsToMove = targetStep - stepPosition;
    
    if (stepsToMove > 0) {
        moveStepperForward(stepsToMove);
    } else if (stepsToMove < 0) {
        moveStepperBackward(abs(stepsToMove));
    }
    
    stepPosition = targetStep;
}

// **Stepper Motor Control Functions - Optimized for multi-step movement**
void moveStepperForward(int steps) {
    for (int i = 0; i < steps; i++) {
        for (int step = 0; step < 4; step++) {
            digitalWrite(MOTOR_PIN_1, stepSequence[step][0]);
            digitalWrite(MOTOR_PIN_2, stepSequence[step][1]);
            digitalWrite(MOTOR_PIN_3, stepSequence[step][2]);
            digitalWrite(MOTOR_PIN_4, stepSequence[step][3]);
            delay(10);
        }
    }
}

void moveStepperBackward(int steps) {
    for (int i = 0; i < steps; i++) {
        for (int step = 0; step < 4; step++) {
            int s = 3 - step;
            digitalWrite(MOTOR_PIN_1, stepSequence[s][0]);
            digitalWrite(MOTOR_PIN_2, stepSequence[s][1]);
            digitalWrite(MOTOR_PIN_3, stepSequence[s][2]);
            digitalWrite(MOTOR_PIN_4, stepSequence[s][3]);
            delay(10);
        }
    }
}

// **Update OLED Display with Progress Bar**
void updateDisplay() {
    display.clearDisplay();
    
    // Title
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Water Consumption:");
    
    // Water values with decimals
    display.setTextSize(2);
    display.setCursor(5, 16);
    
    // Format numbers with 1 decimal place
    char waterValue[16];
    char goalValue[16];
    dtostrf(numerator, 4, 1, waterValue); // Convert float to string with 1 decimal
    dtostrf(denominator, 4, 1, goalValue); // Convert float to string with 1 decimal
    
    // Remove leading spaces that dtostrf might add
    char* trimmedWater = waterValue;
    while(*trimmedWater == ' ') trimmedWater++;
    
    char* trimmedGoal = goalValue;
    while(*trimmedGoal == ' ') trimmedGoal++;
    
    // Display the formatted water values
    display.print(trimmedWater);
    display.print("/");
    display.print(trimmedGoal);
    display.println("L");

    // Draw a progress bar
    int barWidth = map(numerator * 10, 0, denominator * 10, 0, 108); // Multiply by 10 for better precision
    barWidth = constrain(barWidth, 0, 108);
    display.drawRect(10, 40, 108, 15, SSD1306_WHITE);
    display.fillRect(10, 40, barWidth, 15, SSD1306_WHITE);
    
    // Show percentage
    float percentage = (numerator / denominator) * 100.0;
    display.setTextSize(1);
    display.setCursor(40, 56);
    display.print(percentage, 1); // Display percentage with 1 decimal place
    display.print("% Full");
    
    display.display();
}

// **Button Press Handling**
void handleButtonPress() {
    // Non-blocking button checking
    static unsigned long lastButtonTime = 0;
    unsigned long currentTime = millis();
    
    if (currentTime - lastButtonTime >= 200) {  // Debounce time
        if (digitalRead(BUTTON_UP) == LOW) {
            denominator += 2.5; 
            if (denominator > 100.0) denominator = 100.0;  // Limit max goal
            Serial.print("🎯 New Goal: ");
            Serial.println(denominator);
            updateDisplay();
            lastButtonTime = currentTime;
        }

        if (digitalRead(BUTTON_DOWN) == LOW) {
            denominator -= 2.5;
            if (denominator < 5.0) denominator = 5.0;  // Avoid zero
            Serial.print("🎯 New Goal: ");
            Serial.println(denominator);
            updateDisplay();
            lastButtonTime = currentTime;
        }
    }
}

// **Setup Function**
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n🚀 Starting up water tracker device...");
    delay(1000);

    // Initialize I2C and OLED
    Wire.begin(SDA_PIN, SCL_PIN);
    if(!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDRESS)) {
        Serial.println("SSD1306 allocation failed");
        for(;;); // Don't proceed, loop forever
    }
    
    // Set up IO pins
    pinMode(BUTTON_UP, INPUT_PULLUP);
    pinMode(BUTTON_DOWN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    pinMode(MOTOR_PIN_1, OUTPUT);
    pinMode(MOTOR_PIN_2, OUTPUT);
    pinMode(MOTOR_PIN_3, OUTPUT);
    pinMode(MOTOR_PIN_4, OUTPUT);

    // Reset all variables to starting values
    resetVariables();

    // Show startup message
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Water Tracker");
    display.println("Starting...");
    display.println("Initializing motor");
    display.display();

    // First reset stepper to zero position by moving back 170 steps
    resetStepperToZero();
    
    // Update display with zeroed position
    updateDisplay();
    
    display.setCursor(0, 40);
    display.println("Setting up BLE...");
    display.display();

    // Initialize BLE
    BLEDevice::init("Display_Device");
    
    // Initialize BLE scan
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    
    // Begin scanning for BLE devices
    doScan = true;
    
    Serial.println("✅ Setup complete, ready to track water consumption!");
}

// **Loop Function - Non-blocking design**
void loop() {
    // Handle BLE connection
    if (doConnect) {
        Serial.println("🔗 Attempting to connect to BLE Server...");
        if (connectToServer()) {
            Serial.println("✅ Connected to BLE Server!");
        } else {
            Serial.println("❌ BLE Connection Failed! Retrying...");
            doScan = true;  // Restart scanning
        }
        doConnect = false;
    }

    // If disconnected, restart BLE scan
    if (!connected && doScan) {
        Serial.println("\n🔎 SCANNING FOR BLE DEVICES...");
        Serial.print("Looking for Service UUID: ");
        Serial.println(serviceUUID.toString().c_str());
        
        // Set active scanning for better results
        pBLEScan->setActiveScan(true);
        pBLEScan->setInterval(100);
        pBLEScan->setWindow(99);
        
        // Start scan with longer duration (10 seconds)
        pBLEScan->start(10, false);
        
        // No direct error check as start() returns BLEScanResults, not bool
        
        doScan = false;  // Don't start another scan until this one finishes
    }

    // Check for button presses to update the water goal
    handleButtonPress();

    // Refresh display periodically (e.g., every 1 second)
    static unsigned long lastUpdate = 0;
    unsigned long currentMillis = millis();
    
    if (currentMillis - lastUpdate > 1000) {
        Serial.println("🔄 Updating Display...");
        updateDisplay();
        lastUpdate = currentMillis;
    }

    // Small delay to prevent CPU hogging
    delay(50);
}