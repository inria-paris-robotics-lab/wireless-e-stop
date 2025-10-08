#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <EEPROM.h>

#define CE_PIN 9
#define CSN_PIN 10
#define RESET_BUTTON_PIN 2
#define RELAY_PIN 5

int EEPROM_ADDRESS = 0; // Address to store the channel
int ChannelNumber = 0; // Variable to hold the channel number

RF24 radio(CE_PIN, CSN_PIN); // Create a RF24 object

const byte address[6] = "1234"; // Address for communication

int state = 0; // Internal state 0: armed, 1: secured

int relay_state = LOW; // Relay state (low = circuit closed, high = circuit open)

bool msg = true; // Alarm message

// Reset button parameters
bool buttonPressed = false; // Is the button currently pressed
unsigned long buttonPressTime = 0; // Time when button was pressed
const unsigned long RESET_HOLD_TIME = 3000; // Time in ms to hold button to reset

// Leaky bucket parameters
const int BUCKET_CAPACITY = 6; // Bucket capacity before triggering alarm
int bucketLevel = BUCKET_CAPACITY; // Current bucket level (starts full)
unsigned long lastLeakTime = 0; // Last time the bucket leaked
const long LEAK_INTERVAL = 150; // Bucket loses 1 point every leak interval (ms)

void setup() {
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, relay_state); // Ensure relay is inactive at start
  EEPROM.get(EEPROM_ADDRESS, ChannelNumber);
  if (ChannelNumber < 0 || ChannelNumber > 125) { // Validate channel
    ChannelNumber = 108; // Default channel
    EEPROM.put(EEPROM_ADDRESS, ChannelNumber);
  }
  // Check Startup mode
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    Serial.begin(115200);
    while (!Serial) {
      // some boards need to wait to ensure access to serial over USB
    }
    Serial.println("Mode Setup");
    Serial.println("Please enter the new Channel (0-125, you should use high values to avoid WiFi interference): ");
    while (!Serial.available()) {
      // wait for user input
    }
    int newChannel = Serial.parseInt();
    while (!(newChannel >= 0 && newChannel <= 125)) {
      Serial.println("Invalid input. Please enter a valid channel (0-125): ");
      while (!Serial.available()) {
        // wait for user input
      }
      newChannel = Serial.parseInt();
    }
    if (newChannel != ChannelNumber) {
      Serial.print("Default Channel set to: ");
      Serial.println(newChannel);
      EEPROM.put(EEPROM_ADDRESS, newChannel);
    }
    Serial.println("Setup complete. Please release the button to continue.");
    while (digitalRead(RESET_BUTTON_PIN) == LOW) {
      delay(100);
    }
    Serial.end(); // End serial communication to save power
  }

  // Initialize nRF24L01
  while (!radio.begin()) {
    delay(500);
  }
  EEPROM.get(EEPROM_ADDRESS, ChannelNumber);
  radio.setPALevel(RF24_PA_MAX); // Set the maximum propagation distance
  radio.setPayloadSize(sizeof(msg)); // Set the payload size (help to speed up communication)
  radio.setChannel(ChannelNumber); // Set the channel from storage
  radio.setDataRate(RF24_1MBPS); // Set data rate
  radio.openReadingPipe(1, address); // Set the address for communication
  radio.startListening(); // Set the module as receiver
}

void loop() {
  uint8_t pipe;
  // Handle reset button when system is secured
  if (state == 1) {
    if (digitalRead(RESET_BUTTON_PIN) == LOW) {
      if (!buttonPressed) {
        buttonPressed = true;
        buttonPressTime = millis();
      } else {
        if (millis() - buttonPressTime >= RESET_HOLD_TIME) { // If button held long enough
          // Reset the system
          state = 0; // Change state to armed
          relay_state = LOW; // Deactivate relay
          digitalWrite(RELAY_PIN, relay_state); // Deactivate relay
          buttonPressed = false;
          bucketLevel = BUCKET_CAPACITY; // Reset bucket level
          lastLeakTime = millis(); // Reset leak timer
        }
      }
    } else {
      buttonPressed = false;
    }
  }

  // Check for incoming radio data
  if (radio.available(&pipe)) {
    bucketLevel = min(bucketLevel + 1, BUCKET_CAPACITY); // Increase bucket level
    lastLeakTime = millis(); // Reset leak timer
    // Read the incoming message
    radio.read(&msg, sizeof(msg));
    if (msg) {
      if (state == 0) { // If system is armed
        relay_state = HIGH;
        digitalWrite(RELAY_PIN, relay_state); // open the circuit
        state = 1; // Change state to secured
      } else {
        // System is already secured do nothing
      }
    }
  } else {
    if(state == 0) {
      // No data received
      if (millis() - lastLeakTime > LEAK_INTERVAL) {
        bucketLevel = max(bucketLevel - 1, 0); // Decrease bucket level
        lastLeakTime = millis(); // Update last leak time
      }
      // Check if bucket level is empty
      if (bucketLevel == 0 && state == 0) {
        relay_state = HIGH;
        digitalWrite(RELAY_PIN, relay_state); // Activate relay
        state = 1;
      }
    }
  }
}