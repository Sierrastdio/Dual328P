/*
 * ============================================================================
 * Processor 2 - Virtual Machine Firmware v7.5 (74HC595 fetch 단계 진입 시 즉시 래치)
 * ============================================================================
 *
 * Pin layout:
 * - PD2~7, PB0~1: Data bus (D0~D7)  → 74HC245 data buffer P2
 * - PB2~5, PC0~2: Address bus (A0~A6) → 74HC245 address buffer P2
 * - PD0: 74HC595-Processor2 SER (Serial Data)   ← conflicts with UART RX
 * - PD1: 74HC595-Processor2 SCK (Shift Clock)   ← conflicts with UART TX
 * - PC3: 74HC595-Processor2 RCK (Latch Clock)
 * - PC4: N/C
 * - PC5: Handshake to Core 1 PC5 (output: LOW=busy, HIGH=done)
 *
 * Paging system:
 * - 74HC595 controls A7~A13 (7 bits)
 * - 128 pages × 128 bytes = 16 KB addressable
 * - slot[15] = PAGE_REG (dedicated page register)
 *
 * Handshake:
 * - Core 2 (Processor 2): drives PC5 — LOW while executing a burst, HIGH when ready for Core 1
 * - Core 1 (Processor 1): detects Core 2 completion via PC5 interrupt (PCINT1)
 *
 * Timing read flow:
 * - HALT → timing data saved to internal EEPROM
 * - Next boot → setup() reads EEPROM and prints via UART before 595 is driven
 *
 * ============================================================================
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <util/delay.h>

// ─── Instruction Set ─────────────────────────────────────────────────────────
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

#define CACHE_SIZE  128

#define ENABLE_TIMING
#define TIMING_INTERVAL  5120UL

// ─── EEPROM layout (Core 2 offset 0x10~0x19) ─────────────────────────────────
#define EEPROM_ADDR_US    ((uint32_t*)0x10)
#define EEPROM_ADDR_INSTR ((uint32_t*)0x14)
#define EEPROM_ADDR_FLAG  ((uint8_t*) 0x18)
#define EEPROM_ADDR_REGA  ((uint8_t*) 0x19)
#define EEPROM_FLAG_VALID 0xAA

// ─── VM state ─────────────────────────────────────────────────────────────────
volatile uint8_t  regA         = 0x00;
volatile uint8_t  slot[16];
volatile uint8_t  stack[8];
volatile uint8_t  stack_ptr    = 0;
volatile uint8_t  PC           = 0;
volatile uint8_t  current_page = 0;
volatile bool     halted       = false;
volatile uint8_t  cached_page  = 0xFF;

static uint8_t inst_cache[CACHE_SIZE];

#define PAGE_REG slot[15]

// ─── 595 prefetch state ───────────────────────────────────────────────────────
static bool    page_pending     = false;
static uint8_t pending_page_val = 0;

// ─── Hardware macros ─────────────────────────────────────────────────────────
#define SIGNAL_BUSY()       PORTC &= ~(1 << 5)
#define SIGNAL_DONE()       PORTC |=  (1 << 5)

#define HC595_SER_HIGH()    PORTD |=  (1 << 0)
#define HC595_SER_LOW()     PORTD &= ~(1 << 0)
#define HC595_SCK_HIGH()    PORTD |=  (1 << 1)
#define HC595_SCK_LOW()     PORTD &= ~(1 << 1)
#define HC595_RCK_HIGH()    PORTC |=  (1 << 3)
#define HC595_RCK_LOW()     PORTC &= ~(1 << 3)

#define SYNC_DELAY()        asm volatile("nop\n\t nop\n\t nop\n\t nop\n\t")


// ============================================================================
//  TIMING SUBSYSTEM
// ============================================================================
#ifdef ENABLE_TIMING

volatile uint32_t _t1_overflows = 0;

ISR(TIMER1_OVF_vect) {
    _t1_overflows++;
}

static inline uint32_t _get_ticks() {
    uint8_t  sreg = SREG;
    cli();
    uint32_t ov = _t1_overflows;
    uint16_t t  = TCNT1;
    if ((TIFR1 & (1 << TOV1)) && t < 0x8000U) ov++;
    SREG = sreg;
    return (ov << 16) | t;
}

volatile uint32_t timing_result_us    = 0;
volatile uint32_t timing_result_instr = 0;

static uint32_t _timing_start   = 0;
static uint32_t _timing_counted = 0;
static bool     _timing_active  = false;

void timing_init() {
    TCCR1A = 0;
    TCCR1B = (1 << CS11);
    TIMSK1 = (1 << TOIE1);
    TCNT1  = 0;
    _t1_overflows = 0;
    _timing_active = false;
}

static inline void timing_start_window() {
    _timing_counted = 0;
    _timing_start   = _get_ticks();
    _timing_active  = true;
}

static inline bool timing_tick(uint8_t actual_count) {
    if (!_timing_active) return false;
    _timing_counted += actual_count;
    if (_timing_counted >= TIMING_INTERVAL) {
        uint32_t elapsed    = _get_ticks() - _timing_start;
        timing_result_us    = elapsed >> 1;
        timing_result_instr = _timing_counted;
        _timing_active = false;
        return true;
    }
    return false;
}

static void _uart_init() {
    UBRR0H = 0;
    UBRR0L = 103;
    UCSR0B = (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

static void _uart_putc(char c) {
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
}

static void _uart_puts(const char* s) {
    while (*s) _uart_putc(*s++);
}

static void _uart_putu32(uint32_t v) {
    if (v == 0) { _uart_putc('0'); return; }
    char buf[11]; int8_t i = 0;
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i--) _uart_putc(buf[i]);
}

void timing_save_eeprom() {
    eeprom_update_dword(EEPROM_ADDR_US,    timing_result_us);
    eeprom_update_dword(EEPROM_ADDR_INSTR, timing_result_instr);
    eeprom_update_byte (EEPROM_ADDR_REGA,  regA);
    eeprom_update_byte (EEPROM_ADDR_FLAG,  EEPROM_FLAG_VALID);
}

void timing_load_and_print_eeprom() {
    if (eeprom_read_byte(EEPROM_ADDR_FLAG) != EEPROM_FLAG_VALID) return;

    uint32_t us    = eeprom_read_dword(EEPROM_ADDR_US);
    uint32_t instr = eeprom_read_dword(EEPROM_ADDR_INSTR);
    uint8_t  rega  = eeprom_read_byte (EEPROM_ADDR_REGA);

    _uart_init();
    _uart_puts("[TIMING] ");
    _uart_putu32(instr);
    _uart_puts(" instr in ");
    _uart_putu32(us);
    _uart_puts(" us (");
    if (instr > 0) _uart_putu32((us * 1000UL) / instr);
    else           _uart_putc('?');
    _uart_puts(" ns/instr) regA=0x");
    _uart_putc("0123456789ABCDEF"[rega >> 4]);
    _uart_putc("0123456789ABCDEF"[rega & 0x0F]);
    _uart_puts("\r\n");

    while (!(UCSR0A & (1 << TXC0)));
    UCSR0B &= ~(1 << TXEN0);

    eeprom_update_byte(EEPROM_ADDR_FLAG, 0x00);
}

#else
#define timing_init()                   do {} while(0)
#define timing_start_window()           do {} while(0)
#define timing_tick(n)                  (false)
#define timing_save_eeprom()            do {} while(0)
#define timing_load_and_print_eeprom()  do {} while(0)
#endif
// ============================================================================


// ============================================================================
//  74HC595 — 즉시 전환
// ============================================================================
void set_page_595(uint8_t page) {
    page &= 0b01111111;
    if (page == cached_page) return;
    cached_page = page;

    HC595_RCK_LOW();
    for (uint8_t i = 0; i < 7; i++) {
        if (page & 0b01000000) HC595_SER_HIGH();
        else                   HC595_SER_LOW();
        HC595_SCK_HIGH();
        HC595_SCK_LOW();
        page <<= 1;
    }
    HC595_RCK_HIGH();
    asm volatile("nop\n\t");
    HC595_RCK_LOW();
}

// ============================================================================
//  74HC595 — execute 단계 prefetch (RCK 유보)
// ============================================================================
static void prefetch_page_595(uint8_t page) {
    page &= 0b01111111;
    if (page == cached_page) {
        page_pending = false;
        return;
    }
    pending_page_val = page;
    page_pending     = true;

    HC595_RCK_LOW();
    for (uint8_t i = 0; i < 7; i++) {
        if (page & 0b01000000) HC595_SER_HIGH();
        else                   HC595_SER_LOW();
        HC595_SCK_HIGH();
        HC595_SCK_LOW();
        page <<= 1;
    }
    // RCK 펄스 없음
}

// ============================================================================
//  fetch 단계 진입 시 즉시 래치
// ============================================================================
static inline void latch_prefetched_page() {
    if (!page_pending) return;
    cached_page  = pending_page_val;
    page_pending = false;
    HC595_RCK_HIGH();
    asm volatile("nop\n\t");
    HC595_RCK_LOW();
}


// ============================================================================
//  Address / Data bus
// ============================================================================
inline void set_addr_bus(uint8_t addr) {
    addr &= 0b01111111;
    PORTB = (PORTB & 0b11000011) | ((addr & 0b00001111) << 2);
    PORTC = (PORTC & 0b11111000) | ((addr >> 4) & 0b00000111);
}

inline void set_data_output() {
    DDRD |= 0b11111100;
    DDRB |= 0b00000011;
}

inline void set_data_input() {
    DDRD  &= 0b00000011;  PORTD &= 0b00000011;
    DDRB  &= 0b11111100;  PORTB &= 0b11111100;
}

inline void write_data_bus(uint8_t data) {
    PORTD = (PORTD & 0b00000011) | ((data << 2) & 0b11111100);
    PORTB = (PORTB & 0b11111100) | ((data >> 6) & 0b00000011);
}

inline uint8_t read_data_bus() {
    return ((PIND & 0b11111100) >> 2) | ((PINB & 0b00000011) << 6);
}


// ============================================================================
//  High-Z
//  PD0(SER), PD1(SCK), PC3(RCK) 제외 → execute 중 595 구동 가능
// ============================================================================
inline void set_high_z() {
    DDRD  &= 0b00000011;  PORTD &= 0b00000011;
    DDRB  &= 0b11000000;  PORTB &= 0b11000000;
    DDRC  &= 0b11111000;  PORTC &= 0b11111000;
}


// ============================================================================
//  Phase 1: Fetch burst
// ============================================================================
static uint8_t fetch_burst() {
    // ★ prefetch된 페이지 즉시 래치 ★
    latch_prefetched_page();

    set_data_input();
    DDRB |= 0b00111100;
    DDRC |= 0b00000111;

    for (uint8_t i = 0; i < CACHE_SIZE; i++) {
        set_addr_bus(PC);
        SYNC_DELAY();
        _delay_us(1);
        inst_cache[i] = read_data_bus();
        PC++;
        if (PC >= 128) PC = 0;
    }
    return CACHE_SIZE;
}


// ============================================================================
//  Output (OUT)
// ============================================================================
static void output_register(uint8_t value) {
    SIGNAL_BUSY();
    set_data_output();
    write_data_bus(value);
    SYNC_DELAY();
    _delay_us(50);
    set_data_input();
    set_high_z();
    SIGNAL_DONE();
}


// ============================================================================
//  Phase 2: Execute from cache
// ============================================================================
static uint8_t execute_cache(uint8_t count) {
    uint8_t actual = 0;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t opcode  = inst_cache[i] & 0xF0;
        uint8_t operand = inst_cache[i] & 0x0F;

        switch (opcode) {
            case OP_NOP:                                    break;
            case OP_LOAD:   regA  = operand;                break;
            case OP_ADD:    regA += operand;                break;
            case OP_SUB:    regA -= operand;                break;
            case OP_MUL:    regA *= operand;                break;
            case OP_AND:    regA &= operand;                break;
            case OP_OR:     regA |= operand;                break;
            case OP_FETCH:  regA = slot[operand & 0x0F];   break;
            case OP_SLOT:   slot[operand & 0x0F] = regA;   break;
            case OP_OUT:    output_register(regA);          break;

            case OP_PUSH:
                if (stack_ptr < 8) stack[stack_ptr++] = regA;
                break;

            case OP_POP:
                if (stack_ptr > 0) regA = stack[--stack_ptr];
                break;

            case OP_SETPAGE:
                current_page = PAGE_REG & 0b01111111;
                PC = 0;
                // ★ execute 중 다음 페이지 미리 시프트 ★
                prefetch_page_595(current_page);
                break;

            case OP_HALT:
                halted = true;
                actual++;
                return actual;

            default: break;
        }
        actual++;
    }
    return actual;
}


// ============================================================================
//  Setup
// ============================================================================
void setup() {
    set_data_input();
    DDRB |= 0b00111100;
    DDRC |= 0b00000111;
    DDRD |= 0b00000011;
    DDRC |= 0b00001000;
    DDRC |= 0b00100000;  // PC5 output

    regA = 0x00; PC = 0; stack_ptr = 0;
    current_page = 0; cached_page = 0xFF; halted = false;
    page_pending = false; pending_page_val = 0;
    for (uint8_t i = 0; i < 16; i++) slot[i]  = 0;
    for (uint8_t i = 0; i < 8;  i++) stack[i] = 0;
    for (uint8_t i = 0; i < CACHE_SIZE; i++) inst_cache[i] = 0;
    PAGE_REG = 0;

    timing_load_and_print_eeprom();

    set_page_595(0);

    SIGNAL_DONE();

    timing_init();
    timing_start_window();

    _delay_ms(10);
}


// ============================================================================
//  Loop
// ============================================================================
void loop() {
    if (halted) {
        set_high_z();
        SIGNAL_DONE();
        timing_save_eeprom();
        while (1);
    }

    // ── Phase 1: Fetch ────────────────────────────────────────────────────────
    SIGNAL_BUSY();
    uint8_t fetched = fetch_burst();

    set_high_z();
    SIGNAL_DONE();

    // ── Phase 2: Execute (SETPAGE 시 다음 페이지 미리 시프트) ────────────────
    uint8_t actual = execute_cache(fetched);

    if (timing_tick(actual)) {
        timing_save_eeprom();
    }
}
