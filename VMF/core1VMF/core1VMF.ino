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
volatile uint8_t slots[16];        // 16 variable slots 데이터 크기 8비트로 취급.
volatile uint8_t stack[8];         // 8-deep stack
volatile uint8_t STCP = 0;           // Stack pointer
volatile uint8_t PC = 0;           // 페이지 내 오프셋 (0~127)
volatile uint8_t current_page = 0; // 현재 페이지 (0~127)
volatile bool halted = false;

// Page Cache
volatile uint8_t cached_page = 0xFF; // 캐시된 페이지 번호 (초기값: 무효)

// Hardware Control Macros
#define ACTIVATE_CORE1()    PORTC &= ~(1 << 4)
#define RELEASE_TO_CORE2()  PORTC |=  (1 << 4)
#define SET_BUS_READ()      PORTC &= ~(1 << 5)
#define SET_BUS_WRITE()     PORTC |=  (1 << 5)

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
 */
uint8_t fetch() {
    ACTIVATE_CORE1();
    SET_BUS_READ();
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
    SET_BUS_WRITE();
    set_data_output();
    
    write_data_bus(value);
    
    SYNC_DELAY();
    _delay_us(50);  // 나노#2가 74HC165로 읽을 시간 확보
    
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
    
    // 시스템 제어 (PC4~5)
    DDRC |= 0x30;  // PC4(Core Select), PC5(DIR) 출력
    
    // 초기 상태
    ACTIVATE_CORE1();
    SET_BUS_READ();
    
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
        RELEASE_TO_CORE2();
        _delay_ms(100);
        return;
    }
    
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
    
    // Core 간 전환 (인터리빙)
    set_high_z();
    RELEASE_TO_CORE2();
    _delay_us(50);  // Core 2가 작업할 시간
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
 * ; 페이지 0 (물리주소 0x0000~0x007F)
 * LOAD 5
 * SLOT 0
 * SETPAGE 1    ; 페이지 1로 전환
 * 
 * ; 페이지 1 (물리주소 0x0080~0x00FF)
 * FETCH 0      ; slot[0]에서 5를 가져옴 (슬롯은 유지됨)
 * ADD 3        ; A = 8
 * OUT
 * HALT
 * 
 * Example 2 - 슬롯과 스택:
 * -------------------------------------
 * LOAD 10      ; A = 10
 * SLOT 0       ; slots[0] = 10
 * LOAD 5       ; A = 5
 * SLOT 1       ; slots[1] = 5
 * FETCH 0      ; A = slots[0] = 10
 * PUSH         ; stack = [10]
 * FETCH 1      ; A = slots[1] = 5
 * POP          ; A = 10
 * OUT          ; 출력
 * HALT
 * 
 * Example 3 - 복합 연산:
 * --------------------------------
 * LOAD 3       ; A = 3
 * SLOT 0       ; x = 3
 * LOAD 4       ; A = 4
 * SLOT 1       ; y = 4
 * FETCH 0      ; A = 3
 * PUSH         ; stack = [3]
 * FETCH 1      ; A = 4
 * PUSH         ; stack = [3, 4]
 * POP          ; A = 4
 * SLOT 2       ; temp = 4
 * POP          ; A = 3
 * FETCH 2      ; A = 4
 * MUL 3        ; A = 12
 * OUT
 * HALT
 * 
 * ============================================================================
 * 페이지 시스템
 * ============================================================================
 * - SETPAGE 0~15: 16개 페이지 사용 (4비트)
 * - 확장 시 0~127까지 사용 가능 (7비트)
 * - 각 페이지: 128바이트
 * - 최대 16KB 접근 가능 (Bank 0 기준)
 * - PC4로 Bank 선택 (Bank 0/1)
 * 
 * 페이지 캐싱:
 * - 같은 페이지 내에서는 595 업데이트 스킵
 * - 페이지 전환 시에만 595 업데이트
 * - 평균 오버헤드: 약 0.5 사이클/fetch
 * ============================================================================
 */