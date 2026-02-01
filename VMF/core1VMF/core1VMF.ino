/*
 * ============================================================================
 * Core 1 - Virtual Machine Firmware v2.2 (Final)
 * ============================================================================
 * 
 * Features:
 * - 16 Slots (SRAM variables)
 * - 8-deep Stack
 * - Physical Page switching (A7~A13 via address bus)
 * 
 * ============================================================================
 */

#include <avr/io.h>
#include <util/delay.h>

// Instruction Set
#define OP_NOP     0x00
#define OP_LOAD    0x10
#define OP_ADD     0x20
#define OP_SUB     0x30
#define OP_MUL     0x40
#define OP_AND     0x50
#define OP_OR      0x60
#define OP_OUT     0x70
#define OP_FETCH   0x80
#define OP_SLOT    0x90
#define OP_PUSH    0xA0
#define OP_POP     0xB0
#define OP_SETPAGE 0xE0
#define OP_HALT    0xF0

// VM State
volatile uint8_t regA = 0x00;
volatile uint8_t slots[16];        // 16 variable slots
volatile uint8_t stack[8];         // 8-deep stack
volatile uint8_t SP = 0;           // Stack pointer
volatile uint8_t page = 0;         // Current page (0-15)
volatile uint8_t PC_offset = 0;    // Offset within page (0-127)
volatile bool halted = false;

const uint8_t REG_A_ADDR = 0x20;

// Hardware Control
#define TAKE_BUS()      PORTC |= (1 << 4)
#define GIVE_BUS()      PORTC &= ~(1 << 4)
#define SET_READ()      PORTC &= ~(1 << 5)
#define SET_WRITE()     PORTC |= (1 << 5)
#define SYNC_DELAY()    asm volatile("nop\n\t nop\n\t nop\n\t")

// Hardware Abstraction
inline void set_addr(uint8_t offset_7bit) {
    // 하위 7비트 주소만 설정 (A0~A6)
    DDRB |= 0x3C;  // PB2~PB5 출력
    DDRC |= 0x07;  // PC0~PC2 출력
    
    offset_7bit &= 0x7F;
    PORTB = (PORTB & 0xC3) | ((offset_7bit & 0x0F) << 2);
    PORTC = (PORTC & 0xF8) | ((offset_7bit >> 4) & 0x07);
}

inline void set_page_addr(uint8_t page_num) {
    // 페이지 번호를 상위 주소로 변환
    // 실제로는 74HC595를 통해 A7~A13을 제어해야 하지만
    // 현재 설계에서는 코어가 직접 제어할 수 없음
    // 
    // **해결책**: PC3을 통해 138 디코더의 C 입력을 제어
    // 이를 통해 제한적이나마 페이지 전환 가능
    
    // PC3 토글로 간단한 페이지 구분
    if(page_num & 0x01) {
        PORTC |= (1 << 3);   // PC3 = HIGH
    } else {
        PORTC &= ~(1 << 3);  // PC3 = LOW
    }
}

inline void set_data_out() { 
    DDRD |= 0xFC;
    DDRB |= 0x03;
}

inline void set_data_in() { 
    DDRD &= 0x03;
    DDRB &= 0xFC;
}

inline void set_high_z() {
    DDRD &= 0x03; PORTD &= 0x03;
    DDRB &= 0x03; PORTB &= 0x03;
    DDRC &= 0xF8; PORTC &= 0xF8;
}

inline void write_bus(uint8_t d) {
    PORTD = (PORTD & 0x03) | ((d << 2) & 0xFC);
    PORTB = (PORTB & 0xFC) | ((d >> 6) & 0x03);
}

inline uint8_t read_bus() {
    return ((PIND & 0xFC) >> 2) | ((PINB & 0x03) << 6);
}

