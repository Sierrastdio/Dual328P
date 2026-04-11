/* 
 * ============================================================================
 * Arduino Nano #2 - GLCD Display Monitor v3.0
 * ============================================================================
 * 변경사항 (v2.0 → v3.0):
 * - Core 신호 감지: 폴링 → 하드웨어 인터럽트 (INT0=D2, INT1=D3)
 * - 74HC165 읽기: digitalRead → 직접 포트 접근 (속도 향상)
 * - delay() 완전 제거 → millis() 기반 상태머신
 * - Stats 화면 표시 중에도 신호 캡처 유지
 * ============================================================================
 */

#include <U8g2lib.h>

// ── GLCD ─────────────────────────────────────────────────────────────────────
// CLK=D13(PB5), Data=D11(PB3), CS=D10(PB2), Reset=D8(PB0)
U8G2_ST7920_128X64_1_SW_SPI u8g2(U8G2_R3, 13, 11, 10, 8);

// ── 74HC165 핀 (포트 직접 접근용) ────────────────────────────────────────────
// A0 = PC0 (SH/LD), A1 = PC1 (CLK), A2 = PC2 (DATA)
#define HC165_LOAD_LOW()   PORTC &= ~(1 << 0)
#define HC165_LOAD_HIGH()  PORTC |=  (1 << 0)
#define HC165_CLK_LOW()    PORTC &= ~(1 << 1)
#define HC165_CLK_HIGH()   PORTC |=  (1 << 1)
#define HC165_DATA_READ()  ((PINC >> 2) & 0x01)   // PC2

// ── 인터럽트 핀 ──────────────────────────────────────────────────────────────
// D2 = INT0 (Core 1), D3 = INT1 (Core 2)
const uint8_t CORE1_SIGNAL = 2;
const uint8_t CORE2_SIGNAL = 3;

// ── 인터럽트 공유 변수 ────────────────────────────────────────────────────────
volatile bool core1_triggered = false;
volatile bool core2_triggered = false;

// ── 디스플레이 상태 ──────────────────────────────────────────────────────────
uint8_t  last_core1_addr = 0, last_core1_data = 0;
uint8_t  last_core2_addr = 0, last_core2_data = 0;
uint32_t core1_count = 0, core2_count = 0;

bool system_running       = false;
bool display_needs_update = false;

// Stats 화면 상태머신
bool          stats_showing  = false;
unsigned long stats_show_at  = 0;   // stats 보이기 시작한 시각
const uint32_t STATS_DURATION = 3000;
const uint32_t STATS_INTERVAL = 15000;
unsigned long last_stats_time = 0;

// 디스플레이 갱신 쓰로틀
unsigned long last_display_update = 0;
const uint32_t DISPLAY_THROTTLE = 200;   // ms

// ── 인터럽트 핸들러 ──────────────────────────────────────────────────────────
// FALLING: 74HC138 출력이 Active LOW이므로 LOW로 떨어질 때 캡처
void ISR_core1() { core1_triggered = true; }
void ISR_core2() { core2_triggered = true; }

// ── 74HC165 체인 읽기 (포트 직접 접근) ───────────────────────────────────────
// 체인 순서: #1(Data D0~D7) Q7 → #2(Addr A0~A6) DS → Q7 → Nano A2
// 시프트 아웃 순서: #2 MSB 먼저, #1 MSB 나중
// result[15:9] = A0~A6, result[7:0] = D0~D7
uint16_t read74HC165Chain() {
    uint16_t result = 0;

    // 1. 병렬 로드 (Active LOW 펄스)
    HC165_LOAD_LOW();
    asm volatile("nop\n\t nop\n\t nop\n\t nop\n\t");  // ~250ns
    HC165_LOAD_HIGH();
    asm volatile("nop\n\t nop\n\t nop\n\t nop\n\t");

    // 2. 16비트 시프트
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

// ── 디스플레이 함수 ──────────────────────────────────────────────────────────
void drawDisplay() {
    u8g2.firstPage();
    do {
        char buf[22];

        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(0, 8, "Dual-Core Monitor");
        u8g2.drawHLine(0, 10, 128);

        u8g2.drawStr(0, 20, "Core 1:");
        u8g2.setFont(u8g2_font_5x7_tf);
        sprintf(buf, "Addr: 0x%02X", last_core1_addr);
        u8g2.drawStr(8, 28, buf);
        sprintf(buf, "Data: 0x%02X (%d)", last_core1_data, last_core1_data);
        u8g2.drawStr(8, 36, buf);

        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(0, 46, "Core 2:");
        u8g2.setFont(u8g2_font_5x7_tf);
        sprintf(buf, "Addr: 0x%02X", last_core2_addr);
        u8g2.drawStr(8, 54, buf);
        sprintf(buf, "Data: 0x%02X (%d)", last_core2_data, last_core2_data);
        u8g2.drawStr(8, 62, buf);

    } while (u8g2.nextPage());
}

void drawWelcome() {
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_9x15_tf);
        u8g2.drawStr(10, 25, "Dual-Core");
        u8g2.drawStr(8, 45, "System v3.0");
    } while (u8g2.nextPage());
}

