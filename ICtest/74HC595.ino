/*=========================================================================
 * 74HC595 시프트 레지스터 테스트 코드.
 * 나노의 5V출력 전압(4.6V)가 칩 곳곳(1번, 15번핀 등)에서 잘 감지됨.
 * 얜 안망가진듯?
 * 
 * 74HC595   |  NANO
 * -------------------------
 * 16(VCC)  ->  VCC
 * 8(GND)   ->  GND
 * 10(SRCLR)->  5V
 * 13(OE)   ->  GND
 * 14(DS)   ->  D12
 * 11(SHCP) ->  D13
 * 12(STCP) ->  A0
 * 
 * =========================================================================
*/
#define DATA_PIN 12   // DS
#define LATCH_PIN 14  // STCP (A0)
#define CLOCK_PIN 13  // SHCP

void setup() {
  pinMode(DATA_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  Serial.begin(9600);
}

void loop() {
  for (int i = 0; i < 256; i++) {
    digitalWrite(LATCH_PIN, LOW);
    shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, i);
    digitalWrite(LATCH_PIN, HIGH);
    
    Serial.print("Current Address: ");
    Serial.println(i);
    delay(500);
  }
}
