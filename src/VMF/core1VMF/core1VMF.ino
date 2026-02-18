/*
 * ============================================================================
 * Core 1 - Virtual Machine Firmware v4.3 (핸드셰이크 추가)
 * ============================================================================
 * 핀 배치:
 * - PD2~7, PB0~1: 데이터 버스 (D0~D7) → 74HC245 데이터버퍼#1
 * - PB2~5, PC0~2: 주소 버스 (A0~A6) → 74HC245 주소버퍼#1
 * - PD0: 74HC595-Core1 SER (Serial Data)
 * - PD1: 74HC595-Core1 SCK (Shift Clock)
 * - PC3: 74HC595-Core1 RCK (Latch Clock)
 * - PC4: Core Select (0=Core1 Active) + EEPROM A14
 * - PC5: Core 2 완료 신호 입력 (LOW=작업중, HIGH=완료)
 * 
 * 페이징 시스템:
 * - 74HC595로 A7~A13 제어 (7비트)
 * - 128페이지 × 128바이트 = 16KB 접근 가능
 * - slots[15] = PAGE_REG (페이지 전용 레지스터)
 * 
 * 핸드셰이크:
 * - Core 2가 PC5로 완료 신호 전송
 * - Core 1은 PC5를 폴링하여 Core 2 완료 감지
 * ============================================================================
 */

#include <avr/io.h>
#include <util/delay.h>

// Instruction Set
#define OP_NOP      0x00
#define OP_LOAD     0x10
#define OP_ADD      0x20
#define OP_SUB      0x30
#define OP_MUL      0x40
#define OP_AND      0x50
#define OP_OR       0x60
#define OP_OUT      0x70
#define OP_FETCH    0x80
#define OP_SLOT     0x90
#define OP_PUSH     0xA0
#define OP_POP      0xB0
#define OP_SETPAGE  0xE0
#define OP_HALT     0xF0

// VM State
volatile uint8_t regA = 0x00;
volatile uint8_t slots[16];
volatile uint8_t stack[8];
volatile uint8_t stack_ptr = 0;
volatile uint8_t PC = 0;
volatile uint8_t current_page = 0;
volatile bool halted = false;

// Page Cache
volatile uint8_t cached_page = 0xFF;

// PAGE_REG
#define PAGE_REG slots[15]

// Hardware Control Macros
#define ACTIVATE_CORE1()    PORTC &= ~(1 << 4)  // PC4 = 0
#define RELEASE_TO_CORE2()  PORTC |=  (1 << 4)  // PC4 = 1

// Core 2 완료 신호 읽기
#define IS_CORE2_DONE()     (PINC & (1 << 5))   // PC5 = HIGH이면 완료

// 74HC595 Control
#define HC595_SER_HIGH()    PORTD |=  (1 << 0)  // PD0 = 1
#define HC595_SER_LOW()     PORTD &= ~(1 << 0)  // PD0 = 0
#define HC595_SCK_HIGH()    PORTD |=  (1 << 1)  // PD1 = 1
#define HC595_SCK_LOW()     PORTD &= ~(1 << 1)  // PD1 = 0
#define HC595_RCK_HIGH()    PORTC |=  (1 << 3)  // PC3 = 1
#define HC595_RCK_LOW()     PORTC &= ~(1 << 3)  // PC3 = 0

#define SYNC_DELAY()        asm volatile("nop\n\t nop\n\t nop\n\t nop\n\t")

/*
 * ============================================================================
 * 74HC595 페이지 설정
 * ============================================================================
 */
void set_page_595(uint8_t page) {
    page &= 0b01111111;

    if(page == cached_page) return;

    cached_page = page;

    HC595_RCK_LOW();

    for(uint8_t i = 0; i < 7; i++) {
        if(page & 0b01000000) {
            HC595_SER_HIGH();
        } else {
            HC595_SER_LOW();
        }

        HC595_SCK_HIGH();
        HC595_SCK_LOW();

        page <<= 1;
    }

    HC595_RCK_HIGH();
    asm volatile("nop\n\t");
    HC595_RCK_LOW();
}

/*
 * ============================================================================
 * 주소 버스 설정
 * ============================================================================
 */
inline void set_addr_bus(uint8_t addr) {
    addr &= 0b01111111;

    PORTB = (PORTB & 0b11000011) | ((addr & 0b00001111) << 2);
    PORTC = (PORTC & 0b11111000) | ((addr >> 4) & 0b00000111);
}

/*
 * ============================================================================
 * 데이터 버스 입출력
 * ============================================================================
 */
inline void set_data_output() {
    DDRD |= 0b11111100;
    DDRB |= 0b00000011;
}

inline void set_data_input() {
    DDRD &= 0b00000011;
    PORTD &= 0b00000011;
    DDRB &= 0b11111100;
    PORTB &= 0b11111100;
}

inline void write_data_bus(uint8_t data) {
    PORTD = (PORTD & 0b00000011) | ((data << 2) & 0b11111100);
    PORTB = (PORTB & 0b11111100) | ((data >> 6) & 0b00000011);
}

inline uint8_t read_data_bus() {
    return ((PIND & 0b11111100) >> 2) | ((PINB & 0b00000011) << 6);
}

/*
 * ============================================================================
 * High-Z 상태
 * ============================================================================
 */
inline void set_high_z() {
    DDRD &= 0b00000011;
    PORTD &= 0b00000011;
    DDRB &= 0b11111100;
    PORTB &= 0b11111100;

    DDRB &= 0b11000011;
    PORTB &= 0b11000011;
    DDRC &= 0b11111000;
    PORTC &= 0b11111000;
}

