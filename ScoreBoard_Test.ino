#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" 
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer* pServer = NULL;
bool deviceConnected = false;

// --- HOME TEAM PINS ---
const int latchPinHome = 3; 
const int clockPinHome = 4; 
const int dataPinHome  = 5; 

// --- VISITOR TEAM PINS ---
// TODO: Connect your second chip to these pins (or change them to match your wiring)
const int latchPinVisitor = 6; 
const int clockPinVisitor = 7; 
const int dataPinVisitor  = 8; 

// GAME VARIABLES
int homeScore = 0;
int visitorScore = 0;

// DIGIT PATTERNS (0-9)
byte digitPatterns[10] = {
  0b00111111, // 0
  0b00000110, // 1
  0b01011011, // 2
  0b01001111, // 3
  0b01100110, // 4
  0b01101101, // 5
  0b01111101, // 6
  0b00000111, // 7
  0b01111111, // 8
  0b01101111  // 9
};

// --- HELPER METHOD (Refactored for reuse) ---
void writeScoreToChip(int latchPin, int clockPin, int dataPin, int score) {
  // Boundary check: ensure we only display 0-9 (single digit)
  int safeIndex = abs(score) % 10; 

  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, digitPatterns[safeIndex]); 
  digitalWrite(latchPin, HIGH);
}

// --- CALLBACKS ---
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println(">> Client Connected");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println(">> Client Disconnected");
      BLEDevice::startAdvertising(); 
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();

      if (value.length() >= 2) {
        Serial.print("Cmd: ");
        Serial.println(value);
        
        char team = value[0];   // 'H' or 'V'
        char action = value[1]; // '+' or '-'

        // Update Variables
        if (team == 'H' || team == 'h') {
          if (action == '+') homeScore++;
          if (action == '-') homeScore--;
        }
        else if (team == 'V' || team == 'v') {
          if (action == '+') visitorScore++;
          if (action == '-') visitorScore--;
        }
        else if (team == 'R') {
          homeScore = 0;
          visitorScore = 0;
        }

        // Prevent negative numbers
        if (homeScore < 0) homeScore = 0;
        if (visitorScore < 0) visitorScore = 0;
        
        // --- UPDATE HARDWARE ---
        // We now call the helper method for each specific hardware set
        writeScoreToChip(latchPinHome, clockPinHome, dataPinHome, homeScore);
        writeScoreToChip(latchPinVisitor, clockPinVisitor, dataPinVisitor, visitorScore);
        
        Serial.printf("Home: %d | Visitor: %d\n", homeScore, visitorScore);
      }
    }
};

void setup() {
  Serial.begin(115200);

  // Initialize HOME Pins
  pinMode(latchPinHome, OUTPUT);
  pinMode(clockPinHome, OUTPUT);
  pinMode(dataPinHome, OUTPUT);

  // Initialize VISITOR Pins
  pinMode(latchPinVisitor, OUTPUT);
  pinMode(clockPinVisitor, OUTPUT);
  pinMode(dataPinVisitor, OUTPUT);

  // BLE Setup
  BLEDevice::init("Scoreboard Pro"); 
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                                            CHARACTERISTIC_UUID_RX,
                                            BLECharacteristic::PROPERTY_WRITE
                                        );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  BLECharacteristic *pTxCharacteristic = pService->createCharacteristic(
                                          CHARACTERISTIC_UUID_TX,
                                          BLECharacteristic::PROPERTY_NOTIFY
                                        );
  pTxCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  
  Serial.println("Waiting for app...");
  
  // Reset Display on Boot
  writeScoreToChip(latchPinHome, clockPinHome, dataPinHome, 0);
  writeScoreToChip(latchPinVisitor, clockPinVisitor, dataPinVisitor, 0);
}

void loop() {
  delay(1000); 
}
