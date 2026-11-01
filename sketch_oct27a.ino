#include <RCSwitch.h>

RCSwitch receiver;

void setup() {
  Serial.begin(115200);
  delay(1000);

  receiver.enableReceive(
    digitalPinToInterrupt(27)
  );

  Serial.println("ESP32 Receiver Ready");
  Serial.println("Waiting for commands...");
}

void loop() {
  if (receiver.available()) {
    unsigned long command =
      receiver.getReceivedValue();

    if (command != 0) {
      Serial.print("Received: ");
      Serial.println(command);

      if (command == 12345) {
        Serial.println("BUTTON PRESSED!");
      }
    }

    receiver.resetAvailable();
  }
}