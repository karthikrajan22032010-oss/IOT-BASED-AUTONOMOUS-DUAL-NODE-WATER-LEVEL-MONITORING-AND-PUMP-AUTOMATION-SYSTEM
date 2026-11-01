#include <RCSwitch.h>

RCSwitch transmitter;

const int TX_PIN = 4;
const int BUTTON_PIN = 3;

bool previousButton = HIGH;
unsigned long lastPress = 0;

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  transmitter.enableTransmit(TX_PIN);
  transmitter.setRepeatTransmit(5);

  Serial.println("ESP32-C3 Super Mini TX Ready");
}

void loop() {
  bool button = digitalRead(BUTTON_PIN);

  if (previousButton == HIGH &&
      button == LOW &&
      millis() - lastPress > 250) {

    lastPress = millis();

    transmitter.send(12345, 24);

    Serial.println("Sent command: 12345");
  }

  previousButton = button;
  delay(10);
}