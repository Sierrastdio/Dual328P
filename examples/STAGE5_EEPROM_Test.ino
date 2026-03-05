/*
 * ============================================================================
 * STAGE 5 테스트: 28C256 EEPROM 메모리 시스템
 * ============================================================================
 * 목표: 주소 설정 → 데이터 쓰기/읽기 → 페이징 동작 확인
 * 
 * 핀 배치:
 * - PD2~7: 74HC245(Data Buffer) A0~A5 → EEPROM D0~D7
 * - PB0~1: 74HC245(Data Buffer) A6~A7 → EEPROM D6~D7 (데이터)
 * - PB2~5: 주소버퍼 A0~A3 → EEPROM A0~A3
 * - PC0~2: 주소버퍼 A4~A6 → EEPROM A4~A6
 * - PD0: 74HC595 SER (페이징)
 * - PD1: 74HC595 SCK (페이징)
 * - PC3: 74HC595 RCK (페이징)
 * - PC4: 74HC04 입력 (A14 제어) + Core 선택
 * - PC5: 코어 간 핸드셰이크
 * 
 * 28C256 EEPROM 핀:
 * - A0~A13: 주소 입력 (14비트 = 16KB)
 *   A0~A6: 직접 주소 (PB2-5, PC0-2)
 *   A7~A13: 페이징 (74HC595 QA~QG)
 * - D0~D7: 데이터 (양방향, 74HC245를 통해)
 * - OE#: 읽기 허용 (OUTPUT ENABLE)
 * - WE#: 쓰기 허용 (WRITE ENABLE)
 * - CE#: 칩 선택 (CHIP ENABLE)
 * 
 * 메모리 구조:
 * 16KB = 128 pages × 128 bytes/page
 * 
 * 물리 주소 = (페이지 << 7) | 주소
 * 
 * 예:
 * - Page 0, Addr 0~127 → EEPROM[0x0000~0x007F]
 * - Page 1, Addr 0~127 → EEPROM[0x0080~0x00FF]
 * - ...
 * - Page 127, Addr 0~127 → EEPROM[0x3F80~0x3FFF]
 * 
 * 예상 결과:
 * ✓ 0x0000~0x007F 범위의 메모리에 쓰기/읽기 가능
 * ✓ 페이지 변경 후 다른 위치에 쓰기/읽기 가능
 * ✓ 읽은 데이터가 쓴 데이터와 일치
 */

#include <avr/io.h>
#include <util/delay.h>

// EEPROM 메모리 접근 제어
volatile uint8_t eeprom_data = 0;  // 현재 데이터 버스 값

// ============================================================================
// 주소 설정 (A0~A13)
// ============================================================================
void set_address(uint16_t addr) {
    // addr = (page << 7) | (local_addr)
    
    // A0~A6 (7비트): PB2~5, PC0~2
    uint8_t local_addr = addr & 0x7F;
    
    // PB2~5에 A0~A3 설정
    uint8_t pb_val = (PORTB & 0x03) | ((local_addr & 0x0F) << 2);
    PORTB = pb_val;
    
    // PC0~PC2에 A4~A6 설정
    uint8_t pc_val = (PORTC & 0xF8) | ((local_addr >> 4) & 0x07);
    PORTC = pc_val;
    
    // A7~A13 (7비트): 74HC595 QA~QG (페이징)
    uint8_t page = (addr >> 7) & 0x7F;
    shift_register_write(page);
}

// ============================================================================
// 데이터 버스 접근
// ============================================================================

// 데이터 버스를 출력으로 설정 (쓰기 모드)
void set_data_output(uint8_t data) {
    // PD2~7에 D0~D5 설정
    uint8_t pd_val = (PORTD & 0x03) | ((data & 0x3F) << 2);
    PORTD = pd_val;
    
    // PB0~1에 D6~D7 설정
    uint8_t pb_val = (PORTB & 0xFC) | ((data >> 6) & 0x03);
    PORTB = pb_val;
    
    // DDRD, DDRB를 출력으로 설정
    DDRD = 0xFC;  // PD2~7 출력
    DDRB = 0x03;  // PB0~1 출력
    
    eeprom_data = data;
}

// 데이터 버스를 입력으로 설정 (읽기 모드)
uint8_t read_data(void) {
    // DDRD, DDRB를 입력으로 설정
    DDRD = 0x00;  // PD 모두 입력
    DDRB = 0x00;  // PB 모두 입력
    
    _delay_us(1);  // 신호 안정화
    
    // PD2~7에서 D0~D5 읽기
    uint8_t lower = (PIND >> 2) & 0x3F;
    
    // PB0~1에서 D6~D7 읽기
    uint8_t upper = (PINB & 0x03) << 6;
    
    uint8_t data = lower | upper;
    
    // 다시 출력 모드로 설정 (다음 쓰기를 위해)
    DDRD = 0xFC;
    DDRB = 0x03;
    
    eeprom_data = data;
    return data;
}

