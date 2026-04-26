/*
 * ============================================================================
 * Processor 1 - Virtual Machine Firmware v6.0 (Timer added)
 * ============================================================================
 *
 * Pin layout:
 * - PD2~7, PB0~1: Data bus (D0~D7)  → 74HC245 data buffer P1
 * - PB2~5, PC0~2: Address bus (A0~A6) → 74HC245 address buffer P1
 * - PD0: 74HC595-Processor1 SER (Serial Data)   ← conflicts with UART RX
 * - PD1: 74HC595-Processor1 SCK (Shift Clock)   ← conflicts with UART TX
 * - PC3: 74HC595-Processor1 RCK (Latch Clock)
 * - PC4: Processor Select (0=Processor1 Active) + EEPROM A14
 * - PC5: Processor 2 done signal input (LOW=busy, HIGH=done)
 *
 * Paging system:
 * - 74HC595 controls A7~A13 (7 bits)
 * - 128 pages × 128 bytes = 16 KB addressable
 * - slot[15] = PAGE_REG (dedicated page register)
 *
 * Handshake:
 * - Core 2 (Processor 2): sends PC5 signal on task completion (High/Low)
 * - Core 1 (Processor 1): detects Core 2 completion via PC5 interrupt (PCINT1)
 *
 * Timing read flow:
 * - HALT → timing data saved to internal EEPROM
 * - Next boot → setup() reads EEPROM and prints via UART before 595 is driven
 *
 * ============================================================================
 */

#include <Arduino.h>
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

// ─── Burst mode ───────────────────────────────────────────────────────────────
#define BURST_SIZE  4   // instructions executed per bus acquisition

// ─── Timing measurement ───────────────────────────────────────────────────────
//
//  Enable:  uncomment #define ENABLE_TIMING
//  Disable: comment it out → zero runtime overhead (all timing code compiles away)
//
//  Timer1 runs free with prescaler 8  →  1 tick = 0.5 µs @ 16 MHz
//  Overflow ISR extends counter to 32 bits  →  ~35 min before wrap
//
//  Result flow:
//    HALT → eeprom_update saves 9 bytes (~30 ms, after execution)
//    Next power-on → setup() reads EEPROM, prints via UART (595 not yet driven)
//    → flag cleared → normal execution begins
//
#define ENABLE_TIMING
#define TIMING_INTERVAL  5120UL   // measure every N instructions

// ─── EEPROM layout (9 bytes total) ───────────────────────────────────────────
//  0x00~0x03 : timing_result_us    (uint32_t, little-endian)
//  0x04~0x07 : timing_result_instr (uint32_t, little-endian)
//  0x08      : flag  0xAA = valid data present
#define EEPROM_ADDR_US    ((uint32_t*)0x00)
#define EEPROM_ADDR_INSTR ((uint32_t*)0x04)
#define EEPROM_ADDR_FLAG  ((uint8_t*) 0x08)
#define EEPROM_FLAG_VALID 0xAA

// ─── VM state ─────────────────────────────────────────────────────────────────
volatile uint8_t  regA         = 0x00;
volatile uint8_t  slot[16];
volatile uint8_t  stack[8];
volatile uint8_t  stack_ptr    = 0;
volatile uint8_t  PC           = 0;
volatile uint8_t  current_page = 0;
volatile bool     halted       = false;
volatile bool     core2_ready  = true;
volatile uint8_t  cached_page  = 0xFF;

#define PAGE_REG slot[15]

// ─── Hardware macros ─────────────────────────────────────────────────────────
#define ACTIVATE_CORE1()    PORTC &= ~(1 << 4)
#define RELEASE_TO_CORE2()  PORTC |=  (1 << 4)
#define IS_CORE2_DONE()     (PINC & (1 << 5))

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
    TCCR1B = (1 << CS11);    // prescaler 8 → 0.5 µs/tick @ 16 MHz
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
        timing_result_us    = elapsed >> 1;   // ticks × 0.5 → µs
        timing_result_instr = _timing_counted;
        _timing_active = false;
        return true;
    }
    return false;
}

