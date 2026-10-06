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

RF24 radio(CE_PIN, CSN_PIN); // Create a RF24 object

const byte address[6] = "1234"; // Address for communication

int state = 0; // Internal state 0: armed, 1: secured

int relay_state = RELAY_STOP;

bool msg = false; // Alarm message

bool radioOK = false;

const unsigned long RADIO_INIT_TIMEOUT = 5000;

// Reset button parameters (avec debounce logiciel)
const unsigned long DEBOUNCE_DELAY = 30;
unsigned long lastButtonChange = 0;
bool lastRawButtonState = HIGH;
bool buttonState = HIGH;

bool buttonPressed = false;                 // Is the button currently pressed
unsigned long buttonPressTime = 0;          // Time when button was pressed
const unsigned long RESET_HOLD_TIME = 3000; // Time in ms to hold button to reset

// Leaky bucket parameters
const int BUCKET_CAPACITY = 8;     // Bucket capacity before triggering alarm
int bucketLevel = BUCKET_CAPACITY; // Current bucket level (starts full)
unsigned long lastLeakTime = 0;    // Last time the bucket leaked
const long LEAK_INTERVAL = 150;    // Bucket loses 1 point every leak interval (ms)

// Heartbeat LED (etat RUN uniquement) : un blink prouve que loop() tourne
// toujours, contrairement a un bleu fixe qui pourrait masquer un freeze.
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
  // La couleur/pulse est ensuite geree par le heartbeat dans loop().
  heartbeatOn = true;
  lastHeartbeat = millis();
  setLed(0, 0, 255);
}

void setRelayStop() {
  relay_state = RELAY_STOP;
  digitalWrite(RELAY_PIN, relay_state);
  setLed(255, 0, 0); // fixe : un etat secure qui clignote preterait a confusion
}

void setup() {
  // Watchdog desactive au tout debut au cas ou un reset precedent l'aurait
  // laisse actif (evite une boucle de reset si le watchdog etait deja arme).
  wdt_disable();

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  // setup led
  led.begin();
  led.setBrightness(50); // 0-255
  led.show();

  setRelayStop();

  EEPROM.get(EEPROM_ADDRESS, ChannelNumber);
  if (ChannelNumber < 0 || ChannelNumber > 125) { // Validate channel
    ChannelNumber = 110;                          // Default channel
    EEPROM.put(EEPROM_ADDRESS, ChannelNumber);
  }

  // Check Startup mode
  // Rappel : ne brancher le PC / ouvrir un moniteur serie que dans ce mode
  // de setup ou en dev. En usage normal (sans hote USB connecte), garder
  // Serial.begin() actif ne pose pas de probleme de reset DTR.
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    // Relais deja en STOP par defaut ci-dessus, on le confirme.
    setRelayStop();
    Serial.begin(115200);
    setLed(255, 255, 0);
    while (!Serial) {
      // some boards need to wait to ensure access to serial over USB
    }
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
        // wait for user input
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
    Serial.end(); // End serial communication to save power
  }

  // Initialize nRF24L01 avec timeout borne.
  // Si la radio ne repond pas dans le temps imparti, on ne reste pas
  // bloque indefiniment : on sort et on securise explicitement le systeme
  // plus bas (radioOK restera false).
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

    // Radio confirmee OK : on autorise le robot.
    state = 0;
    setRelayRun();
  } else {
    // Timeout atteint sans radio fonctionnelle : on ne bouge pas de STOP,
    // on passe directement en etat securise.
    state = 1;
    setRelayStop();
  }

  lastLeakTime = millis();

  // Watchdog arme en tout dernier, une fois l'init terminee. Si loop() ne
  // revient jamais (hang materiel/logiciel), la carte reset au bout d'1s :
  // le relais repasse par setup() -> STOP par defaut -> radio revalidee
  // avant toute reautorisation du robot.
  wdt_enable(WDTO_1S);
}

void loop() {
  wdt_reset(); // preuve que loop() tourne toujours, en tout premier

  uint8_t pipe;

  // Si la radio n'a jamais pu s'initialiser, on reste bloque en securise.
  // Le bouton reset ne doit pas pouvoir reactiver le robot sans radio OK.
  if (!radioOK) {
    return;
  }

  // Heartbeat visuel : ne clignote que quand le systeme est arme (RUN),
  // pour distinguer un fonctionnement normal d'un freeze silencieux.
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