// ============================================================================
// EEPROM 제어 신호 (WE#, OE#, CE#)
// ============================================================================
// 
// 실제 핀 매핑 (회로도 기준 재확인 필요):
// WE# (Write Enable): (설정 필요)
// OE# (Output Enable): (설정 필요)
// CE# (Chip Enable): (설정 필요)
//
// 아래는 예시 코드입니다. 실제 회로에 맞게 수정하세요.

#define EEPROM_WE  0  // 임시 (실제 핀 설정 필요)
#define EEPROM_OE  1  // 임시
#define EEPROM_CE  2  // 임시

// 쓰기 펄스 생성
void eeprom_write_pulse(void) {
    // WE# = LOW (쓰기 가능)
    // PORT? &= ~(1 << EEPROM_WE);  // 실제로 사용할 포트
    _delay_us(100);  // 쓰기 시간 (28C256: 50ns 이상)
    
    // WE# = HIGH (쓰기 완료)
    // PORTA |= (1 << EEPROM_WE);   // 실제로 사용할 포트
    _delay_us(10);
}

// 읽기 활성화
void eeprom_read_enable(void) {
    // OE# = LOW
    // PORTA &= ~(1 << EEPROM_OE);
    _delay_us(1);  // 읽기 셋업 시간 (28C256: ~70ns)
}

// 읽기 비활성화
void eeprom_read_disable(void) {
    // OE# = HIGH
    // PORTA |= (1 << EEPROM_OE);
}

// ============================================================================
// 74HC595 시프트 레지스터 (페이징)
// ============================================================================
void shift_register_write(uint8_t value) {
    for (int8_t i = 7; i >= 0; i--) {
        if (value & (1 << i)) {
            PORTD |= (1 << 0);    // SER = 1
        } else {
            PORTD &= ~(1 << 0);   // SER = 0
        }
        
        _delay_us(1);
        
        // SCK 펄스
        PORTD |= (1 << 1);        // SCK = 1
        _delay_us(1);
        PORTD &= ~(1 << 1);       // SCK = 0
        _delay_us(1);
    }
    
    // RCK 펄스
    PORTC |= (1 << 3);           // RCK = 1
    _delay_us(1);
    PORTC &= ~(1 << 3);          // RCK = 0
    _delay_us(1);
    
    PORTD &= ~(1 << 0);          // SER = 0
}

// ============================================================================
// 메모리 접근 래퍼
// ============================================================================
void write_byte(uint16_t addr, uint8_t data) {
    set_address(addr);
    set_data_output(data);
    eeprom_write_pulse();
}

uint8_t read_byte(uint16_t addr) {
    set_address(addr);
    eeprom_read_enable();
    uint8_t data = read_data();
    eeprom_read_disable();
    return data;
}

// ============================================================================
// 부팅 및 메인 루프
// ============================================================================
void setup(void) {
    // 포트 초기화
    DDRD = 0xFC;   // PD0,1 제어, PD2~7 데이터
    DDRB = 0x03;   // PB0~1 데이터
    DDRC = 0x1C;   // PC2,3,4 제어
    
    PORTD = 0x00;
    PORTB = 0x00;
    PORTC = 0x00;
}

int main(void) {
    setup();
    
    while (1) {
        // 테스트 1: 한 페이지 범위의 쓰기/읽기
        test_single_page();
        
        // 테스트 2: 여러 페이지 전환
        test_page_switching();
        
        // 테스트 3: 메모리 패턴 (전체 16KB)
        test_memory_pattern();
    }
}

// ============================================================================
// 테스트 함수들
// ============================================================================

// 테스트 1: 페이지 0 (0x0000 ~ 0x007F) 숨 쓰기/읽기
// 예상 결과: addr에 실제 데이터 값을 저장했으므로 읽기 후 일치 확인
void test_single_page(void) {
    // Page 0 설정
    set_address(0);
    
    // 0x00 ~ 0x7F에 데이터 쓰기
    for (uint8_t addr = 0; addr < 128; addr++) {
        write_byte(addr, addr & 0xFF);  // 0x00, 0x01, 0x02, ...
        _delay_ms(1);  // EEPROM 쓰기 시간
    }
    
    // 읽기 검증
    uint8_t error_count = 0;
    for (uint8_t addr = 0; addr < 128; addr++) {
        uint8_t written = addr & 0xFF;
        uint8_t read_val = read_byte(addr);
        
        if (written != read_val) {
            error_count++;
            // 에러 나면 Serial.print() 또는 LED로 표시
        }
        _delay_ms(1);
    }
    
    // 검증 결과 (USB Serial로 출력 필요시)
    // Serial.print("Page 0 Errors: ");
    // Serial.println(error_count);
    
    _delay_ms(2000);  // 다음 테스트 전 대기
}

