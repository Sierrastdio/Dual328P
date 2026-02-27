import serial
import serial.tools.list_ports
import time
import curses

# --- 설정 및 포트 탐색 ---
def find_arduino_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if any(kw in port.description for kw in ['Arduino', 'CH340', 'USB', 'ACM']):
            return port.device
    return "/dev/ttyUSB0" # iSH/Linux 기본값 경로

SERIAL_PORT = find_arduino_port()
BAUD_RATE = 115200

class LightProgrammer:
    def __init__(self, stdscr):
        self.stdscr = stdscr
        self.editor_lines = [""]
        self.cursor_y = 0
        self.cursor_x = 0
        self.mode = "INSERT" # INSERT / COMMAND
        self.status = "READY"
        self.bank = 0
        self.page = 0
        self.ser = None
        
        # 시리얼 연결
        try:
            self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
            time.sleep(1)
            self.status = "CONNECTED"
        except:
            self.status = "SERIAL ERROR"

        self.run()

    def draw_gui(self):
        self.stdscr.clear()
        h, w = self.stdscr.getmaxyx()

        # 상단 타이틀 바
        title = f" RAM PROGRAMMER | PORT: {SERIAL_PORT} | MODE: {self.mode} "
        self.stdscr.attron(curses.A_REVERSE)
        self.stdscr.addstr(0, 0, title.ljust(w))
        self.stdscr.attroff(curses.A_REVERSE)

        # 왼쪽: 에디터 영역
        for i, line in enumerate(self.editor_lines):
            if i < h - 4:
                prefix = f"{i:03d}: "
                self.stdscr.addstr(i + 2, 2, prefix, curses.A_DIM)
                self.stdscr.addstr(i + 2, 7, line[:w-10])

        # 오른쪽: 상태창 (화면 여유 있을 때만)
        if w > 50:
            stat_x = w - 25
            self.stdscr.addstr(2, stat_x, " [SYSTEM STATUS] ", curses.A_BOLD)
            self.stdscr.addstr(3, stat_x, f" Status: {self.status}")
            self.stdscr.addstr(4, stat_x, f" Bank  : {self.bank}")
            self.stdscr.addstr(5, stat_x, f" Page  : {self.page}")
            
            self.stdscr.addstr(8, stat_x, " [HOTKEYS] ", curses.A_BOLD)
            self.stdscr.addstr(9, stat_x, " ESC  : Command Mode")
            self.stdscr.addstr(10, stat_x, " i    : Insert Mode")

        # 하단 상태바 / 명령줄
        msg = f" {self.status} | Line: {self.cursor_y} "
        self.stdscr.addstr(h-2, 0, "-" * w)
        self.stdscr.addstr(h-1, 0, msg[:w-1])

        # 커서 이동
        if self.mode == "INSERT":
            self.stdscr.move(self.cursor_y + 2, self.cursor_x + 7)
        else:
            self.stdscr.move(h-1, len(msg))

        self.stdscr.refresh()

    def handle_command(self, cmd_str):
        parts = cmd_str.lower().split()
        if not parts: return

        cmd = parts[0]
        try:
            if cmd == "w":
                self.bank = int(parts[1]) if len(parts) > 1 else 0
                self.page = int(parts[2]) if len(parts) > 2 else 0
                self.upload()
            elif cmd == "run":
                self.send_raw(":run")
                self.status = "RUNNING"
            elif cmd == "r":
                self.send_raw(":r")
                self.status = "HALTED"
            elif cmd == "clear":
                self.editor_lines = [""]
                self.cursor_y = 0
                self.cursor_x = 0
            elif cmd == "q":
                return False
        except Exception as e:
            self.status = f"ERR: {str(e)[:15]}"
        return True

    def send_raw(self, msg):
        if self.ser and self.ser.is_open:
            self.ser.write(f"{msg}\n".encode())
            return True
        return False

    def upload(self):
        if not self.ser: return
        self.status = "UPLOADING..."
        self.draw_gui()
        
        self.send_raw(":clear")
        time.sleep(0.1)
        
        for line in self.editor_lines:
            if line.strip():
                self.send_raw(line.strip())
                time.sleep(0.02)
        
        self.send_raw(f":w {self.bank} {self.page}")
        self.status = "UPLOAD DONE"

    def run(self):
        while True:
            self.draw_gui()
            ch = self.stdscr.getch()

            if self.mode == "INSERT":
                if ch == 27: # ESC
                    self.mode = "COMMAND"
                elif ch in (10, 13): # Enter
                    self.editor_lines.insert(self.cursor_y + 1, "")
                    self.cursor_y += 1
                    self.cursor_x = 0
                elif ch in (curses.KEY_BACKSPACE, 127, 8):
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
                if ch == ord('i'):
                    self.mode = "INSERT"
                elif ch == ord(':'):
                    curses.echo()
                    cmd_line = self.stdscr.getstr(self.stdscr.getmaxyx()[0]-1, 1).decode('utf-8')
                    curses.noecho()
                    if not self.handle_command(cmd_line):
                        break
                    self.mode = "INSERT"

def main():
    curses.wrapper(LightProgrammer)

if __name__ == "__main__":
    main()