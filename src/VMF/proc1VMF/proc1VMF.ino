/*
 * ============================================================================
 * Processor 1 - Virtual Machine Firmware v7.0 (cache memory structure added)
 * ============================================================================
 * 혁명적 변경:
 * - 명령어 캐시 버퍼 도입 (4바이트)
 * - Fetch 단계와 Execute 단계 완전 분리
 * - Execute 중 버스 불필요 → 진짜 병렬 처리!
 * 
 * 파이프라인:
 * Phase 1 (Fetch):   버스 획득 → 4개 명령어 읽기 → 버퍼 저장 → 버스 반납
 * Phase 2 (Execute): 버퍼에서 읽어서 실행 (버스 불필요!)
 * 
 * 이론 성능:
 * - Fetch: 20us (버스 필요)
 * - Execute: 4us (내부 처리, Core 2와 동시 진행!)
 * - 총: 24us (기존 48us 대비 2배!)
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

// ─── Cache / burst ────────────────────────────────────────────────────────────
#define CACHE_SIZE  4   // instructions fetched per bus acquisition

// ─── Timing measurement ───────────────────────────────────────────────────────
#define ENABLE_TIMING
#define TIMING_INTERVAL  5120UL

// ─── EEPROM layout (10 bytes) ─────────────────────────────────────────────────
//  0x00~0x03 : timing_result_us
//  0x04~0x07 : timing_result_instr
//  0x08      : flag 0xAA = valid
//  0x09      : regA final value
#define EEPROM_ADDR_US    ((uint32_t*)0x00)
#define EEPROM_ADDR_INSTR ((uint32_t*)0x04)
#define EEPROM_ADDR_FLAG  ((uint8_t*) 0x08)
#define EEPROM_ADDR_REGA  ((uint8_t*) 0x09)
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

// ─── Instruction cache ────────────────────────────────────────────────────────
static uint8_t inst_cache[CACHE_SIZE];

#define PAGE_REG slot[15]
// slot[14] = TEMP_SLOT (compiler reserved — do NOT use here)

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
// ============================================================================
inline void set_high_z() {
    DDRD  &= 0b00000011;  PORTD &= 0b00000011;
    DDRB  &= 0b11000000;  PORTB &= 0b11000000;
    DDRC  &= 0b11111000;  PORTC &= 0b11111000;
}


// ============================================================================
//  Phase 1: Fetch burst into cache (bus required)
// ============================================================================
static uint8_t fetch_burst() {
    // 버스 이미 획득된 상태에서 호출
    set_data_input();
    DDRB |= 0b00111100;
    DDRC |= 0b00000111;

    uint8_t count = 0;
    for (uint8_t i = 0; i < CACHE_SIZE; i++) {
        set_addr_bus(PC);
        SYNC_DELAY();
        _delay_us(1);               // 62256: 100ns max, 1µs = 충분
        inst_cache[i] = read_data_bus();
        PC++;
        if (PC >= 128) PC = 0;
        count++;
    }
    return count;
}


// ============================================================================
//  Output (OUT 명령어 — execute 중 버스 재획득 필요)
//  Core2가 버스를 쓰고 있을 수 있으므로 완료 대기 후 획득
// ============================================================================
static void output_register(uint8_t value) {
    // Core2 완료 대기
    while (!core2_ready) asm volatile("nop");

    // 버스 재획득
    ACTIVATE_CORE1();
    set_data_output();
    write_data_bus(value);
    SYNC_DELAY();
    _delay_us(50);
    set_data_input();

    // 버스 반납 후 Core2 재개
    set_high_z();
    core2_ready = false;
    RELEASE_TO_CORE2();
}


// ============================================================================
//  Phase 2: Execute from cache (bus NOT required — true parallel window)
//  Returns actual executed count
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

            case OP_OUT:
                // ★ 버스 재획득 필요 — Core2 완료 대기 포함 ★
                output_register(regA);
                break;

            case OP_PUSH:
                if (stack_ptr < 8) stack[stack_ptr++] = regA;
                break;

            case OP_POP:
                if (stack_ptr > 0) regA = stack[--stack_ptr];
                break;

            case OP_SETPAGE:
                // 595는 Core1 전용 — 버스 충돌 없음
                current_page = PAGE_REG & 0b01111111;
                PC = 0;
                set_page_595(current_page);
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
    for (uint8_t i = 0; i < CACHE_SIZE; i++) inst_cache[i] = 0;
    PAGE_REG = 0;

    timing_load_and_print_eeprom();  // 595 구동 전 UART 안전 구간

    set_page_595(0);    // 여기서부터 595 구동

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
        RELEASE_TO_CORE2();
        timing_save_eeprom();
        while (1);
    }

    // ── Phase 1: FETCH (버스 점유) ────────────────────────────────────────────
    ACTIVATE_CORE1();
    uint8_t fetched = fetch_burst();

    // Core2에 버스 넘기기 전에 플래그 초기화 (경쟁 상태 방지)
    core2_ready = false;
    set_high_z();
    RELEASE_TO_CORE2();

    // ── Phase 2: EXECUTE (버스 불필요 — Core2 fetch와 진짜 병렬!) ────────────
    uint8_t actual = execute_cache(fetched);

    // 타이밍 기록
    if (timing_tick(actual)) {
        timing_save_eeprom();
    }

    // ── 동기화: Core2 완료 대기 ───────────────────────────────────────────────
    // OUT 명령어가 없었다면 Core2가 아직 실행 중일 수 있음
    while (!core2_ready) asm volatile("nop");
}