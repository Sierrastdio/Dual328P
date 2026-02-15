/*
 * ============================================================================
 * Core 1 - Virtual Machine Firmware v4.0 (74HC595 Paging)
 * ============================================================================
 * 핀 배치:
 * - PD2~7, PB0~1: 데이터 버스 (D0~D7) → 74HC245 데이터버퍼#1
 * - PB2~5, PC0~2: 주소 버스 (A0~A6) → 74HC245 주소버퍼#1
 * - PD0: 74HC595-Core1 SER (Serial Data)
 * - PD1: 74HC595-Core1 SCK (Shift Clock)
 * - PC3: 74HC595-Core1 RCK (Latch Clock)
 * - PC4: Core Select (0=Core1 Active) + EEPROM A14 → 74HC04, 28C256
 * - PC5: DIR (모든 74HC245 방향 제어)
 * 
 * 페이징 시스템:
 * - 74HC595로 A7~A13 제어 (7비트)
 * - 128페이지 × 128바이트 = 16KB 접근 가능
 * - 페이지 캐싱으로 성능 최적화
 * - PC4로 Bank 선택 (Bank 0/1)
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
volatile uint8_t slots[16];        // 16 variable slots
volatile uint8_t stack[8];         // 8-deep stack
volatile uint8_t stack_ptr = 0;    // Stack pointer (SP 충돌 회피)
volatile uint8_t PC = 0;           // 페이지 내 오프셋 (0~127)
volatile uint8_t current_page = 0; // 현재 페이지 (0~127)
volatile bool halted = false;

// Page Cache
volatile uint8_t cached_page = 0xFF; // 캐시된 페이지 번호

// Hardware Control Macros
#define ACTIVATE_CORE1()    PORTC &= ~(1 << 4)  // PC4 = 0
#define RELEASE_TO_CORE2()  PORTC |=  (1 << 4)  // PC4 = 1
#define SET_BUS_READ()      PORTC &= ~(1 << 5)  // PC5 = 0
#define SET_BUS_WRITE()     PORTC |=  (1 << 5)  // PC5 = 1

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
 * 74HC595 페이지 설정 (A7~A13, 7비트)
 * ============================================================================
 */
