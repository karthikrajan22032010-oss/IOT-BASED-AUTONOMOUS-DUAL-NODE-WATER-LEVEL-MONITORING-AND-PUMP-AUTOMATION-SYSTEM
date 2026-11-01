#include <RCSwitch.h>

RCSwitch transmitter;

// Matches your working test code (TX on Pin 4)
const int TX_PIN = 4;
const int ledPin = 8; // Onboard Blue LED (Active LOW)

// Water level probe pins (Moved 100% probe to Pin 0 so Pin 4 is free for TX)
const int PROBE_25  = 1;
const int PROBE_50  = 2;
const int PROBE_75  = 3;
const int PROBE_100 = 0;

unsigned long packetCount = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);

  // Configure water level probes
  pinMode(PROBE_25, INPUT_PULLUP);
  pinMode(PROBE_50, INPUT_PULLUP);
  pinMode(PROBE_75, INPUT_PULLUP);
  pinMode(PROBE_100, INPUT_PULLUP);

  // --- EXACT WORKING TEST CODE INITIALIZATION ---
  transmitter.enableTransmit(TX_PIN);
  transmitter.setRepeatTransmit(5);

  Serial.println("ESP32-C3 Super Mini TX Ready");
}

void loop() {
  int water_level = 0;
  unsigned long commandToSend = 12300; // Base command like 12345 (12300 = 0%)

  // Read water probes (Highest wet probe takes priority)
  if (digitalRead(PROBE_100) == LOW) {
    water_level = 100;
    commandToSend = 12400;
  } 
  else if (digitalRead(PROBE_75) == LOW) {
    water_level = 75;
    commandToSend = 12375;
  } 
  else if (digitalRead(PROBE_50) == LOW) {
    water_level = 50;
    commandToSend = 12350;
  } 
  else if (digitalRead(PROBE_25) == LOW) {
    water_level = 25;
    commandToSend = 12325;
  }

  packetCount++;

  // Flash LED and send 24-bit command exactly like your working test code
  digitalWrite(ledPin, LOW);
  transmitter.send(commandToSend, 24);
  digitalWrite(ledPin, HIGH);

  Serial.print("[Packet #");
  Serial.print(packetCount);
  Serial.print("] Water Level: ");
  Serial.print(water_level);
  Serial.print("% -> Sent command: ");
  Serial.println(commandToSend);

  delay(2000); // Send update every 2 seconds
}