void drawStats() {
    u8g2.firstPage();
    do {
        char buf[20];
        u8g2.setFont(u8g2_font_9x15_tf);
        u8g2.drawStr(20, 15, "Statistics");
        u8g2.setFont(u8g2_font_6x10_tf);
        sprintf(buf, "Total: %lu", core1_count + core2_count);
        u8g2.drawStr(10, 35, buf);
        sprintf(buf, "Core 1: %lu", core1_count);
        u8g2.drawStr(10, 48, buf);
        sprintf(buf, "Core 2: %lu", core2_count);
        u8g2.drawStr(10, 61, buf);
    } while (u8g2.nextPage());
}

void drawIdleScreen() {
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(15, 30, "Waiting for");
        u8g2.drawStr(10, 45, "Program Start...");
    } while (u8g2.nextPage());
}

// ── setup ────────────────────────────────────────────────────────────────────
void setup() {
    // 74HC165 핀 설정 (포트 직접)
    DDRC  |=  (1 << 0) | (1 << 1);   // A0(LOAD), A1(CLK) → 출력
    DDRC  &= ~(1 << 2);               // A2(DATA) → 입력
    PORTC &= ~(1 << 2);               // 풀업 없음

    HC165_LOAD_HIGH();
    HC165_CLK_LOW();

    // Core 신호 핀: 입력 풀업 + 하드웨어 인터럽트
    pinMode(CORE1_SIGNAL, INPUT_PULLUP);
    pinMode(CORE2_SIGNAL, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(CORE1_SIGNAL), ISR_core1, FALLING);
    attachInterrupt(digitalPinToInterrupt(CORE2_SIGNAL), ISR_core2, FALLING);

    u8g2.begin();
    u8g2.setContrast(128);

    drawWelcome();
    // delay 대신 millis로 2초 대기하되 인터럽트는 계속 작동
    unsigned long t = millis();
    while (millis() - t < 2000);

    drawIdleScreen();
}

// ── loop ─────────────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    // ── 인터럽트 플래그 처리 ─────────────────────────────────────────────────
    // 인터럽트가 발생한 시점의 버스 값을 읽음
    // (신호가 아직 LOW인 동안 읽으므로 타이밍상 유효)
    if (core1_triggered) {
        core1_triggered = false;
        uint16_t bus = read74HC165Chain();
        last_core1_addr = extractAddress(bus);
        last_core1_data = extractData(bus);
        core1_count++;
        system_running       = true;
        display_needs_update = true;
    }

    if (core2_triggered) {
        core2_triggered = false;
        uint16_t bus = read74HC165Chain();
        last_core2_addr = extractAddress(bus);
        last_core2_data = extractData(bus);
        core2_count++;
        system_running       = true;
        display_needs_update = true;
    }

    // ── Stats 상태머신 (delay 없이) ──────────────────────────────────────────
    if (!stats_showing) {
        // 15초마다, 출력 데이터가 있을 때만 Stats 표시
        if ((now - last_stats_time > STATS_INTERVAL) &&
            (core1_count + core2_count > 0)) {
            drawStats();
            stats_showing    = true;
            stats_show_at    = now;
            last_stats_time  = now;
        }
    } else {
        // 3초 경과 → 일반 화면 복귀
        if (now - stats_show_at > STATS_DURATION) {
            stats_showing        = false;
            display_needs_update = true;   // 즉시 메인 화면 재그림
        }
    }

    // ── 디스플레이 갱신 (Stats 중에는 갱신 생략) ─────────────────────────────
    if (!stats_showing) {
        bool throttle_ok = (now - last_display_update > DISPLAY_THROTTLE);

        if (display_needs_update && throttle_ok) {
            if (system_running) drawDisplay();
            else                drawIdleScreen();
            display_needs_update = false;
            last_display_update  = now;
        }

        // 500ms 주기 강제 갱신 (신호 없어도 화면 유지)
        if (system_running && (now - last_display_update > 500)) {
            drawDisplay();
            last_display_update = now;
        }
    }
}

/*
 * ============================================================================
 * GLCD ST7920 Pinout (Software SPI)
 * ============================================================================
 * 
 * Arduino Nano -> ST7920
 * ----------------------------
 * repo: Arduino-Computer branch: TES-2XO Images/Circuit/glcdct.png
 * 
 * ============================================================================
 * 74HC165 Wiring
 * ============================================================================
 * 
 * 74HC165 #1 (Data Bus D0~D7):
 *   Pin 1  (SH/LD)  -> Nano A0
 *   Pin 2  (CLK)    -> Nano A1
 *   Pin 9  (Q7)     -> 74HC165 #2 Pin 10 (DS)
 *   Pin 11-14, 3-6  -> 28C256 D0~D7
 *   Pin 15 (CE)     -> GND (always enabled)
 * 
 * 74HC165 #2 (Address Bus A0~A6):
 *   Pin 1  (SH/LD)  -> Nano A0
 *   Pin 2  (CLK)    -> Nano A1
 *   Pin 9  (Q7)     -> Nano A2
 *   Pin 10 (DS)     -> 74HC165 #1 Pin 9
 *   Pin 11-14, 3-6  -> 28C256 A0~A6
 *   Pin 7  (A7)     -> GND (unused)
 *   Pin 15 (CE)     -> GND (always enabled)
 * 
 * ============================================================================
 */