inline void set_page_595(uint8_t page) {
    // 페이지 캐싱
    if(page == cached_page) return;
    
    page &= 0b01111111;  // 7비트 마스크
    
    HC595_RCK_LOW();
    
    // 7비트 시리얼 전송 (MSB first)
    for(uint8_t i = 0; i < 7; i++) {
        if(page & 0b01000000) {  // 최상위 비트 체크
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
    
    cached_page = page >> 1;
}

/*
 * ============================================================================
 * 주소 버스 설정 (A0~A6, 7비트)
 * ============================================================================
 * A0~A3: PB2~5
 * A4~A6: PC0~2
 */
inline void set_addr_bus(uint8_t addr) {
    addr &= 0b01111111;  // 7비트 마스크
    
    // A0~A3 (PB2~5)
    PORTB = (PORTB & 0b11000011) | ((addr & 0b00001111) << 2);
    
    // A4~A6 (PC0~2)
    PORTC = (PORTC & 0b11111000) | ((addr >> 4) & 0b00000111);
}

/*
 * ============================================================================
 * 데이터 버스 입출력
 * ============================================================================
 * D0~D5: PD2~7
 * D6~D7: PB0~1
 */
inline void set_data_output() {
    DDRD |= 0b11111100;  // PD2~7 출력
    DDRB |= 0b00000011;  // PB0~1 출력
}

inline void set_data_input() {
    DDRD &= 0b00000011;  // PD2~7 입력
    PORTD &= 0b00000011; // 풀업 비활성화
    DDRB &= 0b11111100;  // PB0~1 입력
    PORTB &= 0b11111100; // 풀업 비활성화
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
 * High-Z 상태: 버스에서 완전히 분리
 * ============================================================================
 */
inline void set_high_z() {
    // 데이터 버스 High-Z (PD2~7, PB0~1)
    DDRD &= 0b00000011; 
    PORTD &= 0b00000011;
    DDRB &= 0b11111100; 
    PORTB &= 0b11111100;
    
    // 주소 버스 High-Z (PB2~5, PC0~2)
    DDRB &= 0b11000011;
    PORTB &= 0b11000011;
    DDRC &= 0b11111000;
    PORTC &= 0b11111000;
}

/*
 * ============================================================================
 * Fetch: ROM에서 명령어 읽기
 * ============================================================================
 */
uint8_t fetch() {
    ACTIVATE_CORE1();
    SET_BUS_READ();
    set_data_input();
    
    // 주소 버스 출력 모드
    DDRB |= 0b00111100;  // PB2~5
    DDRC |= 0b00000111;  // PC0~2
    
    // 페이지 설정
    set_page_595(current_page);
    
    // 오프셋 설정
    set_addr_bus(PC);
    
    SYNC_DELAY();
    _delay_us(5);
    
    uint8_t instruction = read_data_bus();
    
    return instruction;
}

/*
 * ============================================================================
 * Output: 나노#2가 데이터 버스를 읽을 수 있도록 출력
 * ============================================================================
 */
void output_register(uint8_t value) {
    SET_BUS_WRITE();
    set_data_output();
    
    write_data_bus(value);
    
    SYNC_DELAY();
    _delay_us(50);
    
    SET_BUS_READ();
    set_data_input();
}

/*
 * ============================================================================
 * Execute: 명령어 실행
 * ============================================================================
 */
void execute(uint8_t instruction) {
    uint8_t opcode = instruction & 0xF0;
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
            current_page = operand & 0x0F;
            PC = 0;
            break;
            
        case OP_HALT:
            halted = true;
            break;
            
        default:
            break;
    }
}

void setup() {
    // 데이터 버스 초기화
    set_data_input();
    
    // 주소 버스 출력
    DDRB |= 0b00111100;  // PB2~5
    DDRC |= 0b00000111;  // PC0~2
    
    // 595 제어 핀 출력
    DDRD |= 0b00000011;  // PD0~1
    DDRC |= 0b00001000;  // PC3
    
    // 시스템 제어 출력
    DDRC |= 0b00110000;  // PC4~5
    
    // 초기 상태
    ACTIVATE_CORE1();
    SET_BUS_READ();
    
    // VM 상태 초기화
    regA = 0x00;
    PC = 0;
    stack_ptr = 0;
    current_page = 0;
    cached_page = 0xFF;
    halted = false;
    
    // Slots, Stack 초기화
    for(uint8_t i = 0; i < 16; i++) slots[i] = 0;
    for(uint8_t i = 0; i < 8; i++) stack[i] = 0;
    
    // 초기 페이지 설정
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
    
    // Core 간 전환
    set_high_z();
    RELEASE_TO_CORE2();
    _delay_us(50);
    ACTIVATE_CORE1();
    
    _delay_us(10);
}

/*
 * ============================================================================
 * Example Programs
 * ============================================================================
 * 
 * Example 1 - 페이지 전환:
 * -------------------------------------
 * ; 페이지 0
 * LOAD 5
 * SLOT 0
 * SETPAGE 1
 * 
 * ; 페이지 1
 * FETCH 0
 * ADD 3
 * OUT
 * HALT
 * 
 * Example 2 - 슬롯과 스택:
 * -------------------------------------
 * LOAD 10
 * SLOT 0
 * LOAD 5
 * SLOT 1
 * FETCH 0
 * PUSH
 * FETCH 1
 * POP
 * OUT
 * HALT
 * 
 * Example 3 - 복합 연산:
 * --------------------------------
 * LOAD 3
 * SLOT 0
 * LOAD 4
 * SLOT 1
 * FETCH 0
 * PUSH
 * FETCH 1
 * PUSH
 * POP
 * SLOT 2
 * POP
 * FETCH 2
 * MUL 3
 * OUT
 * HALT
 * 
 * ============================================================================
 * 페이지 시스템
 * ============================================================================
 * - SETPAGE 0~15: 16개 페이지 (4비트)
 * - 각 페이지: 128바이트
 * - 최대 16KB 접근
 * - 페이지 캐싱으로 성능 최적화
 * ============================================================================
 */