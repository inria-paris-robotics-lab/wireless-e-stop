#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <RF24.h>
#include <SPI.h>
#include <avr/wdt.h>
#include <nRF24L01.h>

#define CE_PIN 10
#define CSN_PIN 9
#define RESET_BUTTON_PIN 3
#define RELAY_PIN 5
#define LED_PIN 2
#define LED_COUNT 1

// IMPORTANT : This relay logic is made for robot on COM + NC
// RELAY_RUN  = Open COM-NC  -> normal state
// RELAY_STOP = Close COM-NC  -> trigger emergency stop on the robot

#define RELAY_RUN LOW
#define RELAY_STOP HIGH

int EEPROM_ADDRESS = 0; // Address to store the channel
int ChannelNumber = 0;  // Variable to hold the channel number

RF24 radio(CE_PIN, CSN_PIN);

const byte address[6] = "1234"; // Address for communication

int state = 0; // Internal state 0: armed, 1: secured

int relay_state = RELAY_STOP;

bool msg = false; // Alarm message

bool radioOK = false;

const unsigned long RADIO_INIT_TIMEOUT = 5000;

// Reset button parameters
const unsigned long DEBOUNCE_DELAY = 30;
unsigned long lastButtonChange = 0;
bool lastRawButtonState = HIGH;
bool buttonState = HIGH;

bool buttonPressed = false;
unsigned long buttonPressTime = 0;
const unsigned long RESET_HOLD_TIME = 3000; // Time in ms to hold button to reset

// Leaky bucket parameters
const int BUCKET_CAPACITY = 8;     // Bucket capacity before triggering alarm
int bucketLevel = BUCKET_CAPACITY; // Current bucket level (starts full)
unsigned long lastLeakTime = 0;    // Last time the bucket leaked
const long LEAK_INTERVAL = 150;    // Bucket loses 1 point every leak interval (ms)

// Heartbeat LED
unsigned long lastHeartbeat = 0;
bool heartbeatOn = false;
const unsigned long HEARTBEAT_INTERVAL = 500;

// Signal LED
Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
void setLed(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

bool readButtonDebounced() {
  bool raw = digitalRead(RESET_BUTTON_PIN);
  if (raw != lastRawButtonState) {
    lastButtonChange = millis();
    lastRawButtonState = raw;
  }
  if (millis() - lastButtonChange > DEBOUNCE_DELAY) {
    buttonState = raw;
  }
  return buttonState;
}

void setRelayRun() {
  relay_state = RELAY_RUN;
  digitalWrite(RELAY_PIN, relay_state);
  heartbeatOn = true;
  lastHeartbeat = millis();
  setLed(0, 0, 255);
}

void setRelayStop() {
  relay_state = RELAY_STOP;
  digitalWrite(RELAY_PIN, relay_state);
  setLed(255, 0, 0);
}

void setup() {
  wdt_disable();

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  // setup led
  led.begin();
  led.setBrightness(50);

  setRelayStop();

  EEPROM.get(EEPROM_ADDRESS, ChannelNumber);
  if (ChannelNumber < 0 || ChannelNumber > 125) { // Validate channel
    ChannelNumber = 110;                          // Default channel
    EEPROM.put(EEPROM_ADDRESS, ChannelNumber);
  }

  // Check Startup mode
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    // Set relay stop to avoid robto working during setup
    setRelayStop();
    Serial.begin(115200);
    setLed(255, 255, 0);

    Serial.println("Mode Setup");
    Serial.print("Current Channel set to: ");
    Serial.println(ChannelNumber);
    Serial.println("Please enter the new Channel (0-125, you should use high values to avoid WiFi "
                   "interference): ");
    while (!Serial.available()) {
      // wait for user input
    }
    int newChannel = Serial.parseInt();
    while (!(newChannel >= 0 && newChannel <= 125)) {
      Serial.println("Invalid input. Please enter a valid channel (0-125): ");
      while (!Serial.available()) {
      }
      newChannel = Serial.parseInt();
    }
    if (newChannel != ChannelNumber) {
      Serial.print("Default Channel set to: ");
      Serial.println(newChannel);
      EEPROM.put(EEPROM_ADDRESS, newChannel);
      ChannelNumber = newChannel;
    }
    Serial.println("Setup channel done.");
    while (digitalRead(RESET_BUTTON_PIN) == LOW) {
      delay(100);
    }
    Serial.end();
  }

  // Initialize nRF24L01
  unsigned long radioInitStart = millis();
  bool blink = false;
  while (!radioOK && (millis() - radioInitStart < RADIO_INIT_TIMEOUT)) {
    radioOK = radio.begin();
    if (!radioOK) {
      if (blink)
        setLed(255, 150, 0);
      else
        setLed(0, 0, 0);
      blink = !blink;
      delay(200);
    }
  }

  if (radioOK) {
    radio.setPALevel(RF24_PA_MAX);     // Set the maximum propagation distance
    radio.setPayloadSize(sizeof(msg)); // Set the payload size (help to speed up communication)
    radio.setChannel(ChannelNumber);   // Set the channel from storage
    radio.setDataRate(RF24_1MBPS);     // Set data rate
    radio.openReadingPipe(1, address); // Set the address for communication
    radio.startListening();            // Set the module as receiver
    state = 0;
    setRelayRun();
  } else {
    state = 1;
    setRelayStop();
  }

  lastLeakTime = millis();

  wdt_enable(WDTO_1S);
}

void loop() {
  wdt_reset();

  uint8_t pipe;

  if (!radioOK) {
    return;
  }

  if (state == 0 && millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
    lastHeartbeat = millis();
    heartbeatOn = !heartbeatOn;
    setLed(0, 0, heartbeatOn ? 255 : 60);
  }

  // Handle reset button when system is secured
  if (state == 1) {
    if (readButtonDebounced() == LOW) {
      if (!buttonPressed) {
        buttonPressed = true;
        buttonPressTime = millis();
      } else {
        if (millis() - buttonPressTime >= RESET_HOLD_TIME) { // If button held long enough
          // Reset the system (radioOK deja garanti true a ce point)
          state = 0; // Change state to armed
          setRelayRun();
          bucketLevel = BUCKET_CAPACITY; // Reset bucket level
          lastLeakTime = millis();       // Reset leak timer
          msg = false;
          buttonPressed = false;
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
    state = 1;             // Change state to secured
    setRelayStop();
  } else {
    if (state == 0) {
      // No data received
      if (millis() - lastLeakTime > LEAK_INTERVAL) {
        bucketLevel = max(bucketLevel - 1, 0); // Decrease bucket level
        lastLeakTime = millis();               // Update last leak time
      }
      // Check if bucket level is empty
      if (bucketLevel == 0 && state == 0) {
        state = 1;
        setRelayStop();
      }
    }
  }
}
