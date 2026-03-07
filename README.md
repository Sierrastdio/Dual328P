# DST-wiki
## **DST is Multiprocessor System with Virtual Machine Firmware**
### main component:
* `Nano #1(SMU)`
* `shared RAM`
* `Core1`
* `Core2`
* `Nano #2(DDU)`

### parts list:
| parts type | name | number |
|-------|-------|-------|
| MCU   | `Arduino Nano`   | 2 |
| MCU   | `Atmega328P (DIP)`    | 2 |
| Memory| `62256 SRAM`     | 1 |
| IC logic | `74HC245`     | 4 |
| IC logic | `74HC595`     | 4 |
| IC logic | `74HC04`      | 1 |
| IC logic | `74HC125`     | 1 |
| IC logic | `74HC165`     | 2 |
| Oscillator | `Active Crystal`   | 1 |
********
### DST feature:
* **Core-IEE**(core Isolated Execution Environment)
* **VMF**(Virtual Machine Firmware on core)
-------------
# Main Component 
## 1. SMU (System Management Unit)

## 2. Core 1

## 3. Core 2

## 4. DDU (Data Display Unit)
------------------------------------
------------------------------------
# How it works
- Core1,2 `RESET Pin` LOW (`:rst` from SMU)
- Load Binary(8bit) in RAM (`:w <bank number> from SMU`)
- Core1,2 `RESET Pin` HIGH (`:run` from SMU)
  - -> Core starts.
- 1. `Core 1` Acquire `Bus Mastery`(`Core 1 PC4` `HIGH`)
- 2. `Core 1` Read Data from `62256 RAM` and processing Instruction Fetch & Execute
- 3. `Core 1` release`Bus Mastery` to `Core 2`(`Core 1 PC4` `LOW`)
- 4. `Core 2` Read Data from `62256 RAM` and processing Instruction Fetch & Execute



