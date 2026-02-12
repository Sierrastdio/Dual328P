/*
 * ============================================================================
 * Core 2 - Virtual Machine Firmware v4.0 (74HC595 Paging)
 * ============================================================================
 * 핀 배치:
 * - PD2~7, PB0~1: 데이터 버스 (D0~D7) → 74HC245 데이터버퍼#2
 * - PB2~5, PC0~2: 주소 버스 (A0~A6) → 74HC245 주소버퍼#2
 * - PD0: 74HC595-Core2 SER (Serial Data)
 * - PD1: 74HC595-Core2 SCK (Shift Clock)
 * - PC3: 74HC595-Core2 RCK (Latch Clock)
 * - PC4: N/C (Core2는 Bank Select 사용 안 함)
 * - PC5: N/C (DIR은 Core1이 제어)
 * 
 * 페이징 시스템:
 * - 74HC595로 A7~A13 제어 (7비트)
 * - 128페이지 × 128바이트 = 16KB 접근 가능
 * - 페이지 캐싱으로 성능 최적화
 * - Bank 1 영역 사용 (A14=1, Core1 PC4가 제어)
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
volatile uint8_t STCP = 0;           // Stack pointer
volatile uint8_t PC = 0;           // 페이지 내 오프셋 (0~127)
volatile uint8_t current_page = 0; // 현재 페이지 (0~127)
volatile bool halted = false;

// Page Cache
volatile uint8_t cached_page = 0xFF; // 캐시된 페이지 번호 (초기값: 무효)

// Hardware Control Macros
// Core 2는 Core1 PC4 신호를 받아 버퍼 OE 제어됨
// 직접 제어 없음

// 74HC595 Control
#define HC595_SER_HIGH()    PORTD |=  (1 << 0)
#define HC595_SER_LOW()     PORTD &= ~(1 << 0)
#define HC595_SCK_HIGH()    PORTD |=  (1 << 1)
#define HC595_SCK_LOW()     PORTD &= ~(1 << 1)
#define HC595_RCK_HIGH()    PORTC |=  (1 << 3)
#define HC595_RCK_LOW()     PORTC &= ~(1 << 3)

#define SYNC_DELAY()        asm volatile("nop\n\t nop\n\t nop\n\t nop\n\t")

/*
 * ============================================================================
 * 74HC595 페이지 설정 (A7~A13, 7비트)
 * ============================================================================
 * 페이지 캐싱: 같은 페이지면 595 업데이트 스킵
 */
