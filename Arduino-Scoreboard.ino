#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "soc/soc.h"             // Required for power stability
#include "soc/rtc_cntl_reg.h"    // Required for power stability

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" 
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer* pServer = NULL;
BLECharacteristic* pTxCharacteristic = NULL; // GLOBAL TX Characteristic
bool deviceConnected = false;

// --- HOME TEAM PINS (ESP32 Dev Board) ---
const int latchPinHome = 16; // 12-GEEL
const int clockPinHome = 17; // 11-GROEN
const int dataPinHome  = 18; // 14-BLOU

// --- VISITOR TEAM PINS (ESP32 Dev Board) ---
const int latchPinVisitor = 25;
const int clockPinVisitor = 26; 
const int dataPinVisitor  = 27; 

// --- GAME VARIABLES ---
int homeScore = 0;
int visitorScore = 0;

// --- TEST MODE VARIABLES ---
bool isTestMode = false;
int testScore = 0;
unsigned long testModeTimer = 0;

bool bothMinusPressed = false;
unsigned long holdStartTime = 0;
const unsigned long holdDuration = 3000; // 3 seconds to trigger

// --- DIGIT PATTERNS (0-9) ---
byte digitPatterns[10] = {
  0b01111110, // 0
  0b00001100, // 1
  0b10110110, // 2
  0b10011110, // 3
  0b11001100, // 4
  0b11011010, // 5
  0b11111010, // 6
  0b00001110, // 7
  0b11111110, // 8
  0b11011110  // 9
};

// --- BUTTON CONFIGURATION ---
struct ScoreButton {
  int pin;
  char team;          // 'H' for Home, 'V' for Visitor
  int scoreChange;    // +1 or -1
  int state;          
  int lastReading;    
  unsigned long lastDebounceTime;
};

// Define our 4 physical buttons and their safe pins
ScoreButton buttons[4] = {
  {13, 'H',  1, HIGH, HIGH, 0},  // Home +
  {14, 'H', -1, HIGH, HIGH, 0},  // Home -
  {32, 'V',  1, HIGH, HIGH, 0},  // Visitor +
  {33, 'V', -1, HIGH, HIGH, 0}   // Visitor -
};

unsigned long debounceDelay = 50;

// --- HELPER METHODS ---

// Pushes the current score to connected BLE clients
void notifyScoreUpdate() {
  if (deviceConnected && pTxCharacteristic != NULL) {
    char scoreData[25]; 
    // Format: "S-H:00,V:00"
    snprintf(scoreData, sizeof(scoreData), "S-H:%02d,V:%02d", homeScore, visitorScore);

    pTxCharacteristic->setValue((uint8_t*)scoreData, strlen(scoreData));
    pTxCharacteristic->notify();
    
    Serial.print("BLE Notify: ");
    Serial.println(scoreData);
  }
}

// Writes the score to the shift registers
void writeScoreToChip(int latchPin, int clockPin, int dataPin, int score) {
  int tens = (score / 10) % 10;
  int units = score % 10;

  digitalWrite(latchPin, LOW);
  
  // Note on Order: 
  // Send UNITS first so it gets pushed down the line to the 2nd chip.
  // Then send TENS, which stays in the 1st chip.
  shiftOut(dataPin, clockPin, MSBFIRST, digitPatterns[units]); // Pushed to 2nd Chip
  shiftOut(dataPin, clockPin, MSBFIRST, digitPatterns[tens]);  // Stays in 1st Chip
  
  digitalWrite(latchPin, HIGH);
}

// Applies bounds so score stays between 0 and 99
void enforceScoreBounds() {
  if (homeScore < 0) homeScore = 0;
  if (visitorScore < 0) visitorScore = 0;
  if (homeScore > 99) homeScore = 99;
  if (visitorScore > 99) visitorScore = 99;
}

// Updates both physical displays and sends BLE update
void refreshDisplays() {
  writeScoreToChip(latchPinHome, clockPinHome, dataPinHome, homeScore);
  writeScoreToChip(latchPinVisitor, clockPinVisitor, dataPinVisitor, visitorScore);
  Serial.printf("Home: %02d | Visitor: %02d\n", homeScore, visitorScore);
  
  // Push the update to the phone app
  notifyScoreUpdate(); 
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
      String value = pCharacteristic->getValue().c_str();

      if (value.length() >= 1 && !isTestMode) { // Ignore BLE inputs during test mode
        Serial.print("Cmd: ");
        Serial.println(value);
        
        char cmd = value[0];

        // --- 1. HANDLE SYNC REQUEST ---
        if (cmd == 'U' || cmd == 'u') {
          Serial.println("App requested state sync.");
          notifyScoreUpdate(); 
          return;              
        }

        // --- 2. HANDLE SCORE CHANGES (Requires 2 chars) ---
        if (value.length() >= 2) {
          char action = value[1]; // '+' or '-'

          if (cmd == 'H' || cmd == 'h') {
            if (action == '+') homeScore++;
            if (action == '-') homeScore--;
          }
          else if (cmd == 'V' || cmd == 'v') {
            if (action == '+') visitorScore++;
            if (action == '-') visitorScore--;
          }
          else if (cmd == 'R') {
            homeScore = 0;
            visitorScore = 0;
          }

          enforceScoreBounds();
          refreshDisplays();
        }
      }
    }
};

