import serial
import serial.tools.list_ports
import time
import curses

def find_arduino_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if any(kw in port.description for kw in ['Arduino', 'CH340', 'USB', 'ACM']):
            return port.device
    return "/dev/ttyUSB0"

SERIAL_PORT = find_arduino_port()
BAUD_RATE = 115200

class LightProgrammer:
    def __init__(self, stdscr):
        self.stdscr = stdscr
        self.editor_lines = [""]
        self.cursor_y = 0
        self.cursor_x = 0
        self.mode = "INSERT" 
        self.status = "OFFLINE"
        self.bank = 0
        self.page = 0
        self.ser = None
        self.cmd_buffer = ""
        
        curses.use_default_colors()
        self.stdscr.keypad(True)
        self.stdscr.nodelay(False) 

        self.run()

    def draw_gui(self):
        self.stdscr.erase()
        h, w = self.stdscr.getmaxyx()
        editor_width = int(w * 0.6)

        # 상단 바
        title = f" RAM PROG | {SERIAL_PORT} | {self.mode} "
        self.stdscr.attron(curses.A_REVERSE)
        self.stdscr.addstr(0, 0, title.ljust(w)[:w-1])
        self.stdscr.attroff(curses.A_REVERSE)

        # 경계선
        for i in range(1, h-2):
            if editor_width < w:
                self.stdscr.addstr(i, editor_width, "|")

        # 에디터 영역
        for i, line in enumerate(self.editor_lines):
            if i < h - 4:
                prefix = f"{i:03d}: "
                self.stdscr.addstr(i + 2, 1, prefix, curses.A_DIM)
                display_line = line[:max(0, editor_width - 8)]
                self.stdscr.addstr(i + 2, 6, display_line)

        # 상태창
        if w > editor_width + 10:
            stat_x = editor_width + 2
            self.stdscr.addstr(2,  stat_x, "[SYSTEM INFO]", curses.A_BOLD)
            self.stdscr.addstr(3,  stat_x, f"STAT: {self.status}")
            self.stdscr.addstr(4,  stat_x, f"BANK: {self.bank}")
            self.stdscr.addstr(5,  stat_x, f"PAGE: {self.page}")

            self.stdscr.addstr(7,  stat_x, "[SERIAL]", curses.A_BOLD)
            self.stdscr.addstr(8,  stat_x, ":connect [port]")
            self.stdscr.addstr(9,  stat_x, ":disconnect")
            self.stdscr.addstr(10, stat_x, ":ports")

            self.stdscr.addstr(12, stat_x, "[SMU CMD]", curses.A_BOLD)
            self.stdscr.addstr(13, stat_x, ":w <bank> <page>")
            self.stdscr.addstr(14, stat_x, ":run  - start cores")
            self.stdscr.addstr(15, stat_x, ":rst  - reset cores")  # ← :r 아님
            self.stdscr.addstr(16, stat_x, ":clear - clear buf")
            self.stdscr.addstr(17, stat_x, ":q    - quit")

            self.stdscr.addstr(19, stat_x, "[KEYS]", curses.A_BOLD)
            self.stdscr.addstr(20, stat_x, "ESC: CMD MODE")
            self.stdscr.addstr(21, stat_x, "i  : INSERT MODE")

        # 하단 바
        if self.mode == "COMMAND":
            self.stdscr.addstr(h-1, 0, f":{self.cmd_buffer}_".ljust(w-1), curses.A_BOLD)
        else:
            bottom_text = f" [{self.mode}] {self.status} (ESC for CMD)"
            self.stdscr.addstr(h-1, 0, bottom_text[:w-1])

        # 커서 위치
        if self.mode == "INSERT":
            self.stdscr.move(self.cursor_y + 2, self.cursor_x + 6)
        else:
            self.stdscr.move(h-1, len(self.cmd_buffer) + 1)
        
        self.stdscr.refresh()

    def handle_command(self):
        cmd_str = self.cmd_buffer.strip().lower()
        self.cmd_buffer = ""
        
        if not cmd_str: return True
        parts = cmd_str.split()
        cmd = parts[0]

        try:
            # ── 연결 제어 ─────────────────────────────────────────────────────
            if cmd == "connect":
                global SERIAL_PORT
                if len(parts) > 1:
                    SERIAL_PORT = parts[1]
                else:
                    SERIAL_PORT = find_arduino_port()
                try:
                    if self.ser and self.ser.is_open:
                        self.ser.close()
                    self.status = "CONNECTING..."
                    self.draw_gui()
                    self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
                    time.sleep(1)
                    self.status = "CONNECTED"
                except Exception as e:
                    self.ser = None
                    self.status = f"ERR:{str(e)[:12]}"

            elif cmd == "disconnect":
                if self.ser and self.ser.is_open:
                    self.ser.close()
                self.ser = None
                self.status = "OFFLINE"

            elif cmd == "ports":
                ports = serial.tools.list_ports.comports()
                if ports:
                    self.status = " ".join(p.device for p in ports)[:20]
                else:
                    self.status = "NO PORTS"

            # ── SMU 명령 ──────────────────────────────────────────────────────
            elif cmd == "w":
                self.bank = int(parts[1]) if len(parts) > 1 else 0
                self.page = int(parts[2]) if len(parts) > 2 else 0
                self.upload()

            elif cmd == "run":
                self.send_raw(":run")   # SMU: RELEASE_CORES → "RUN\n"
                self.status = "RUNNING"

            elif cmd == "rst":
                self.send_raw(":rst")   # SMU: RESET_CORES → "RST\n"
                self.status = "HALTED"

            elif cmd == "clear":
                self.editor_lines = [""]
                self.cursor_y = self.cursor_x = 0
                self.send_raw(":clear") # SMU: prog_sz=0 → "CLR\n"
                self.status = "CLEARED"

            elif cmd == "q":
                return False

            else:
                self.status = f"UNKNOWN: {cmd}"

        except:
            self.status = "CMD ERR"
        return True

    def send_raw(self, msg):
        if self.ser and self.ser.is_open:
            self.ser.write(f"{msg}\n".encode())
            return True
        self.status = "NOT CONNECTED"
        return False

    def upload(self):
        if not self.ser or not self.ser.is_open:
            self.status = "NOT CONNECTED"
            return

        self.status = "UPLOADING..."
        self.draw_gui()

        # SMU 버퍼 초기화
        self.send_raw(":clear")
        time.sleep(0.1)

        # 명령어 라인 전송 (SMU의 processLine 으로 전달됨)
        count = 0
        for line in self.editor_lines:
            if line.strip():
                self.send_raw(line.strip())
                time.sleep(0.02)
                count += 1

        # SMU에 쓰기 지시: :w <bank> <page>
        # SMU 응답: "B{bank}:P{page} {size}B\n...\nOK\n"
        self.ser.write(f":w {self.bank} {self.page}\n".encode())
        time.sleep(0.5)

        resp = self.ser.read_all().decode('utf-8', errors='ignore')
        if "OK" in resp:
            self.status = f"OK {count}L B{self.bank}P{self.page}"
        else:
            self.status = "NO OK - CHECK CONN"

    def run(self):
        while True:
            self.draw_gui()
            ch = self.stdscr.getch()

            if self.mode == "INSERT":
                if ch == 27: # ESC
                    self.mode = "COMMAND"
                    self.cmd_buffer = ""
                elif ch in (10, 13, curses.KEY_ENTER):
                    self.editor_lines.insert(self.cursor_y + 1, "")
                    self.cursor_y += 1
                    self.cursor_x = 0
                elif ch in (curses.KEY_BACKSPACE, 127, 8, ord('\b')):
                    if self.cursor_x > 0:
                        line = self.editor_lines[self.cursor_y]
                        self.editor_lines[self.cursor_y] = line[:self.cursor_x-1] + line[self.cursor_x:]
                        self.cursor_x -= 1
                    elif self.cursor_y > 0:
                        prev_len = len(self.editor_lines[self.cursor_y-1])
                        self.editor_lines[self.cursor_y-1] += self.editor_lines.pop(self.cursor_y)
                        self.cursor_y -= 1
                        self.cursor_x = prev_len
                elif 32 <= ch <= 126:
                    line = self.editor_lines[self.cursor_y]
                    self.editor_lines[self.cursor_y] = line[:self.cursor_x] + chr(ch) + line[self.cursor_x:]
                    self.cursor_x += 1
                elif ch == curses.KEY_UP and self.cursor_y > 0:
                    self.cursor_y -= 1
                    self.cursor_x = min(self.cursor_x, len(self.editor_lines[self.cursor_y]))
                elif ch == curses.KEY_DOWN and self.cursor_y < len(self.editor_lines)-1:
                    self.cursor_y += 1
                    self.cursor_x = min(self.cursor_x, len(self.editor_lines[self.cursor_y]))

            elif self.mode == "COMMAND":
                if ch in (10, 13, curses.KEY_ENTER):
                    if not self.handle_command():
                        break
                    self.mode = "INSERT"
                elif ch == 27:
                    self.cmd_buffer = ""
                    self.mode = "INSERT"
                elif ch in (curses.KEY_BACKSPACE, 127, 8, ord('\b')):
                    self.cmd_buffer = self.cmd_buffer[:-1]
                elif 32 <= ch <= 126:
                    if ch == ord(':') and not self.cmd_buffer:
                        pass
                    else:
                        self.cmd_buffer += chr(ch)

def main():
    curses.wrapper(LightProgrammer)

if __name__ == "__main__":
    main()