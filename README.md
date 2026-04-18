<!-- markdownlint-disable -->
# Multiprocessor System with Virtual Machine Firmware
### main component:
* `Arduino Mega 2560(SMU)`
* `shared RAM`
* `Processor1`
* `Processor2`
* `Arduino Nano(DDU)`

### parts list:
| parts type | name | number |
|-------|-------|-------|
| MCU   | `Arduino Mega 2560`   | 1 |
| MCU   | `Arduino Nano`   | 1 |
| MCU   | `Atmega328P (DIP)`    | 2 |
| Memory| `62256 SRAM`     | 1 |
| IC logic | `74HC245`     | 4 |
| IC logic | `74HC595`     | 2 |
| IC logic | `74HC04`      | 1 |
| IC logic | `74HC125`     | 1 |
| IC logic | `74HC165`     | 2 |
| Oscillator | `Active Crystal 16MHz`   | 1 |
********
### DST feature:

* **Processor-IEE**(processor Isolated Execution Environment)

* **VMF**(Virtual Machine Firmware on processor)

-------------

# Main Component

## 1. SMU (System Management Unit)

The SMU is the unit responsible for overall system management, based on Arduino Mega 2560. It handles processor resets, memory programming, bus control, etc.

## 2. Processor 1

Processor 1 is the first processor based on Atmega328P. It fetches and executes instructions from shared RAM, controlling data access through bus mastery.

## 3. Processor 2

Processor 2 is the second processor based on Atmega328P. It alternates with Processor 1 to execute instructions using shared RAM.

## 4. DDU (Data Display Unit)

The DDU is the data display unit, using Arduino Nano to display system status or outputs.

********

********

# How it works

- Processor1,2 `RESET Pin` LOW (`:rst` from SMU)
- Load Binary(8bit) in RAM (`:w <bank number> from SMU`)
- Processor1,2 `RESET Pin` HIGH (`:run` from SMU)
  - -> Processor starts.
- 1. `Processor 1` Acquire `Bus Mastery`(`Processor 1 PC4` `HIGH`)
- 2. `Processor 1` Read Data from `62256 RAM` and processing Instruction Fetch & Execute
- 3. `Processor 1` release`Bus Mastery` to `Processor 2`(`Processor 1 PC4` `LOW`)
- 4. `Processor 2` Read Data from `62256 RAM` and processing Instruction Fetch & Execute


# Fuse Bit Configuration
When you use `Active Crystal(Oscillator)`, You need to configure the Fuse Bits to **enable clock output** on `Processor 1` and `Processor 2`.
If you don't have an active crystal, you can use this method(both methods need to configure fuse bits unless you use `Passive Crystal`.). This section explains how to configure fuse bits using only Arduino without a separate oscillator or active crystal to set up 16MHz generator and read mode.

1. Connect the Uno (A, programmer) to the PC via USB.

2. In Arduino IDE, select File > Examples > 11.ArduinoISP > ArduinoISP and upload it to the Uno to be used as programmer.

3. Connect the programmer Uno (A) and the target Nano (B) as per the table below:

| Programmer Uno (A) | Target Nano (B) |
|--------------------|-----------------|
| D10               | RESET          |
| D11               | D11            |
| D12               | D12            |
| D13               | D13            |
| 5V                | 5V             |
| GND               | GND            |

4. Connect the computer and the programmer Arduino (A) with the upload cable.

5. Windows Environment (PowerShell based)

   Change to the avrdude directory:
   ```powershell
   cd "C:\Users\user\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17\bin"
   ```

   Verify connection:
   ```powershell
   .\avrdude.exe -C ..\etc\avrdude.conf -c avrisp -p m328p -P COM3 -b 19200 -v
   ```
   Note: Ensure the COM port (COM3) and baud rate (19200) are consistent with your setup.

   Set 16MHz CKOUT (Nano fuse manipulation):
   ```powershell
   .\avrdude.exe -C ..\etc\avrdude.conf -c avrisp -p m328p -P COM3 -b 19200 -U lfuse:w:0xBF:m
   ```

   Set clock read mode:
   ```powershell
   .\avrdude.exe -C ..\etc\avrdude.conf -c avrisp -p m328p -P COM3 -b 19200 -U lfuse:w:0xE0:m
   ```

   Set default:
   ```powershell
   .\avrdude.exe -C ..\etc\avrdude.conf -c avrisp -p m328p -P COM3 -b 19200 -U lfuse:w:0xFF:m -U hfuse:w:0xDA:m -U efuse:w:0xFD:m
   ```

# Usage
1. Initialize the processors via SMU.
2. Generate binaries using the code generator.
3. Control the system through the user interface.

# Examples
The `examples/` folder contains simple ASM examples:
- `test1.asm`: Basic test code
- `test2.asm`: Advanced test code

# Contributing


# License
This project is under the GNU General Public License v3.0 license. See the `LICENSE` file for details.

Third-Party Credits:
Modified ArduinoISP: This project includes a modified version of the ArduinoISP sketch.

Original Author: Copyright (c) 2008-2011 Randall Bohn (BSD License).

Modifications: include 8MHz clock output on D9.

The original copyright notices are preserved in the source code as per the BSD license requirements.