// --- CORE SETUP ---
void setup() {
  // DISABLE BROWNOUT DETECTOR (Crucial for ESP32 Dev Boards using BLE)
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);

  // Initialize HOME Pins
  pinMode(latchPinHome, OUTPUT);
  pinMode(clockPinHome, OUTPUT);
  pinMode(dataPinHome, OUTPUT);

  // Initialize VISITOR Pins
  pinMode(latchPinVisitor, OUTPUT);
  pinMode(clockPinVisitor, OUTPUT);
  pinMode(dataPinVisitor, OUTPUT);

  // Initialize all 4 buttons with internal pull-ups
  for (int i = 0; i < 4; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
  }

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

  // Use the GLOBAL pTxCharacteristic variable here
  pTxCharacteristic = pService->createCharacteristic(
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
  
  // BOOT TEST
  writeScoreToChip(latchPinHome, clockPinHome, dataPinHome, 88);
  writeScoreToChip(latchPinVisitor, clockPinVisitor, dataPinVisitor, 88);
  delay(1000);
  refreshDisplays();
}

// --- MAIN LOOP ---
void loop() {
  // 1. RUN TEST MODE ANIMATION
  if (isTestMode) {
    if (millis() - testModeTimer >= 200) { // Increment every 200ms
      testModeTimer = millis();
      testScore++;
      if (testScore > 99) testScore = 0;
      
      // Update displays directly with the test score
      writeScoreToChip(latchPinHome, clockPinHome, dataPinHome, testScore);
      writeScoreToChip(latchPinVisitor, clockPinVisitor, dataPinVisitor, testScore);
    }
  }

  // 2. CHECK ALL BUTTONS
  bool anyButtonPressedThisLoop = false;
  
  for (int i = 0; i < 4; i++) {
    int reading = digitalRead(buttons[i].pin);
    
    if (reading != buttons[i].lastReading) {
      buttons[i].lastDebounceTime = millis();
    }

    if ((millis() - buttons[i].lastDebounceTime) > debounceDelay) {
      if (reading != buttons[i].state) {
        buttons[i].state = reading;
        
        // ONLY trigger on the exact moment the button goes DOWN (Falling Edge)
        if (buttons[i].state == LOW) {
          anyButtonPressedThisLoop = true;
          
          // If we are NOT in test mode, update the normal scores
          if (!isTestMode) {
            if (buttons[i].team == 'H') homeScore += buttons[i].scoreChange;
            if (buttons[i].team == 'V') visitorScore += buttons[i].scoreChange;
            
            enforceScoreBounds();
            refreshDisplays();
          }
        }
      }
    }
    buttons[i].lastReading = reading;
  }

  // 3. HANDLE TEST MODE EXIT
  // If we are in test mode and ANY button is pressed, exit and reset to 0
  if (isTestMode && anyButtonPressedThisLoop) {
    Serial.println("Exiting Test Mode. Resetting Scores.");
    isTestMode = false;
    homeScore = 0;
    visitorScore = 0;
    bothMinusPressed = false; 
    refreshDisplays();
    return; // Skip the hold check this loop so we don't accidentally re-trigger
  }

  // 4. CHECK FOR 3-SECOND HOLD TO ENTER TEST MODE
  // buttons[1] is Home -, buttons[3] is Visitor -
  if (!isTestMode) {
    if (buttons[1].state == LOW && buttons[3].state == LOW) {
      if (!bothMinusPressed) {
        bothMinusPressed = true;
        holdStartTime = millis(); // Start the 3-second timer

      } else if (millis() - holdStartTime >= holdDuration) {
        // 3 SECONDS REACHED! Enter test mode.
        Serial.println(">>> Preparing for test mode <<<");
        writeScoreToChip(latchPinHome, clockPinHome, dataPinHome, 88);
        writeScoreToChip(latchPinVisitor, clockPinVisitor, dataPinVisitor, 88);
        delay(2000);

        Serial.println(">>> TEST MODE ACTIVATED <<<");
        isTestMode = true;
        testScore = 0;
        testModeTimer = millis();
        bothMinusPressed = false; // Reset the hold trigger
      }
    } else {
      // If either button is released before 3 seconds, cancel the hold
      bothMinusPressed = false;
    }
  }
}