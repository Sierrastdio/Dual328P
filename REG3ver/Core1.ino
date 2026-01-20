/*
 * ============================================================================
 * Core 1 (Master) - 3 Register Architecture
 * ============================================================================
 * 
 * Hardware Pinout:
 * ----------------
 * Data Bus (8-bit):
 *   - D0~D5: PD2~PD7 (6 bits on PORTD)
 *   - D6~D7: PB0~PB1 (2 bits on PORTB)
 * 
 * Address Bus (7-bit):
 *   - A0~A3: PB2~PB5 (4 bits on PORTB)
 *   - A4~A6: PC0~PC2 (3 bits on PORTC)
 * 
 * Control Signals:
 *   - PC4: Bus Grant (HIGH=Master owns, LOW=Slave owns via 74HC04)
 *   - PC5: Direction Control (LOW=Read, HIGH=Write)
 * 
 * Registers:
 *   - Register A (0x70): Accumulator (Main calculation results)
 *   - Register B (0x71): Operand (Temporary data storage)
 *   - Register C (0x72): Counter/Index (Loop control)
 * 
 * Operation:
 *   1. Read data from ROM into Register B
 *   2. Perform calculation (B + 1) and store in Register A
 *   3. Output Register A to Monitor (address 0x70)
 *   4. Increment Register C (loop counter)
 *   5. Release bus for Core 2 operation
 * 
 * ============================================================================
 */

#include <avr/io.h>
#include <util/delay.h>

// ============================================================================
// Configuration Constants
// ============================================================================
const uint16_t ROM_START = 0x0000;
const uint8_t DATA_SET[] = {0x00, 0x01, 0x02, 0x03, 0x04};
const uint8_t LEN = 5;

// Register I/O Addresses (Memory-mapped I/O)
const uint8_t REG_A_ADDR = 0x70;  // Accumulator output address
const uint8_t REG_B_ADDR = 0x71;  // Operand output address (optional)
const uint8_t REG_C_ADDR = 0x72;  // Counter output address (optional)

// ============================================================================
// Software Registers
// ============================================================================
volatile uint8_t regA = 0x00;  // Accumulator
volatile uint8_t regB = 0x00;  // Operand
volatile uint8_t regC = 0x00;  // Counter

// ============================================================================
// Hardware Control Macros
// ============================================================================

// Bus Ownership Control
#define TAKE_BUS()      PORTC |= (1 << 4)   // PC4 HIGH -> Master Active
#define GIVE_BUS()      PORTC &= ~(1 << 4)  // PC4 LOW  -> Slave Active

// Data Direction Control
#define SET_READ()      PORTC &= ~(1 << 5)  // PC5 LOW  = Read Mode
#define SET_WRITE()     PORTC |= (1 << 5)   // PC5 HIGH = Write Mode

// Timing
#define SYNC_DELAY()    asm volatile("nop\n\t nop\n\t nop\n\t")

// ============================================================================
// Hardware Abstraction Layer
// ============================================================================

/**
 * Set 7-bit address on bus
 */
inline void set_addr(uint16_t a) {
    // Set address pins to OUTPUT
    DDRB |= 0x3C;  // PB2~PB5 (A0~A3)
    DDRC |= 0x07;  // PC0~PC2 (A4~A6)
    
    // Write address value
    PORTB = (PORTB & 0xC3) | ((a & 0x0F) << 2);
    PORTC = (PORTC & 0xF8) | ((a >> 4) & 0x07);
}

/**
 * Configure data bus as OUTPUT
 */
inline void set_data_out() { 
    DDRD |= 0xFC;  // PD2~PD7
    DDRB |= 0x03;  // PB0~PB1
}

/**
 * Configure data bus as INPUT
 */
inline void set_data_in() { 
    DDRD &= 0x03;  // PD2~PD7 as INPUT
    DDRB &= 0xFC;  // PB0~PB1 as INPUT
}

/**
 * Set all bus lines to High-Z
 */
inline void set_high_z() {
    DDRD &= 0x03; PORTD &= 0x03;
    DDRB &= 0x03; PORTB &= 0x03;
    DDRC &= 0xF8; PORTC &= 0xF8;
}

/**
 * Write 8-bit data to bus
 */
inline void write_bus(uint8_t d) {
    PORTD = (PORTD & 0x03) | ((d << 2) & 0xFC);
    PORTB = (PORTB & 0xFC) | ((d >> 6) & 0x03);
}

/**
 * Read 8-bit data from bus
 */
inline uint8_t read_bus() {
    return ((PIND & 0xFC) >> 2) | ((PINB & 0x03) << 6);
}

// ============================================================================
// Register Output Functions
// ============================================================================

/**
 * Output register value to monitor
 * This triggers 74HC138 decoder to activate corresponding Y output
 */
void output_register(uint8_t reg_addr, uint8_t value) {
    TAKE_BUS();         // Claim bus
    set_data_out();     // Data pins as output
    SET_WRITE();        // Set write mode
    
    set_addr(reg_addr); // Set register address (triggers 74HC138)
    write_bus(value);   // Write register value to data bus
    
    SYNC_DELAY();       // Hold for Arduino Nano to read
    _delay_us(10);      // Extended hold time for monitoring
    
    set_high_z();       // Release bus
}

// ============================================================================
// Main Program
// ============================================================================

void setup() {
    // Configure control pins as OUTPUT
    DDRC |= 0x30;  // PC4 (Bus Grant), PC5 (Direction)
    
    // Initial state: Master owns bus
    TAKE_BUS();
    SET_READ();
    
    // Initialize registers
    regA = 0x00;
    regB = 0x00;
    regC = 0x00;
}

void loop() {
    // ========================================================================
    // Main Execution Loop
    // ========================================================================
    
    for(uint8_t i = 0; i < LEN; i++) {
        // ====================================================================
        // PHASE 1: Read from ROM into Register B
        // ====================================================================
        TAKE_BUS();
        set_data_in();
        SET_READ();
        
        set_addr(ROM_START + DATA_SET[i]);
        SYNC_DELAY();
        
        regB = read_bus();  // Load data into Register B
        
        // ====================================================================
        // PHASE 2: Calculation (A = B + 1)
        // ====================================================================
        regA = regB + 1;    // Accumulator = Operand + 1
        regC = i;           // Update counter
        
        // ====================================================================
        // PHASE 3: Output Register A to Monitor
        // ====================================================================
        output_register(REG_A_ADDR, regA);
        
        // Optional: Output other registers for debugging
        // output_register(REG_B_ADDR, regB);
        // output_register(REG_C_ADDR, regC);
        
        // ====================================================================
        // PHASE 4: Release Bus for Core 2
        // ====================================================================
        set_high_z();
        GIVE_BUS();
        _delay_us(20);  // Wait for Core 2 to complete its operation
    }
    
    // End of cycle marker (optional)
    output_register(REG_C_ADDR, 0xFF);  // Signal completion
    
    _delay_ms(1000);  // Pause between cycles
}
