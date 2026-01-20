/*
 * ============================================================================
 * Core 2 (Slave) - Co-processor
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
 * Operation Logic:
 * ----------------
 * Inverter (74HC04) flips PC4 signal.
 * When Master gives bus (PC4 LOW), Inverter outputs HIGH to Slave.
 * Slave polls PC4 and activates when HIGH is detected.
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

// ============================================================================
// Hardware Monitoring Macros
// ============================================================================

// Bus Grant Detection (inverted by 74HC04)
#define BUS_GRANTED()   (PINC & (1 << 4))   // HIGH = Bus granted to Slave

// Direction Detection (set by Master's PC5)
#define IS_READ()       (!(PINC & (1 << 5))) // LOW  = Read phase

// Timing
#define SYNC_DELAY()    asm volatile("nop\n\t nop\n\t nop\n\t")

// ============================================================================
// Hardware Abstraction Layer
// ============================================================================

/**
 * Set ALL bus pins (data + address) to High-Z
 * CRITICAL: Must release address bus too to prevent conflicts with Master!
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
    DDRB |= 0x3C; DDRC |= 0x07; // Set Addr pins to OUTPUT first
    PORTB = (PORTB & 0xC3) | ((a & 0x0F) << 2);
    PORTC = (PORTC & 0xF8) | ((a >> 4) & 0x07);
}

/**
 * Configure data bus pins as OUTPUT (for writing)
 */
inline void set_data_out() { 
    DDRD |= 0xFC; 
    DDRB |= 0x03; 
}

/**
 * Configure data bus pins as INPUT (for reading)
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
// Main Program
// ============================================================================

void setup() {
    // Configure control pins as INPUT (monitoring Master's signals)
    DDRC &= ~0x30; // PC4, PC5 as INPUT
    
    // Initial state: Release all bus lines to avoid conflicts
    set_high_z_all();
}

void loop() {
    static uint8_t idx = 0;          // Current data index
    static uint8_t res = 0;          // Latched calculation result
    static bool wrote = false;       // Prevents duplicate writes
    
    // ========================================================================
    // Bus Access State Machine (Polling-based)
    // ========================================================================
    
    if(BUS_GRANTED()) {
        // Bus is granted to Slave by Master
        
        if(IS_READ()) {
            // ================================================================
            // READ PHASE: Fetch from ROM and Calculate
            // ================================================================
            set_data_in();                            // Data pins as input
            set_addr(SLAVE_OFFSET + DATA_SET[idx]);   // Set ROM address
            SYNC_DELAY();                             // Wait for stabilization
            
            uint8_t raw = read_bus();                 // Read from ROM
            res = raw * raw;                          // Immediate Calculation (Square)
            wrote = false;                            // Reset write flag
            
        } else {
            // ================================================================
            // WRITE PHASE: Send Result to Monitor
            // ================================================================
            set_data_out();                           // Data pins as output
            write_bus(res);                           // Write calculated result
            SYNC_DELAY();                             // Ensure stable write
            
            // Increment index only once per write cycle
            if(!wrote) {
                idx = (idx + 1) % LEN;
                wrote = true;
            }
        }
    } else {
        // ====================================================================
        // IDLE PHASE: Master owns the bus
        // ====================================================================
        set_high_z_all();                             // Release all pins (Hi-Z)
    }
}
