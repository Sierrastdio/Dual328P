/* 
 * ============================================================================
 * Arduino Nano #2 - 16x2 I2C LCD Display Monitor v3.0 (Simplified)
 * ============================================================================
 * 
 * 출력 화면 ASCII 아트:
 *
 * [Main Display]
 * +----------------+
 * |C1:00 11111111 |
 * |C2:FF 10101010 |
 * +----------------+
 *
 * 설명:
 * - C1/C2: 코어 번호
 * - 주소: 16진수 2자리
 * - 데이터: 2진수 8자리
 *
 * ============================================================================
 */

#include <LiquidCrystal_I2C.h>

// ── 16x2 I2C LCD ─────────────────────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ── 74HC165 핀 (포트 직접 접근용) ────────────────────────────────────────────
#define HC165_LOAD_LOW()   PORTC &= ~(1 << 0)
#define HC165_LOAD_HIGH()  PORTC |=  (1 << 0)
#define HC165_CLK_LOW()    PORTC &= ~(1 << 1)
#define HC165_CLK_HIGH()   PORTC |=  (1 << 1)
#define HC165_DATA_READ()  ((PINC >> 2) & 0x01)

// ── 인터럽트 핀 ──────────────────────────────────────────────────────────────
const uint8_t CORE1_SIGNAL = 2;
const uint8_t CORE2_SIGNAL = 3;

// ── 인터럽트 공유 변수 ────────────────────────────────────────────────────────
volatile bool core1_triggered = false;
volatile bool core2_triggered = false;

// ── 디스플레이 상태 ──────────────────────────────────────────────────────────
uint8_t  last_core1_addr = 0, last_core1_data = 0;
uint8_t  last_core2_addr = 0, last_core2_data = 0;

bool system_running = false;
unsigned long last_display_update = 0;
const uint32_t DISPLAY_THROTTLE = 200;

// ── 인터럽트 핸들러 ──────────────────────────────────────────────────────────
void ISR_core1() { core1_triggered = true; }
void ISR_core2() { core2_triggered = true; }

// ── 74HC165 체인 읽기 ────────────────────────────────────────────────────────
uint16_t read74HC165Chain() {
    uint16_t result = 0;

    HC165_LOAD_LOW();
    asm volatile("nop\n\t nop\n\t nop\n\t nop\n\t");
    HC165_LOAD_HIGH();
    asm volatile("nop\n\t nop\n\t nop\n\t nop\n\t");

    for (uint8_t i = 0; i < 16; i++) {
        result = (result << 1) | HC165_DATA_READ();
        HC165_CLK_HIGH();
        asm volatile("nop\n\t nop\n\t");
        HC165_CLK_LOW();
        asm volatile("nop\n\t nop\n\t");
    }

    return result;
}

inline uint8_t extractAddress(uint16_t d) { return (d >> 9) & 0x7F; }
inline uint8_t extractData   (uint16_t d) { return  d       & 0xFF; }

// ── 디스플레이 갱신 ──────────────────────────────────────────────────────────
void updateDisplay() {
    char buf[17];
    
    lcd.setCursor(0, 0);
    sprintf(buf, "C1:%02X %d%d%d%d%d%d%d%d", 
        last_core1_addr,
        (last_core1_data >> 7) & 1,
        (last_core1_data >> 6) & 1,
        (last_core1_data >> 5) & 1,
        (last_core1_data >> 4) & 1,
        (last_core1_data >> 3) & 1,
        (last_core1_data >> 2) & 1,
        (last_core1_data >> 1) & 1,
        last_core1_data & 1
    );
    lcd.print(buf);
    
    lcd.setCursor(0, 1);
    sprintf(buf, "C2:%02X %d%d%d%d%d%d%d%d", 
        last_core2_addr,
        (last_core2_data >> 7) & 1,
        (last_core2_data >> 6) & 1,
        (last_core2_data >> 5) & 1,
        (last_core2_data >> 4) & 1,
        (last_core2_data >> 3) & 1,
        (last_core2_data >> 2) & 1,
        (last_core2_data >> 1) & 1,
        last_core2_data & 1
    );
    lcd.print(buf);
}

// ── 초기화 화면 ──────────────────────────────────────────────────────────────
void showStartup() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Dual-Core");
    lcd.setCursor(0, 1);
    lcd.print("System v3.0");
}

void showIdle() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Waiting for");
    lcd.setCursor(0, 1);
    lcd.print("Program...");
}

// ── setup ────────────────────────────────────────────────────────────────────
void setup() {
    // 74HC165 포트 설정
    DDRC  |=  (1 << 0) | (1 << 1);
    DDRC  &= ~(1 << 2);
    PORTC &= ~(1 << 2);

    HC165_LOAD_HIGH();
    HC165_CLK_LOW();

    // 인터럽트 설정
    pinMode(CORE1_SIGNAL, INPUT_PULLUP);
    pinMode(CORE2_SIGNAL, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(CORE1_SIGNAL), ISR_core1, FALLING);
    attachInterrupt(digitalPinToInterrupt(CORE2_SIGNAL), ISR_core2, FALLING);

    // LCD 초기화
    lcd.init();
    lcd.backlight();

    showStartup();
    
    // 2초 대기
    unsigned long t = millis();
    while (millis() - t < 2000);

    showIdle();
}

// ── loop ─────────────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    // ── 인터럽트 처리 ────────────────────────────────────────────────────────
    if (core1_triggered) {
        core1_triggered = false;
        uint16_t bus = read74HC165Chain();
        last_core1_addr = extractAddress(bus);
        system_running = true;
    }

    if (core2_triggered) {
        core2_triggered = false;
        uint16_t bus = read74HC165Chain();
        last_core2_addr = extractAddress(bus);
        system_running = true;
    }

    // ── 초당 빈도 계산 ────────────────────────────────────────────────────────
    if (now - last_freq_update >= 1000) {
        core1_freq = core1_temp_count;
        core2_freq = core2_temp_count;
        core1_temp_count = 0;
        core2_temp_count = 0;
        last_freq_update = now;
    }

    // ── 디스플레이 갱신 (쓰로틀) ────────────────────────────────────────────
    if (system_running) {
        if (now - last_display_update > DISPLAY_THROTTLE) {
            updateDisplay();
            last_display_update = now;
        }
    }
}

/*
 * ============================================================================
 * 16x2 I2C LCD Wiring
 * ============================================================================
 * 
 * Arduino Nano -> I2C LCD (5V, 주소: 0x27)
 * ----------------------------
 * A4 (SDA) -> LCD SDA
 * A5 (SCL) -> LCD SCL
 * 5V -> LCD VCC
 * GND -> LCD GND
 * 
 * ============================================================================
 * 74HC165 Wiring
 * ============================================================================
 * 
 * Arduino Nano -> 74HC165 #1 (Data Bus D0~D7)
 * A0 (PC0) -> Pin 1 (SH/LD)
 * A1 (PC1) -> Pin 2 (CLK)
 * A2 (PC2) -> Pin 9 (Q7 → input)
 * 
 * 74HC165 #1 Q7 -> 74HC165 #2 Pin 10 (DS)
 * 74HC165 #2 Q7 -> Arduino A2
 * 
 * ============================================================================
 */