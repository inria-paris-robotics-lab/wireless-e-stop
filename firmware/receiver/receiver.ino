#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <RF24.h>
#include <SPI.h>
#include <nRF24L01.h>

#define CE_PIN 10
#define CSN_PIN 9
#define RESET_BUTTON_PIN 3
#define RELAY_PIN 5
#define LED_PIN 2
#define LED_COUNT 1

int EEPROM_ADDRESS = 0; // Address to store the channel
int ChannelNumber = 0;  // Variable to hold the channel number

RF24 radio(CE_PIN, CSN_PIN); // Create a RF24 object

const byte address[6] = "1234"; // Address for communication

int state = 0; // Internal state 0: armed, 1: secured

int relay_state = HIGH; // Relay state (low = circuit closed, high = circuit open)

// FIX #1: initialisé à false. "true" reçu par radio = signal d'alarme.
// Tant qu'aucun message n'a été reçu, on ne doit pas considérer qu'il y a alarme.
bool msg = false; // Alarm message

// Reset button parameters
bool buttonPressed = false;                 // Is the button currently pressed
unsigned long buttonPressTime = 0;          // Time when button was pressed
const unsigned long RESET_HOLD_TIME = 3000; // Time in ms to hold button to reset

// Leaky bucket parameters
const int BUCKET_CAPACITY = 6;     // Bucket capacity before triggering alarm
int bucketLevel = BUCKET_CAPACITY; // Current bucket level (starts full)
unsigned long lastLeakTime = 0;    // Last time the bucket leaked
const long LEAK_INTERVAL = 150;    // Bucket loses 1 point every leak interval (ms)

// Signal LED
Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
void setLed(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

void setup() {
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, relay_state); // Ensure relay is inactive at start
  // setup led
  led.begin();
  led.setBrightness(50); // 0-255
  led.show();
  EEPROM.get(EEPROM_ADDRESS, ChannelNumber);
  if (ChannelNumber < 0 || ChannelNumber > 125) { // Validate channel
    ChannelNumber = 110;                          // Default channel
    EEPROM.put(EEPROM_ADDRESS, ChannelNumber);
  }
  // Check Startup mode
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    Serial.begin(115200);
    setLed(255, 255, 0);
    while (!Serial) {
      // some boards need to wait to ensure access to serial over USB
    }
    Serial.println("Mode Setup");
    Serial.println("Please enter the new Channel (0-125, you should use high values to avoid WiFi "
                   "interference): ");
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
    Serial.println("Setup channel done.");
    while (digitalRead(RESET_BUTTON_PIN) == LOW) {
      delay(100);
    }
    Serial.end(); // End serial communication to save power
  }

  // Initialize nRF24L01
  bool blink = false;
  while (!radio.begin()) {
    if (blink)
      setLed(255, 150, 0);
    else
      setLed(0, 0, 0);
    blink = !blink;
    delay(500);
  }

  setLed(0, 0, 255);
  EEPROM.get(EEPROM_ADDRESS, ChannelNumber);
  radio.setPALevel(RF24_PA_MAX);     // Set the maximum propagation distance
  radio.setPayloadSize(sizeof(msg)); // Set the payload size (help to speed up communication)
  radio.setChannel(ChannelNumber);   // Set the channel from storage
  radio.setDataRate(RF24_1MBPS);     // Set data rate
  radio.openReadingPipe(1, address); // Set the address for communication
  radio.startListening();            // Set the module as receiver
  // Deactive the relay
  relay_state = LOW;
  digitalWrite(RELAY_PIN, relay_state);

  lastLeakTime = millis();

  // Serial.println("Setup full");
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
          state = 0;                            // Change state to armed
          relay_state = LOW;                    // Deactivate relay
          digitalWrite(RELAY_PIN, relay_state); // Deactivate relay
          bucketLevel = BUCKET_CAPACITY;        // Reset bucket level
          lastLeakTime = millis();              // Reset leak timer
          msg = false;
          buttonPressed = false;
          setLed(0, 0, 255);
        }
      }
    } else {
      buttonPressed = false;
    }
  }

  // Check for incoming radio data and take the most recent message
  while (radio.available(&pipe)) {
    radio.read(&msg, sizeof(msg));
    bucketLevel = min(bucketLevel + 1, BUCKET_CAPACITY);
    lastLeakTime = millis();
  }

  if (msg && state == 0) { // If system is armed and alarm signal received
    relay_state = HIGH;
    digitalWrite(RELAY_PIN, relay_state); // open the circuit
    state = 1;                            // Change state to secured
    setLed(255, 0, 0);
    // Serial.println("Alarm triggered!");
  } else {
    if (state == 0) {
      // No data received
      if (millis() - lastLeakTime > LEAK_INTERVAL) {
        bucketLevel = max(bucketLevel - 1, 0); // Decrease bucket level
        lastLeakTime = millis();               // Update last leak time
      }
      // Check if bucket level is empty
      if (bucketLevel == 0 && state == 0) {
        relay_state = HIGH;
        digitalWrite(RELAY_PIN, relay_state); // Activate relay
        state = 1;
        setLed(255, 0, 0); // FIX #3: feedback LED manquant sur ce chemin de déclenchement
        // Serial.println("Alarm triggered due to signal loss!");
      }
    }
  }
}
