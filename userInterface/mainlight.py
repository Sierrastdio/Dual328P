import curses
import serial
import serial.tools.list_ports
import time

# ─── Serial 설정 ────────────────────────────────────────────────────────────────

def find_arduino_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if any(k in (port.description or "") for k in ('Arduino', 'CH340', 'USB')):
            return port.device
    if ports:
        return ports[0].device
    return "COM4"  # ← Arduino IDE에서 포트 확인 후 수정

SERIAL_PORT = find_arduino_port()
BAUD_RATE   = 115200

# ─── 색상 쌍 인덱스 ──────────────────────────────────────────────────────────────
C_NORMAL = 1   # 노랑 on 검정
C_TITLE  = 2   # 검정 on 노랑

# ─── 상태 ────────────────────────────────────────────────────────────────────────
class State:
    def __init__(self):
        self.lines      = [""]
        self.cur_row    = 0
        self.cur_col    = 0
        self.mode       = "INSERT"
        self.cmd        = ":"
        self.notify     = "Editor ready. Serial not connected. Use :connect"
        self.notify_err = False
        self.bank       = 0
        self.page       = 0
        self.hw_status  = "OFFLINE"
        self.ser        = None

    def set_notify(self, msg, err=False):
        self.notify     = msg
        self.notify_err = err

    # ── serial ───────────────────────────────────────────────────────────────
    def connect(self, port=None):
        global SERIAL_PORT
        if port:
            SERIAL_PORT = port
        try:
            if self.ser and self.ser.is_open:
                self.ser.close()
            self.set_notify(f"Connecting to {SERIAL_PORT}...")
            self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1, write_timeout=1)
            time.sleep(2)
            self.hw_status = "READY"
            self.set_notify(f"Connected: {SERIAL_PORT}")
            return True
        except Exception as e:
            self.set_notify(f"Connection failed: {e}", err=True)
            self.ser        = None
            self.hw_status  = "OFFLINE"
            return False

    def disconnect(self):
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except:
                pass
        self.ser       = None
        self.hw_status = "OFFLINE"
        self.set_notify("Disconnected")

    def _send(self, cmd):
        if not self.ser or not self.ser.is_open:
            self.set_notify("Not connected! Use :connect", err=True)
            return False
        try:
            self.ser.write(f"{cmd}\n".encode())
            time.sleep(0.1)
            resp = self.ser.read_all().decode('utf-8', errors='ignore')
            return "OK" in resp
        except Exception as e:
            self.set_notify(f"Send error: {e}", err=True)
            return False

    def upload(self):
        if not self.ser or not self.ser.is_open:
            self.set_notify("Not connected! Use :connect", err=True)
            return False
        try:
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            self.ser.write(b":clear\n")
            time.sleep(0.1)

            count = 0
            for inst in self.lines:
                inst = inst.strip()
                if inst:
                    self.ser.write(f"{inst}\n".encode())
                    time.sleep(0.03)
                    count += 1

            self.ser.write(f":w {self.bank} {self.page}\n".encode())
            time.sleep(0.5)
            resp = self.ser.read_all().decode('utf-8', errors='ignore')

            if "OK" in resp:
                self.hw_status = "LOADED"
                self.set_notify(f"Uploaded {count} lines -> Bank {self.bank}, Page {self.page}")
                return True
            else:
                self.set_notify("Upload failed (no OK from device)", err=True)
                return False
        except Exception as e:
            self.set_notify(f"Upload error: {e}", err=True)
            return False

    # ── command ──────────────────────────────────────────────────────────────
    def exec_cmd(self, text):
        args = text.split()
        if not args:
            return None
        cmd = args[0]

        # ── 연결 관련 ──────────────────────────────────────────────────────
        if cmd == ":connect":
            # :connect          -> 자동감지 포트로 연결
            # :connect /dev/ttyX -> 지정 포트로 연결
            port = args[1] if len(args) > 1 else None
            self.connect(port)

        elif cmd == ":disconnect":
            self.disconnect()

        elif cmd == ":ports":
            ports = serial.tools.list_ports.comports()
            if ports:
                names = "  ".join(p.device for p in ports)
                self.set_notify(f"Ports: {names}")
            else:
                self.set_notify("No serial ports found", err=True)

        elif cmd == ":status":
            connected = self.ser and self.ser.is_open
            self.set_notify(
                f"{'CONNECTED' if connected else 'OFFLINE'}  "
                f"Port:{SERIAL_PORT}  HW:{self.hw_status}"
            )

        # ── 기존 명령 ──────────────────────────────────────────────────────
        elif cmd == ":w":
            self.bank = args[1] if len(args) > 1 else "0"
            self.page = args[2] if len(args) > 2 else "0"
            self.upload()

        elif cmd == ":run":
            if self._send(":run"):
                self.hw_status = "RUNNING"
                self.set_notify("Cores started")

        elif cmd == ":r":
            if self._send(":r"):
                self.hw_status = "HALTED"
                self.set_notify("Cores reset")

        elif cmd == ":clear":
            self.lines   = [""]
            self.cur_row = 0
            self.cur_col = 0
            self._send(":clear")
            self.set_notify("Editor cleared")

        elif cmd == ":q":
            return "QUIT"

        else:
            self.set_notify(f"Unknown command: {cmd}", err=True)

        return None