// ── UART (safe only before 595 is driven) ────────────────────────────────────
static void _uart_init() {
    UBRR0H = 0;
    UBRR0L = 103;   // 9600 baud @ 16 MHz
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

// ── EEPROM save (called at HALT) ──────────────────────────────────────────────
// eeprom_update_* skips write if value unchanged → minimises wear
// 9 bytes × ~3.3 ms = ~30 ms total, well after execution ends
void timing_save_eeprom() {
    eeprom_update_dword(EEPROM_ADDR_US,    timing_result_us);
    eeprom_update_dword(EEPROM_ADDR_INSTR, timing_result_instr);
    eeprom_update_byte (EEPROM_ADDR_FLAG,  EEPROM_FLAG_VALID);
}

// ── EEPROM load + print (called at next boot, before 595 is driven) ───────────
void timing_load_and_print_eeprom() {
    if (eeprom_read_byte(EEPROM_ADDR_FLAG) != EEPROM_FLAG_VALID) return;

    uint32_t us    = eeprom_read_dword(EEPROM_ADDR_US);
    uint32_t instr = eeprom_read_dword(EEPROM_ADDR_INSTR);

    _uart_init();
    _uart_puts("[TIMING] ");
    _uart_putu32(instr);
    _uart_puts(" instr in ");
    _uart_putu32(us);
    _uart_puts(" us (");
    if (instr > 0) _uart_putu32((us * 1000UL) / instr);
    else           _uart_putc('?');
    _uart_puts(" ns/instr)\r\n");

    eeprom_update_byte(EEPROM_ADDR_FLAG, 0x00);  // clear flag
}

#else
#define timing_init()                   do {} while(0)
#define timing_start_window()           do {} while(0)
#define timing_tick(n)                  (false)
#define timing_save_eeprom()            do {} while(0)
#define timing_load_and_print_eeprom()  do {} while(0)
#endif  // ENABLE_TIMING
// ============================================================================


// ============================================================================
//  ISR — Core 2 done signal
// ============================================================================
ISR(PCINT1_vect) {
    if (IS_CORE2_DONE()) core2_ready = true;
}


// ============================================================================
//  74HC595 page register
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
//  Address bus
// ============================================================================
inline void set_addr_bus(uint8_t addr) {
    addr &= 0b01111111;
    PORTB = (PORTB & 0b11000011) | ((addr & 0b00001111) << 2);
    PORTC = (PORTC & 0b11111000) | ((addr >> 4) & 0b00000111);
}


// ============================================================================
//  Data bus I/O
// ============================================================================
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
// ============================================================================
inline void set_high_z() {
    DDRD  &= 0b00000011;  PORTD &= 0b00000011;
    DDRB  &= 0b11000000;  PORTB &= 0b11000000;
    DDRC  &= 0b11111000;  PORTC &= 0b11111000;
}


// ============================================================================
//  Fetch
// ============================================================================
uint8_t fetch() {
    ACTIVATE_CORE1();
    set_data_input();
    DDRB |= 0b00111100;
    DDRC |= 0b00000111;
    set_addr_bus(PC);
    SYNC_DELAY();
    _delay_us(5);
    return read_data_bus();
}


// ============================================================================
//  Output
// ============================================================================
void output_register(uint8_t value) {
    set_data_output();
    write_data_bus(value);
    SYNC_DELAY();
    _delay_us(50);
    set_data_input();
}


// ============================================================================
//  Execute
// ============================================================================
void execute(uint8_t instruction) {
    uint8_t opcode  = instruction & 0xF0;
    uint8_t operand = instruction & 0x0F;

    switch (opcode) {
        case OP_NOP:                                    break;
        case OP_LOAD:   regA  = operand;                break;
        case OP_ADD:    regA += operand;                break;
        case OP_SUB:    regA -= operand;                break;
        case OP_MUL:    regA *= operand;                break;
        case OP_AND:    regA &= operand;                break;
        case OP_OR:     regA |= operand;                break;
        case OP_OUT:    output_register(regA);          break;
        case OP_FETCH:  regA = slot[operand & 0x0F];    break;
        case OP_SLOT:   slot[operand & 0x0F] = regA;    break;

        case OP_PUSH:
            if (stack_ptr < 8) stack[stack_ptr++] = regA;
            break;

        case OP_POP:
            if (stack_ptr > 0) regA = stack[--stack_ptr];
            break;

        case OP_SETPAGE:
            current_page = PAGE_REG & 0b01111111;
            PC = 0;
            set_page_595(current_page);
            break;

        case OP_HALT:
            halted = true;
            break;

        default: break;
    }
}


// ============================================================================
//  Setup
// ============================================================================
void setup() {
    set_data_input();
    DDRB  |= 0b00111100;
    DDRC  |= 0b00000111;
    DDRD  |= 0b00000011;
    DDRC  |= 0b00001000;
    DDRC  |= 0b00010000;
    DDRC  &= ~0b00100000;   // PC5 input
    PORTC &= ~(1 << 5);     // pull-up disabled

    PCICR  |= (1 << PCIE1);
    PCMSK1 |= (1 << PCINT13);
    sei();

    ACTIVATE_CORE1();

    regA = 0x00; PC = 0; stack_ptr = 0;
    current_page = 0; cached_page = 0xFF; halted = false; core2_ready = true;
    for (uint8_t i = 0; i < 16; i++) slot[i]  = 0;
    for (uint8_t i = 0; i < 8;  i++) stack[i] = 0;
    PAGE_REG = 0;

    // ★ 595가 구동되기 전 — UART 안전 구간 ★
    // 이전 실행에서 저장된 타이밍 결과가 있으면 출력 후 flag 클리어
    timing_load_and_print_eeprom();

    set_page_595(0);    // ← 여기서부터 595 구동 시작 (UART 사용 불가)

    timing_init();
    timing_start_window();

    _delay_ms(100);
}


// ============================================================================
//  Loop
// ============================================================================
void loop() {
    if (halted) {
        set_high_z();
        RELEASE_TO_CORE2();
        timing_save_eeprom();   // ~30 ms, 실행 종료 후라 측정값 오염 없음
        while (1);
    }

    uint8_t actual = 0;
    for (uint8_t i = 0; i < BURST_SIZE; i++) {
        execute(fetch());
        PC++;
        if (PC >= 128) PC = 0;
        actual++;
        if (halted) break;
    }

    timing_tick(actual);

    set_high_z();
    RELEASE_TO_CORE2();
    core2_ready = false;

    while (!core2_ready) asm volatile("nop");

    ACTIVATE_CORE1();
}


/*
 * ============================================================================
 * Performance summary
 * ============================================================================
 *
 * Timing result read flow:
 *   Run → HALT → EEPROM save (9 bytes, ~30 ms)
 *   Power cycle → boot → UART print (595 not yet driven) → flag cleared
 *   No extra pins / ISP / chip removal needed
 *
 * Timing subsystem overhead (ENABLE_TIMING active):
 *   Per burst : 1× uint32_t add + 1× compare  (~2–3 CPU cycles)
 *   At HALT   : EEPROM write ~30 ms, after execution — no measurement impact
 *   Disabled  : #undef ENABLE_TIMING → zero overhead, zero code size impact
 * ============================================================================
 */
