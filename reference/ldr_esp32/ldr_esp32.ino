// ESP32 LDR (light-dependent resistor) reference sketch for uncanny-eyes.
//
// Outputs light readings in the same serial protocol as suspect.ino:
//   LDR:<raw>
//
// The desktop app's LDRSerialInput::parseLine() accepts both "LDR:NNN" and
// bare "NNN", but serial_monitor.py requires the "LDR:" prefix. This sketch
// always emits the prefix for compatibility.
//
// ESP32 ADC is 12-bit (0..4095). The uncanny-eyes CLI accepts --ldr-min/--ldr-max
// to match (defaults are 0..4095 for ESP32).

const int LDR_PIN = 34;

void setup() {
    Serial.begin(115200);
}

void loop() {
    int value = analogRead(LDR_PIN);
    Serial.print("LDR:");
    Serial.println(value);
    delay(50);
}
