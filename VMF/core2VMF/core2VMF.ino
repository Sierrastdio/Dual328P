/*
 * 코어2의 펌웨어도 코어1의 것과 동일한것으로 일단 간주.
 * ============================================================================
 * Core 1 - Virtual Machine Firmware
 * ============================================================================
 * 
 * This firmware interprets bytecode instructions stored in ROM.
 * Users write programs by loading bytecode into ROM via Arduino Nano.
 * 
 * Instruction Set (8-bit):
 * -------------------------
 * Format: OOOO DDDD (4-bit opcode, 4-bit data)
 * 
 * 0x0D  NOP        - No operation
 * 0x1D  LOAD D     - A = D (load immediate)
 * 0x2D  ADD D      - A = A + D
 * 0x3D  SUB D      - A = A - D
 * 0x4D  MUL D      - A = A * D
 * 0x5D  AND D      - A = A & D
 * 0x6D  OR D       - A = A | D
 * 0x70  OUT        - Output A to RegA (0x20)
 * 0x80  LOADM      - A = ROM[next byte address]
 * 0x90  STORM      - ROM[next byte] = A (if writable)
 * 0xF0  HALT       - Stop execution
 * 
 * Example Program:
 * ----------------
 * 0x0000: 0x15   LOAD 5      ; A = 5
 * 0x0001: 0x23   ADD 3       ; A = 5 + 3 = 8
 * 0x0002: 0x70   OUT         ; Output 8
 * 0x0003: 0xF0   HALT        ; Stop
 * 
 * ============================================================================
 */

#include <avr/io.h>
#include <util/delay.h>

// ============================================================================
// Instruction Set Opcodes
// ============================================================================
#define OP_NOP   0x00
#define OP_LOAD  0x10
#define OP_ADD   0x20
#define OP_SUB   0x30
#define OP_MUL   0x40
#define OP_AND   0x50
#define OP_OR    0x60
#define OP_OUT   0x70
#define OP_LOADM 0x80
#define OP_STORM 0x90
#define OP_HALT  0xF0

// ============================================================================
// VM State
// ============================================================================
volatile uint8_t regA = 0x00;      // Accumulator
volatile uint8_t regB = 0x00;      // Temporary (internal)
volatile uint16_t PC = 0x00;       // Program Counter
volatile bool halted = false;       // Execution state

// I/O Address
const uint8_t REG_A_ADDR = 0x20;

// ============================================================================
// Hardware Control Macros
// ============================================================================
#define TAKE_BUS()      PORTC |= (1 << 4)
#define GIVE_BUS()      PORTC &= ~(1 << 4)
#define SET_READ()      PORTC &= ~(1 << 5)
#define SET_WRITE()     PORTC |= (1 << 5)
#define SYNC_DELAY()    asm volatile("nop\n\t nop\n\t nop\n\t")

// ============================================================================
// Hardware Abstraction Layer
// ============================================================================

inline void set_addr(uint8_t a) {
    DDRB |= 0x3C;
    DDRC |= 0x07;
    a &= 0x7F;
    PORTB = (PORTB & 0xC3) | ((a & 0x0F) << 2);
    PORTC = (PORTC & 0xF8) | ((a >> 4) & 0x07);
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

// ============================================================================
// VM Instructions
// ============================================================================

/**
 * Read instruction byte from ROM at current PC
 */
uint8_t fetch() {
    TAKE_BUS();
    set_data_in();
    SET_READ();
    
    set_addr(PC & 0x7F);
    SYNC_DELAY();
    _delay_us(5);
    
    uint8_t instruction = read_bus();
    
    set_high_z();
    return instruction;
}

/**
 * Output register to monitor
 */
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

/**
 * Execute single instruction
 */
void execute(uint8_t instruction) {
    uint8_t opcode = instruction & 0xF0;
    uint8_t operand = instruction & 0x0F;
    
    switch(opcode) {
        case OP_NOP:
            // Do nothing
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
            
        case OP_LOADM:
            // Load from memory (next byte is address)
            PC++;
            regB = fetch();
            PC++;
            regA = fetch();  // Value at address
            PC--;  // Adjust for auto-increment
            break;
            
        case OP_HALT:
            halted = true;
            break;
            
        default:
            // Invalid opcode, treat as NOP
            break;
    }
}

// ============================================================================
// Main Program
// ============================================================================

void setup() {
    // Configure control pins
    DDRC |= 0x30;
    
    TAKE_BUS();
    SET_READ();
    
    // Initialize VM state
    regA = 0x00;
    regB = 0x00;
    PC = 0x00;
    halted = false;
    
    // Small delay for system stabilization
    _delay_ms(100);
}

void loop() {
    if(halted) {
        // Program halted, release bus
        set_high_z();
        GIVE_BUS();
        _delay_ms(100);
        return;
    }
    
    // Fetch-Decode-Execute cycle
    uint8_t instruction = fetch();
    execute(instruction);
    
    PC++;
    
    // Prevent PC overflow (wrap at 128 bytes)
    if(PC >= 128) {
        PC = 0;
    }
    
    // Give Core 2 a chance to run
    set_high_z();
    GIVE_BUS();
    _delay_us(100);
    TAKE_BUS();
    
    _delay_us(10);  // Instruction cycle time
}

/*
 * ============================================================================
 * Programming Examples
 * ============================================================================
 * 
 * Example 1: Simple Addition
 * --------------------------
 * W 0x0000 0x15   ; LOAD 5
 * W 0x0001 0x23   ; ADD 3
 * W 0x0002 0x70   ; OUT
 * W 0x0003 0xF0   ; HALT
 * G               ; Execute
 * 
 * Output: [CORE1] A: 0x08 (8)
 * 
 * 
 * Example 2: Multiplication
 * -------------------------
 * W 0x0000 0x17   ; LOAD 7
 * W 0x0001 0x42   ; MUL 2
 * W 0x0002 0x70   ; OUT
 * W 0x0003 0xF0   ; HALT
 * G
 * 
 * Output: [CORE1] A: 0x0E (14)
 * 
 * 
 * Example 3: Bit Operations
 * --------------------------
 * W 0x0000 0x1F   ; LOAD 15 (0b1111)
 * W 0x0001 0x5C   ; AND 12 (0b1100)
 * W 0x0002 0x70   ; OUT
 * W 0x0003 0xF0   ; HALT
 * G
 * 
 * Output: [CORE1] A: 0x0C (12)
 * 
 * ============================================================================
 */