inline void set_page_595(uint8_t page) {
    // 페이지 캐싱
    if(page == cached_page) return;
    
    page &= 0x7F;  // 7비트 마스크 (0~127)
    
    HC595_RCK_LOW();
    
    // 7비트 시리얼 전송 (MSB first)
    for(uint8_t i = 0; i < 7; i++) {
        if(page & 0x40) {  // 최상위 비트 체크
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
    
    cached_page = page >> 1;  // 원래 값으로 복원해서 저장
}

/*
 * ============================================================================
 * 주소 버스 설정 (A0~A6, 7비트)
 * ============================================================================
 * A0~A3: PB2~5
 * A4~A6: PC0~2
 */
inline void set_addr_bus(uint8_t addr) {
    addr &= 0x7F;  // 7비트 마스크
    
    // A0~A3 (PB2~5)
    PORTB = (PORTB & 0xC3) | ((addr & 0x0F) << 2);
    
    // A4~A6 (PC0~2)
    PORTC = (PORTC & 0xF8) | ((addr >> 4) & 0x07);
}

/*
 * ============================================================================
 * 데이터 버스 입출력
 * ============================================================================
 * D0~D5: PD2~7
 * D6~D7: PB0~1
 */
inline void set_data_output() {
    DDRD |= 0xFC;  // PD2~7 출력
    DDRB |= 0x03;  // PB0~1 출력
}

inline void set_data_input() {
    DDRD &= 0x03;  // PD2~7 입력
    PORTD &= 0x03; // 풀업 비활성화
    DDRB &= 0xFC;  // PB0~1 입력
    PORTB &= 0xFC; // 풀업 비활성화
}

inline void write_data_bus(uint8_t data) {
    PORTD = (PORTD & 0x03) | ((data << 2) & 0xFC);
    PORTB = (PORTB & 0xFC) | ((data >> 6) & 0x03);
}

inline uint8_t read_data_bus() {
    return ((PIND & 0xFC) >> 2) | ((PINB & 0x03) << 6);
}

/*
 * ============================================================================
 * High-Z 상태: 버스에서 완전히 분리
 * ============================================================================
 * 주의: 595 제어 핀(PD0, PD1, PC3)은 출력 유지
 */
inline void set_high_z() {
    // 데이터 버스 High-Z (PD2~7, PB0~1)
    DDRD &= 0x03; 
    PORTD &= 0x03;
    DDRB &= 0xFC; 
    PORTB &= 0xFC;
    
    // 주소 버스 High-Z (PB2~5, PC0~2)
    DDRB &= 0xC3;
    PORTB &= 0xC3;
    DDRC &= 0xF8;
    PORTC &= 0xF8;
    
    // 595 제어 핀은 출력 유지 (PD0, PD1은 이미 0x03 마스크로 보호됨)
    // PC3(RCK)는 0xF8 마스크로 출력 유지
}

/*
 * ============================================================================
 * Fetch: ROM에서 명령어 읽기
 * ============================================================================
 * Core2는 Core1이 버스를 양보했을 때만 활성화됨
 */
uint8_t fetch() {
    set_data_input();
    
    // 주소 버스 출력 모드 (A0~A6)
    DDRB |= 0x3C;  // PB2~5
    DDRC |= 0x07;  // PC0~2
    
    // 페이지 설정 (캐시되어 있으면 스킵)
    set_page_595(current_page);
    
    // 오프셋 설정 (A0~A6)
    set_addr_bus(PC);
    
    SYNC_DELAY();
    _delay_us(5);  // EEPROM 액세스 타임
    
    uint8_t instruction = read_data_bus();
    
    return instruction;
}

/*
 * ============================================================================
 * Output: 나노#2가 데이터 버스를 읽을 수 있도록 출력
 * ============================================================================
 */
void output_register(uint8_t value) {
    set_data_output();
    
    write_data_bus(value);
    
    SYNC_DELAY();
    _delay_us(50);  // 나노#2가 74HC165로 읽을 시간 확보
    
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
            // Load from slot to regA
            regA = slots[operand & 0x0F];
            break;
            
        case OP_SLOT:
            // Store regA to slot
            slots[operand & 0x0F] = regA;
            break;
            
        case OP_PUSH:
            // Push regA to stack
            if(SP < 8) {
                stack[SP++] = regA;
            }
            break;
            
        case OP_POP:
            // Pop from stack to regA
            if(SP > 0) {
                regA = stack[--SP];
            }
            break;
            
        case OP_SETPAGE:
            // 물리적 페이지 전환 (0~15, 4비트 사용)
            // 전체 128페이지 중 일부만 사용
            // 확장 시 operand & 0x7F로 변경
            current_page = operand & 0x0F;
            PC = 0;  // 새 페이지 시작점으로
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
    
    // 주소 버스 (PB2~5, PC0~2)
    DDRB |= 0x3C;
    DDRC |= 0x07;
    
    // 595 제어 핀 (PD0~1, PC3)
    DDRD |= 0x03;  // PD0(SER), PD1(SCK) 출력
    DDRC |= 0x08;  // PC3(RCK) 출력
    
    // PC4, PC5는 사용 안 함 (N/C)
    
    // VM 상태 초기화
    regA = 0x00;
    PC = 0;
    SP = 0;
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
        _delay_ms(100);
        return;
    }
    
    // Core1이 버스를 양보할 때까지 대기
    // (실제로는 Core1의 타이밍에 맞춰 자동으로 동작)
    
    // Fetch-Decode-Execute
    uint8_t instruction = fetch();
    execute(instruction);
    
    // PC 증가 (128바이트 순환)
    PC++;
    if(PC >= 128) {
        PC = 0;
        // 자동 페이지 증가는 하지 않음
        // SETPAGE로만 페이지 전환
    }
    
    // 버스 반납 (High-Z)
    set_high_z();
    
    // Core1이 다시 버스 사용할 시간
    _delay_us(50);
    
    _delay_us(10);
}

/*
 * ============================================================================
 * Example Programs (Bank 1 영역 사용)
 * ============================================================================
 * 
 * 나노#1에서 프로그램 작성:
 * 
 * ; Core2용 프로그램 - 페이지 0
 * LOAD 7
 * SLOT 0
 * SETPAGE 1
 * :w 0
 * 
 * ; Core2용 프로그램 - 페이지 1
 * FETCH 0
 * MUL 2
 * OUT
 * HALT
 * :w 1
 * 
 * ; 실행
 * :run
 * 
 * ============================================================================
 * 설계 노트
 * ============================================================================
 * 
 * Core 1 vs Core 2 차이점:
 * 
 * 1. Bank 선택:
 *    - Core1: PC4로 A14 제어 (Bank 0/1 선택 가능)
 *    - Core2: Bank 1만 사용 (A14=1, Core1 PC4가 HIGH일 때)
 * 
 * 2. 버스 제어:
 *    - Core1: ACTIVATE_CORE1(), RELEASE_TO_CORE2() 사용
 *    - Core2: 수동 제어 없음 (Core1의 신호에 따라 자동)
 * 
 * 3. DIR 제어:
 *    - Core1: PC5로 모든 버퍼의 DIR 제어
 *    - Core2: DIR 제어 없음 (Core1이 담당)
 * 
 * 4. 페이징:
 *    - Core1: 74HC595-Core1 사용
 *    - Core2: 74HC595-Core2 사용 (독립적)
 * 
 * 5. 메모리 영역:
 *    - Core1: Bank 0 (0x0000~0x3FFF)
 *    - Core2: Bank 1 (0x4000~0x7FFF)
 * 
 * ============================================================================
 * 페이지 시스템
 * ============================================================================
 * - SETPAGE 0~15: 16개 페이지 사용 (4비트)
 * - 확장 시 0~127까지 사용 가능 (7비트)
 * - 각 페이지: 128바이트
 * - 최대 16KB 접근 가능 (Bank 1 기준)
 * 
 * 페이지 캐싱:
 * - 같은 페이지 내에서는 595 업데이트 스킵
 * - 페이지 전환 시에만 595 업데이트
 * - 평균 오버헤드: 약 0.5 사이클/fetch
 * ============================================================================
 */