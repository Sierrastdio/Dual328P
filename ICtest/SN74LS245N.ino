/*=========================================================================
 * 74HC245가 없어 SN74 로 먼저 테스트 했는데 이미 죽어있던건지 코드때문에 죽은건지
 * 그건 모르겠으나 일단 칩 B단에서 전압이 줄줄 새는거 보면 칩은 죽은게 맞는듯.
 * 
 * 
 * 
 * 74LS245   |  NANO
 * -------------------------
 * 1(DIR)   ->  D10
 * 19(OE)   ->  D11
 * 2~9      ->  D2~D9
 * 10(GND)  ->  GND
 * 20(VCC)  ->  VCC
 * 
 * ==========================================================================
*/



// 핀 정의
const int DIR = 10;
const int OE  = 11;
const int DATA_PINS[] = {2, 3, 4, 5, 6, 7, 8, 9}; // Nano D2~D9 -> Transceiver A1~A8

void setup() {
  Serial.begin(9600);
  
  // 제어 핀 설정
  pinMode(DIR, OUTPUT);
  pinMode(OE, OUTPUT);
  
  // 초기 상태: 안전을 위해 버스 차단(HIGH), 방향은 나노->버스(LOW)
  digitalWrite(OE, HIGH);
  digitalWrite(DIR, LOW); 

  // 데이터 핀을 모두 출력으로 설정
  for (int i = 0; i < 8; i++) {
    pinMode(DATA_PINS[i], OUTPUT);
    digitalWrite(DATA_PINS[i], LOW);
  }

  Serial.println("--- SN74LS245 Final Test ---");
  Serial.println("Commands:");
  Serial.println("  O : Open Bus (OE=LOW)");
  Serial.println("  C : Close Bus (OE=HIGH)");
  Serial.println("  W [HEX] : Write Data (Ex: W FF, W 1A)");
  Serial.println("-----------------------------");
}

void loop() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();

    // 버스 열기
    if (cmd == 'O' || cmd == 'o') {
      digitalWrite(OE, LOW);
      Serial.println(">> BUS OPENED (Output Enabled)");
    } 
    // 버스 닫기
    else if (cmd == 'C' || cmd == 'c') {
      digitalWrite(OE, HIGH);
      Serial.println(">> BUS CLOSED (High-Z)");
    } 
    // 데이터 쓰기
    else if (cmd == 'W' || cmd == 'w') {
      String hexString = Serial.readStringUntil('\n');
      hexString.trim(); 

      // 16진수 문자열을 숫자로 변환
      int val = (int)strtol(hexString.c_str(), NULL, 16);

      if (val >= 0 && val <= 255) {
        sendData(val); // 아래에 정의된 함수 호출
        Serial.print(">> Data Set: 0x");
        if(val < 16) Serial.print("0"); // 0x0F 처럼 예쁘게 출력
        Serial.println(val, HEX);
      } else {
        Serial.println(">> Error: Use HEX 00 to FF");
      }
    }
  }
}

// 에러가 났던 바로 그 함수! 실제 핀에 데이터를 쏴주는 로직입니다.
void sendData(byte data) {
  for (int i = 0; i < 8; i++) {
    // data의 i번째 비트가 1인지 0인지 검사해서 핀에 출력
    bool bitVal = (data >> i) & 0x01;
    digitalWrite(DATA_PINS[i], bitVal);
  }
}