/*
 * ============================================================================
 * Fetch
 * ============================================================================
 */
uint8_t fetch() {
    ACTIVATE_CORE1();
    set_data_input();

    DDRB |= 0b00111100;  // PB2~5
    DDRC |= 0b00000111;  // PC0~2

    set_addr_bus(PC);

    SYNC_DELAY();
    _delay_us(5);

    return read_data_bus();
}

/*
 * ============================================================================
 * Output
 * ============================================================================
 */
void output_register(uint8_t value) {
    set_data_output();

    write_data_bus(value);

    SYNC_DELAY();
    _delay_us(50);

    set_data_input();
}

/*
 * ============================================================================
 * Core 2 완료 대기
 * ============================================================================
 */
inline void wait_for_core2() {
    // Core 2가 완료 신호(HIGH)를 보낼 때까지 대기
    while(!IS_CORE2_DONE()) {
        _delay_us(1);
    }
}

/*
 * ============================================================================
 * Execute
 * ============================================================================
 */
void execute(uint8_t instruction) {
    uint8_t opcode  = instruction & 0xF0;
    uint8_t operand = instruction & 0x0F;

    switch(opcode) {
        case OP_NOP:
            break;

        case OP_LOAD:
            regA = operand;
            break;

        case OP_ADD:
            regA += operand;
            break;

        case OP_SUB:
            regA -= operand;
            break;

        case OP_MUL:
            regA *= operand;
            break;

        case OP_AND:
            regA &= operand;
            break;

        case OP_OR:
            regA |= operand;
            break;

        case OP_OUT:
            output_register(regA);
            break;

        case OP_FETCH:
            regA = slots[operand & 0x0F];
            break;

        case OP_SLOT:
            slots[operand & 0x0F] = regA;
            break;

        case OP_PUSH:
            if(stack_ptr < 8) {
                stack[stack_ptr++] = regA;
            }
            break;

        case OP_POP:
            if(stack_ptr > 0) {
                regA = stack[--stack_ptr];
            }
            break;

        case OP_SETPAGE:
            current_page = PAGE_REG & 0b01111111;
            PC = 0;
            set_page_595(current_page);
            break;

        case OP_HALT:
            halted = true;
            break;

        default:
            break;
    }
}

void setup() {
    set_data_input();

    DDRB |= 0b00111100;  // PB2~5 출력
    DDRC |= 0b00000111;  // PC0~2 출력

    DDRD |= 0b00000011;  // PD0~1 출력 (595)
    DDRC |= 0b00001000;  // PC3 출력 (595 RCK)
    DDRC |= 0b00010000;  // PC4 출력 (Core Select)
    DDRC &= ~0b00100000; // PC5 입력 (Core 2 완료 신호) ← 입력!
    PORTC &= ~(1 << 5);  // 풀업 비활성화

    ACTIVATE_CORE1();

    regA = 0x00;
    PC = 0;
    stack_ptr = 0;
    current_page = 0;
    cached_page = 0xFF;
    halted = false;

    for(uint8_t i = 0; i < 16; i++) slots[i] = 0;
    for(uint8_t i = 0; i < 8; i++) stack[i] = 0;

    PAGE_REG = 0;
    set_page_595(0);

    _delay_ms(100);
}

void loop() {
    if(halted) {
        set_high_z();
        RELEASE_TO_CORE2();
        _delay_ms(100);
        return;
    }

    // Fetch-Decode-Execute
    uint8_t instruction = fetch();
    execute(instruction);

    // PC 증가
    PC++;
    if(PC >= 128) {
        PC = 0;
    }

    // 버스 반납 (Core 2에게 양보)
    set_high_z();
    RELEASE_TO_CORE2();

    // Core 2 완료 대기
    wait_for_core2();

    // 버스 재획득
    ACTIVATE_CORE1();

    _delay_us(10);
}

/*
 * ============================================================================
 * 핸드셰이크 프로토콜
 * ============================================================================
 *
 * Core 1의 동작:
 * 1. ACTIVATE_CORE1() - 버스 획득
 * 2. Fetch-Execute 수행
 * 3. RELEASE_TO_CORE2() - 버스 양보
 * 4. wait_for_core2() - Core 2 완료 대기
 * 5. ACTIVATE_CORE1() - 버스 재획득
 *
 * Core 2 신호 (PC5):
 * - LOW  : 작업 중 (대기 필요)
 * - HIGH : 완료 (버스 재획득 가능)
 *
 * 핀 연결:
 * - Core 2 PC5 (출력) → Core 1 PC5 (입력)
 *
 * ============================================================================
 * PAGE_REG 사용 예시
 * ============================================================================
 *
 * ; 페이지 1 이동 (단순)
 * LOAD 1
 * SLOT 15     ; PAGE_REG = 1
 * SETPAGE     ; 1페이지로 전환
 *
 * ; 페이지 100 이동 (연산)
 * LOAD 10
 * MUL 10      ; regA = 100
 * SLOT 15     ; PAGE_REG = 100
 * LOAD 5      ; regA 자유롭게 사용 가능
 * SETPAGE     ; 100페이지로 전환
 *
 * ; 페이지 127 이동 (최대)
 * LOAD 15
 * MUL 8       ; regA = 120
 * ADD 7       ; regA = 127
 * SLOT 15     ; PAGE_REG = 127
 * SETPAGE     ; 127페이지로 전환
 *
 * ============================================================================
 * slots 사용 가능 범위
 * ============================================================================
 * - slots[0]  ~ slots[14]: 일반 변수 (15개)
 * - slots[15]: PAGE_REG 예약 (페이지 전환 전용)
 * ============================================================================
 */