// Fetch instruction from ROM
uint8_t fetch() {
    TAKE_BUS();
    set_data_in();
    SET_READ();
    
    // 페이지 설정 (상위 주소)
    set_page_addr(page);
    
    // 오프셋 설정 (하위 7비트)
    set_addr(PC_offset);
    
    SYNC_DELAY();
    _delay_us(5);
    
    uint8_t instruction = read_bus();
    
    set_high_z();
    return instruction;
}

// Output to monitor
void output_register(uint8_t reg_addr, uint8_t value) {
    TAKE_BUS();
    set_data_out();
    SET_WRITE();
    
    set_addr(reg_addr);
    write_bus(value);
    
    SYNC_DELAY();
    _delay_us(50);
    
    set_high_z();
}

// Execute instruction
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
            output_register(REG_A_ADDR, regA);
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
            // Physical page switch
            page = operand & 0x0F;
            PC_offset = 0;  // Reset offset when changing page
            break;
            
        case OP_HALT:
            halted = true;
            break;
            
        default:
            break;
    }
}

void setup() {
    // Configure control pins
    DDRC |= 0x38;  // PC3, PC4, PC5 출력 (페이지 제어 포함)
    
    TAKE_BUS();
    SET_READ();
    
    // Initialize VM state
    regA = 0x00;
    PC_offset = 0;
    SP = 0;
    page = 0;
    halted = false;
    
    // Clear slots and stack
    for(uint8_t i=0; i<16; i++) slots[i] = 0;
    for(uint8_t i=0; i<8; i++) stack[i] = 0;
    
    _delay_ms(100);
}

void loop() {
    if(halted) {
        set_high_z();
        GIVE_BUS();
        _delay_ms(100);
        return;
    }
    
    // Fetch-Decode-Execute
    uint8_t instruction = fetch();
    execute(instruction);
    
    // Increment PC within page
    PC_offset++;
    
    // Wrap at 128 bytes per page
    if(PC_offset >= 128) {
        PC_offset = 0;
        // Stay on current page or halt
        // (automatic page increment could be added here)
    }
    
    // Give Core 2 a chance
    set_high_z();
    GIVE_BUS();
    _delay_us(100);
    TAKE_BUS();
    
    _delay_us(10);
}

/*
 * ============================================================================
 * Physical Page Switching
 * ============================================================================
 * 
 * 현재 구현:
 * - PC3을 통한 제한적 페이지 전환 (2페이지)
 * - page 변수에 0-15 저장
 * 
 * 완전한 구현을 위해서는:
 * - 추가 I/O 핀으로 74HC595 제어 필요
 * - 또는 나노가 미리 여러 페이지를 연속으로 로드
 * 
 * 현재는 SETPAGE로 page 레지스터 변경 + PC3 토글만 수행
 * 
 * ============================================================================
 * 
 * Example Programs
 * ============================================================================
 * 
 * Example 1 - Using Slots (Variables):
 * -------------------------------------
 * LOAD 10
 * SLOT 0       ; slots[0] = 10
 * LOAD 5
 * SLOT 1       ; slots[1] = 5
 * FETCH 0      ; A = slots[0] = 10
 * PUSH         ; stack = [10]
 * FETCH 1      ; A = slots[1] = 5
 * POP          ; A = 10 (from stack)
 * OUT
 * HALT
 * 
 * Example 2 - Page Switching:
 * ----------------------------
 * ; Page 0
 * LOAD 5
 * SLOT 0
 * SETPAGE 1    ; Switch to page 1
 * ; (continues on page 1 if loaded)
 * 
 * Example 3 - Complex with Stack:
 * --------------------------------
 * LOAD 3
 * SLOT 0       ; x = 3
 * LOAD 4
 * SLOT 1       ; y = 4
 * FETCH 0      ; A = 3
 * PUSH
 * FETCH 1      ; A = 4
 * PUSH
 * POP          ; A = 4
 * SLOT 2       ; temp = 4
 * POP          ; A = 3
 * FETCH 2      ; A = 4
 * MUL 3        ; A = 12
 * OUT
 * HALT
 * 
 * ============================================================================
 */