// 테스트 2: 페이지 전환 (0→1→2...→127)
// 각 페이지에 0xAA or 0x55 패턴 쓰고 검증
void test_page_switching(void) {
    uint8_t pattern_write = 0xAA;  // 1010 1010
    
    // 모든 페이지에 쓰기
    for (uint16_t page = 0; page < 128; page++) {
        set_address(page << 7);  // 페이지 설정 (page * 128)
        
        for (uint8_t addr = 0; addr < 128; addr++) {
            write_byte((page << 7) | addr, pattern_write);
            _delay_us(100);  // EEPROM 쓰기 시간
        }
        
        _delay_ms(10);
    }
    
    // 검증
    uint16_t error_count = 0;
    for (uint16_t page = 0; page < 128; page++) {
        for (uint8_t addr = 0; addr < 128; addr++) {
            uint8_t read_val = read_byte((page << 7) | addr);
            if (read_val != pattern_write) {
                error_count++;
            }
        }
        _delay_ms(1);
    }
    
    // 패턴 변경 (0xAA → 0x55)
    pattern_write ^= 0xFF;
    
    _delay_ms(3000);
}

// 테스트 3: 메모리 패턴 분석 (전체 16KB 접근)
// 각 주소 = 해당 메모리 위치의 값
void test_memory_pattern(void) {
    static uint8_t test_num = 0;
    test_num++;
    
    // 전체 16KB에 증가 패턴 쓰기
    for (uint16_t addr = 0; addr < 16384; addr += 256) {
        uint8_t pattern = (addr >> 8) & 0xFF;  // 상위 바이트
        
        for (uint8_t offset = 0; offset < 128; offset++) {
            write_byte(addr + offset, pattern);
            _delay_us(100);
        }
        
        _delay_ms(1);
    }
    
    // 검증
    uint16_t error_count = 0;
    for (uint16_t addr = 0; addr < 16384; addr += 256) {
        uint8_t expected = (addr >> 8) & 0xFF;
        
        for (uint8_t offset = 0; offset < 128; offset++) {
            uint8_t read_val = read_byte(addr + offset);
            if (read_val != expected) {
                error_count++;
            }
        }
        
        _delay_ms(1);
    }
    
    _delay_ms(5000);
}

/*
 * ============================================================================
 * 로직 애널라이저 및 오실로스코프 확인
 * ============================================================================
 * 
 * 측정 대상:
 * 1. 주소 버스 (A0~A13)
 *    - 어드레스가 올바르게 변하는가?
 *    - 페이지 변경 시 A7~A13 변화?
 * 
 * 2. 데이터 버스 (D0~D7)
 *    - 쓰기: PORTD/PORTB에서 올바른 값?
 *    - 읽기: PIND/PINB에서 메모리 값 반영?
 * 
 * 3. 제어 신호 (WE#, OE#, CE#)
 *    - WE# 펄스 폭: 100ns 이상 (28C256 스펙)
 *    - WE#와 데이터 타이밍 일치?
 *    - OE#과 데이터 읽기 타이밍?
 * 
 * 4. 74HC595 (페이징)
 *    - SER, SCK, RCK 신호 정상?
 *    - QA~QG 출력이 예상한 페이지 값?
 * 
 * ============================================================================
 * 멀티미터 검증
 * ============================================================================
 * 
 * DC 전압:
 * - 28C256 pin 28 (VCC): 5V
 * - 28C256 pin 14 (GND): 0V
 * - A0~A13: 0V 또는 5V (주소에 따라)
 * - D0~D7: 0V 또는 5V (데이터에 따라)
 * 
 * 저항 측정 (전원 OFF):
 * - VCC-GND: 수십 kΩ 이상 (단락 없음 확인)
 * - 각 주소 핀: 저항값 확인 (풀업/풀다운)
 * 
 * ============================================================================
 * 예상 문제 & 해결책
 * ============================================================================
 * 
 * 문제 1: 쓰기는 되지만 읽기가 실패
 * → OE# (읽기 활성화) 신호 확인
 * → 74HC245 방향 제어 (DIR 핀) 확인
 * → DDRD, DDRB 입력 전환 확인
 * 
 * 문제 2: 특정 주소만 오류
 * → 해당 주소의 A0~A13 배선 확인
 * → 74HC595 해당 비트 확인
 * 
 * 문제 3: 페이지 전환 후 메모리 오류
 * → 74HC595 래치 타이밍 확인
 * → 페이지 설정 후 지연 추가 (current: _delay_us(1)→_delay_ms(1))
 * 
 * 문제 4: WE# 펄스가 너무 짧음
 * → eeprom_write_pulse()의 지연 시간 증가
 * → current: _delay_us(100) → _delay_us(1000)
 * 
 * ============================================================================
 * 다음 단계로 전환 조건 (STAGE 6)
 * ============================================================================
 * 
 * ✓ test_single_page() 에러 0
 * ✓ test_page_switching() 에러 0
 * ✓ test_memory_pattern() 에러 0
 * ✓ 로직 애널라이저로 모든 주소/데이터/제어 신호 확인
 * ✓ 페이징 후 다른 메모리 범위 접근 가능
 * 
 * ============================================================================
 * 주의: 실제 회로에 맞게 핀 매핑 수정 필수!
 * ============================================================================
 */