# ─── 그리기 ───────────────────────────────────────────────────────────────────

def draw_border(win, title=""):
    win.box()
    if title:
        _, w = win.getmaxyx()
        label = f" {title} "
        x = max(1, (w - len(label)) // 2)
        try:
            win.attron(curses.color_pair(C_TITLE) | curses.A_BOLD)
            win.addstr(0, x, label)
            win.attroff(curses.color_pair(C_TITLE) | curses.A_BOLD)
        except curses.error:
            pass

def safe_addstr(win, y, x, text, attr=0):
    h, w = win.getmaxyx()
    if y < 0 or y >= h:
        return
    space = w - x - 1
    if space <= 0:
        return
    try:
        win.addstr(y, x, text[:space], attr)
    except curses.error:
        pass

def draw_editor(win, state, scroll_offset):
    win.erase()
    draw_border(win, "EDITOR")
    h, w  = win.getmaxyx()
    inner = h - 2
    attr  = curses.color_pair(C_NORMAL)

    for i in range(inner):
        row = i + scroll_offset
        if row >= len(state.lines):
            break
        prefix = f"{row:04d}: "
        safe_addstr(win, i + 1, 1, (prefix + state.lines[row])[: w - 2], attr)

    csr = state.cur_row - scroll_offset
    if 0 <= csr < inner:
        prefix = f"{state.cur_row:04d}: "
        cx = 1 + len(prefix) + state.cur_col
        try:
            win.move(csr + 1, min(cx, w - 2))
        except curses.error:
            pass
    win.refresh()

def draw_status(win, state):
    win.erase()
    draw_border(win, "SYSTEM STATUS")
    attr      = curses.color_pair(C_NORMAL)
    connected = state.ser and state.ser.is_open
    ser_str   = "CONNECTED" if connected else "OFFLINE"
    base      = "0x0000" if str(state.bank) == "0" else "0x4000"
    lines = [
        "[HARDWARE]",
        f"  Port   : {SERIAL_PORT}",
        f"  Serial : {ser_str}",
        f"  Status : {state.hw_status}",
        "",
        "[TARGET]",
        f"  Bank   : {state.bank} (Core {int(state.bank)+1})",
        f"  Base   : {base}",
        f"  Page   : {state.page}/127",
        f"  Offset : 0x{int(state.page)*128:04X}",
    ]
    for i, l in enumerate(lines):
        safe_addstr(win, i + 1, 1, l, attr)
    win.refresh()

def draw_guide(win):
    win.erase()
    draw_border(win, "QUICK GUIDE")
    attr  = curses.color_pair(C_NORMAL)
    lines = [
        "[SERIAL]",
        " :connect [port]",
        " :disconnect",
        " :ports   - list ports",
        " :status  - check status",
        "",
        "[WRITE]",
        " :w <B> <P>  Write ROM",
        " :run        Start cores",
        " :r          Reset cores",
        " :clear      Clear editor",
        " :q          Quit",
        "",
        "[MODES]",
        " ESC -> COMMAND",
        " i   -> INSERT",
    ]
    for i, l in enumerate(lines):
        safe_addstr(win, i + 1, 1, l, attr)
    win.refresh()

def draw_statusbar(stdscr, state):
    h, w   = stdscr.getmaxyx()
    attr_n = curses.color_pair(C_NORMAL)
    attr_t = curses.color_pair(C_TITLE) | curses.A_BOLD

    mode_str = f" {state.mode} "
    try:
        stdscr.addstr(h - 1, 0, mode_str, attr_t)
    except curses.error:
        pass

    if state.mode == "COMMAND":
        safe_addstr(stdscr, h - 1, len(mode_str), state.cmd[: w - len(mode_str) - 1], attr_n)
    else:
        attr = (curses.color_pair(C_NORMAL) | curses.A_BOLD) if state.notify_err else attr_n
        safe_addstr(stdscr, h - 1, len(mode_str), state.notify[: w - len(mode_str) - 1], attr)

# ─── 입력 처리 ────────────────────────────────────────────────────────────────

def handle_insert(ch, state):
    lines = state.lines
    r, c  = state.cur_row, state.cur_col

    if ch in (curses.KEY_BACKSPACE, 127, 8):
        if c > 0:
            lines[r]      = lines[r][:c-1] + lines[r][c:]
            state.cur_col -= 1
        elif r > 0:
            prev_len       = len(lines[r-1])
            lines[r-1]    += lines[r]
            lines.pop(r)
            state.cur_row -= 1
            state.cur_col  = prev_len

    elif ch == curses.KEY_DC:
        if c < len(lines[r]):
            lines[r] = lines[r][:c] + lines[r][c+1:]
        elif r < len(lines) - 1:
            lines[r] += lines[r+1]
            lines.pop(r+1)

    elif ch == 10:  # Enter
        new_line       = lines[r][c:]
        lines[r]       = lines[r][:c]
        lines.insert(r + 1, new_line)
        state.cur_row += 1
        state.cur_col  = 0

    elif ch == curses.KEY_UP:
        if r > 0:
            state.cur_row -= 1
            state.cur_col  = min(c, len(lines[state.cur_row]))

    elif ch == curses.KEY_DOWN:
        if r < len(lines) - 1:
            state.cur_row += 1
            state.cur_col  = min(c, len(lines[state.cur_row]))

    elif ch == curses.KEY_LEFT:
        if c > 0:
            state.cur_col -= 1
        elif r > 0:
            state.cur_row -= 1
            state.cur_col  = len(lines[state.cur_row])

    elif ch == curses.KEY_RIGHT:
        if c < len(lines[r]):
            state.cur_col += 1
        elif r < len(lines) - 1:
            state.cur_row += 1
            state.cur_col  = 0

    elif ch == curses.KEY_HOME:
        state.cur_col = 0

    elif ch == curses.KEY_END:
        state.cur_col = len(lines[r])

    elif 32 <= ch < 127:
        lines[r]      = lines[r][:c] + chr(ch) + lines[r][c:]
        state.cur_col += 1


def handle_command_input(ch, state):
    if ch in (curses.KEY_BACKSPACE, 127, 8):
        if len(state.cmd) > 1:
            state.cmd = state.cmd[:-1]
    elif ch == 10:
        result     = state.exec_cmd(state.cmd)
        state.mode = "INSERT"
        state.cmd  = ":"
        return result
    elif 32 <= ch < 127:
        state.cmd += chr(ch)
    return None

# ─── 메인 루프 ────────────────────────────────────────────────────────────────

def main(stdscr):
    curses.curs_set(1)
    curses.start_color()
    curses.use_default_colors()
    curses.init_pair(C_NORMAL, curses.COLOR_YELLOW, curses.COLOR_BLACK)
    curses.init_pair(C_TITLE,  curses.COLOR_BLACK,  curses.COLOR_YELLOW)

    stdscr.keypad(True)
    stdscr.bkgd(' ', curses.color_pair(C_NORMAL))

    state         = State()   # 시리얼 없이 바로 시작
    scroll_offset = 0

    while True:
        h, w = stdscr.getmaxyx()
        stdscr.erase()

        left_w   = int(w * 0.60)
        right_w  = w - left_w
        main_h   = h - 1
        status_h = max(12, main_h // 3)
        guide_h  = main_h - status_h

        try:
            editor_win = curses.newwin(main_h,   left_w,  0,        0)
            status_win = curses.newwin(status_h, right_w, 0,        left_w)
            guide_win  = curses.newwin(guide_h,  right_w, status_h, left_w)
        except curses.error:
            stdscr.refresh()
            stdscr.getch()
            continue

        inner_h = main_h - 2
        if state.cur_row < scroll_offset:
            scroll_offset = state.cur_row
        elif state.cur_row >= scroll_offset + inner_h:
            scroll_offset = state.cur_row - inner_h + 1

        draw_editor(editor_win, state, scroll_offset)
        draw_status(status_win, state)
        draw_guide(guide_win)
        draw_statusbar(stdscr, state)
        stdscr.refresh()

        curses.curs_set(1 if state.mode == "INSERT" else 0)

        ch = stdscr.getch()

        if state.mode == "INSERT":
            if ch == 27:  # ESC
                state.mode   = "COMMAND"
                state.notify = ""
            else:
                handle_insert(ch, state)

        elif state.mode == "COMMAND":
            if ch == ord('i') and state.cmd == ":":
                state.mode = "INSERT"
                state.cmd  = ":"
            else:
                result = handle_command_input(ch, state)
                if result == "QUIT":
                    break


def run():
    print(f"Auto-detected port: {SERIAL_PORT}")
    print("Starting... (serial is optional, connect later with :connect)")
    try:
        curses.wrapper(main)
    finally:
        pass

if __name__ == "__main__":
    run()
