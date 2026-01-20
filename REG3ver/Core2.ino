/*
 * ============================================================================
 * Core 2 (Slave) - 3 Register Architecture
 * ============================================================================
 * 
 * Hardware Pinout: (Identical to Core 1)
 * ----------------
 * Data Bus (8-bit):
 *   - D0~D5: PD2~PD7
 *   - D6~D7: PB0~PB1
 * 
 * Address Bus (7-bit):
 *   - A0~A3: PB2~PB5
 *   - A4~A6: PC0~PC2
 * 
 * Control Signals (INPUT mode - monitoring Master):
 *   - PC4: Bus Grant Signal (via 74HC04 inverter)
 *          Master HIGH -> Inverter -> Slave sees LOW (Bus NOT granted)
 *          Master LOW  -> Inverter -> Slave sees HIGH (Bus granted)
 *   - PC5: Direction Monitor (LOW=Read, HIGH=Write)
 * 
 * Registers:
 *   - Register A (0x73): Accumulator (Calculation results)
 *   - Register B (0x74): Operand (Temporary storage)
 *   - Register C (0x75): Counter/Index (Loop control)
 * 
 * Operation:
 *   1. Wait for bus grant from Master (PC4 HIGH via inverter)
 *   2. Read data from ROM into Register B
 *   3. Perform calculation (B * B) and store in Register A
 *   4. Output Register A to Monitor (address 0x73)
 *   5. Update Register C (loop counter)
 *   6. Release bus and wait for next grant
 * 
 * ============================================================================
 */

#include <avr/io.h>

// ============================================================================
// Configuration Constants
// ============================================================================
const uint16_t SLAVE_OFFSET = 0x0040;
const uint8_t DATA_SET[] = {0x02, 0x03, 0x04, 0x05, 0x06};
const uint8_t LEN = 5;

// Register I/O Addresses (Memory-mapped I/O)
const uint8_t REG_A_ADDR = 0x73;  // Core 2 Accumulator
const uint8_t REG_B_ADDR = 0x74;  // Core 2 Operand
const uint8_t REG_C_ADDR = 0x75;  // Core 2 Counter

// ============================================================================
// Software Registers
// ============================================================================
volatile uint8_t regA = 0x00;  // Accumulator
volatile uint8_t regB = 0x00;  // Operand
volatile uint8_t regC = 0x00;  // Counter

// ============================================================================
// Hardware Monitoring Macros
// ============================================================================

// Bus Grant Detection (inverted by 74HC04)
#define BUS_GRANTED()   (PINC & (1 << 4))   // HIGH = Bus granted to Slave

// Direction Detection
#define IS_READ()       (!(PINC & (1 << 5))) // LOW = Read phase
#define IS_WRITE()      (PINC & (1 << 5))    // HIGH = Write phase

// Timing
#define SYNC_DELAY()    asm volatile("nop\n\t nop\n\t nop\n\t")

// ============================================================================
// Hardware Abstraction Layer
// ============================================================================

/**
 * Set ALL bus pins to High-Z
 */
inline void set_high_z_all() {
    DDRD &= 0x03; PORTD &= 0x03; // Data bus Hi-Z
    DDRB &= 0x03; PORTB &= 0x03; // Data + Addr(Low) Hi-Z
    DDRC &= 0xF8; PORTC &= 0xF8; // Addr(High) Hi-Z
}

/**
 * Set 7-bit address on bus
 */
inline void set_addr(uint16_t a) {
    DDRB |= 0x3C; DDRC |= 0x07; // Set Addr pins to OUTPUT
    PORTB = (PORTB & 0xC3) | ((a & 0x0F) << 2);
    PORTC = (PORTC & 0xF8) | ((a >> 4) & 0x07);
}

/**
 * Configure data bus as OUTPUT
 */
inline void set_data_out() { 
    DDRD |= 0xFC; 
    DDRB |= 0x03; 
}

/**
 * Configure data bus as INPUT
 */
inline void set_data_in() { 
    DDRD &= 0x03; 
    DDRB &= 0xFC; 
}

/**
 * Read 8-bit data from bus
 */
inline uint8_t read_bus() {
    return ((PIND & 0xFC) >> 2) | ((PINB & 0x03) << 6);
}

/**
 * Write 8-bit data to bus
 */
inline void write_bus(uint8_t d) {
    PORTD = (PORTD & 0x03) | ((d << 2) & 0xFC);
    PORTB = (PORTB & 0xFC) | ((d >> 6) & 0x03);
}

// ============================================================================
// Register Output Function
// ============================================================================

/**
 * Output register value to monitor when bus is granted
 */
void output_register(uint8_t reg_addr, uint8_t value) {
    if(!BUS_GRANTED()) return;  // Safety check
    
    set_data_out();             // Data pins as output
    set_addr(reg_addr);         // Set register address
    write_bus(value);           // Write value to bus
    
    SYNC_DELAY();               // Hold signal
    
    // Wait briefly for Arduino Nano to capture
    for(volatile uint16_t i = 0; i < 1000; i++) {
        asm volatile("nop");
    }
}

// ============================================================================
// Main Program
// ============================================================================

void setup() {
    // Configure control pins as INPUT (monitoring Master's signals)
    DDRC &= ~0x30; // PC4, PC5 as INPUT
    
    // Initial state: Release all bus lines
    set_high_z_all();
    
    // Initialize registers
    regA = 0x00;
    regB = 0x00;
    regC = 0x00;
}

void loop() {
    static uint8_t idx = 0;
    static uint8_t phase = 0;  // 0=idle, 1=read, 2=calc, 3=write
    
    // ========================================================================
    // State Machine: Wait for Bus Grant
    // ========================================================================
    
    if(BUS_GRANTED()) {
        // Bus is granted to Core 2
        
        switch(phase) {
            // ================================================================
            // PHASE 1: Read from ROM
            // ================================================================
            case 0:
                if(IS_READ()) {
                    set_data_in();
                    set_addr(SLAVE_OFFSET + DATA_SET[idx]);
                    SYNC_DELAY();
                    
                    regB = read_bus();      // Load data into Register B
                    regC = idx;             // Update counter
                    
                    phase = 1;              // Move to calculation phase
                }
                break;
            
            // ================================================================
            // PHASE 2: Calculation
            // ================================================================
            case 1:
                regA = regB * regB;         // A = B^2 (square operation)
                phase = 2;                  // Move to write phase
                break;
            
            // ================================================================
            // PHASE 3: Output Result
            // ================================================================
            case 2:
                if(IS_WRITE()) {
                    output_register(REG_A_ADDR, regA);
                    
                    // Optional: Output other registers
                    // output_register(REG_B_ADDR, regB);
                    // output_register(REG_C_ADDR, regC);
                    
                    idx = (idx + 1) % LEN;  // Increment index
                    phase = 0;              // Return to idle
                }
                break;
        }
        
    } else {
        // ====================================================================
        // IDLE: Master owns the bus
        // ====================================================================
        set_high_z_all();
        phase = 0;  // Reset to idle state
    }
}
