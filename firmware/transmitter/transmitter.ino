#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <EEPROM.h>

#define CE_PIN 9 
#define CSN_PIN 10 
#define BUTTON_PIN 2

int EEPROM_ADDRESS = 0; // Address to store the channel
int ChannelNumber = 0; // Variable to hold the channel number

int period = 100; // Transmission period in ms

RF24 radio(CE_PIN, CSN_PIN); // Create a RF24 object

const byte address[6] = "1234"; // Address for communication

bool msg = false; // Message to send (button state)

void setup() {
  // Setup 
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  EEPROM.get(EEPROM_ADDRESS, ChannelNumber);
  if (ChannelNumber < 0 || ChannelNumber > 125) {
    ChannelNumber = 108; // Default channel
    EEPROM.put(EEPROM_ADDRESS, ChannelNumber);
  }
  // Check Startup mode
  if (digitalRead(BUTTON_PIN) == HIGH) {
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
    while (digitalRead(BUTTON_PIN) == HIGH) {
      delay(100);
    }
    Serial.end();
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
  radio.openWritingPipe(address); // Set the address for communication
  radio.stopListening(); // Set the module as transmitter
}

void loop() {

  msg = digitalRead(BUTTON_PIN) == HIGH; // Read the button state

  bool sent = radio.write(&msg, sizeof(msg));

  delay(period); // Wait for the next transmission
}