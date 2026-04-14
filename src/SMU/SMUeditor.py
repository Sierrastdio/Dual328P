'''
'   WARNING: THIS IS ONLY COMPATIBLE WITH 'SMU system' (SMUmega.ino)
'   BECAUSE THIS CODE SENDS THE 'CHARACTERS' DIRECTLY INPUT BY THE USER TO THE ARDUINO,
'   THEREFORE ONLY THE SMU SYSTEM, WHICH RECEIVES THE 'CHARACTERS' AND CONVERTS THEM 
'   DIRECTLY TO BINARY, IS COMPATIBLE.
'''


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
MAX_LINES_PER_PAGE = 128

class LightProgrammer:
    def __init__(self, stdscr):
        # ESC 반응 속도 최적화 (단위: 밀리초)
        curses.set_escdelay(25)

        self.stdscr = stdscr

        self.pages = [[""]]
        self.current_page = 0
        self.dirty = set()

        self.cursor_y = 0
        self.cursor_x = 0
        self.scroll_y = 0

        self.mode = "INSERT"
        self.status = "OFFLINE"
        self.bank = 0
        self.ser = None
        self.cmd_buffer = ""
        self.upload_log = []  # [(page_idx, "OK"/"FAIL"/"SKIP", line_count), ...]

        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_YELLOW, -1)
        curses.init_pair(2, curses.COLOR_RED,    -1)
        curses.init_pair(3, curses.COLOR_GREEN,  -1)
        curses.init_pair(4, curses.COLOR_CYAN,   -1)
        self.stdscr.keypad(True)
        self.stdscr.nodelay(False)

        self.run()

    # ── 프로퍼티 ──────────────────────────────────────────────────────────
    @property
    def lines(self):
        return self.pages[self.current_page]

    @lines.setter
    def lines(self, val):
        self.pages[self.current_page] = val

    # ── 유틸 ──────────────────────────────────────────────────────────────
    def _editor_rows(self, h):
        return max(1, h - 4)  # 상단1 + 탭1 + 여백1 + 하단1

    def _clamp_scroll(self, h):
        rows = self._editor_rows(h)
        if self.cursor_y < self.scroll_y:
            self.scroll_y = self.cursor_y
        elif self.cursor_y >= self.scroll_y + rows:
            self.scroll_y = self.cursor_y - rows + 1

    def _mark_dirty(self):
        self.dirty.add(self.current_page)

    def _switch_page(self, idx):
        idx = max(0, min(idx, len(self.pages) - 1))
        self.current_page = idx
        self.cursor_y = 0
        self.cursor_x = 0
        self.scroll_y = 0

    # ── 우측 패널에 한 줄 안전하게 출력 ──────────────────────────────────
    def _rprint(self, row, sx, max_x, text, attr=0):
        """경계선(editor_width) 안쪽까지만 출력, wide char 없이 ASCII only"""
        if row < 0:
            return
        avail = max_x - sx - 1
        if avail <= 0:
            return
        text = text[:avail]
        try:
            self.stdscr.addstr(row, sx, text, attr)
        except curses.error:
            pass

    # ── 탭 바 ─────────────────────────────────────────────────────────────
    def _draw_tab_bar(self, editor_width):
        x = 0
        for i in range(len(self.pages)):
            dirty_mark = "*" if i in self.dirty else " "
            lc = len([l for l in self.pages[i] if l.strip()])
            label = f"[P{i:02d}{dirty_mark}{lc:3d}]"
            if x + len(label) >= editor_width:
                try:
                    self.stdscr.addstr(1, x, "...", curses.A_DIM)
                except curses.error:
                    pass
                break
            attr = (curses.color_pair(4) | curses.A_BOLD | curses.A_REVERSE) \
                   if i == self.current_page else curses.A_DIM
            try:
                self.stdscr.addstr(1, x, label, attr)
            except curses.error:
                pass
            x += len(label)

    # ── GUI ───────────────────────────────────────────────────────────────
    def draw_gui(self):
        self.stdscr.erase()
        h, w = self.stdscr.getmaxyx()
        ew = int(w * 0.62)          # editor_width: 경계선 열
        sx = ew + 2                  # 우측 패널 텍스트 시작 열
        rows = self._editor_rows(h)
        EDITOR_TOP = 3

        self._clamp_scroll(h)

        # ── 상단 바 ───────────────────────────────────────────────────────
        title = f" RAM PROG | {SERIAL_PORT} | B{self.bank} | {self.mode} "
        try:
            self.stdscr.attron(curses.A_REVERSE)
            self.stdscr.addstr(0, 0, title.ljust(w)[:w - 1])
            self.stdscr.attroff(curses.A_REVERSE)
        except curses.error:
            pass

        # ── 탭 바 ─────────────────────────────────────────────────────────
        self._draw_tab_bar(ew)

        # ── 경계선 (전체 세로) ────────────────────────────────────────────
        if ew < w:
            for i in range(1, h - 1):
                try:
                    self.stdscr.addstr(i, ew, "|")
                except curses.error:
                    pass

        # ── 에디터 영역 ───────────────────────────────────────────────────
        line_count = len(self.lines)
        for screen_row in range(rows):
            line_idx = self.scroll_y + screen_row
            if line_idx >= line_count:
                break
            line = self.lines[line_idx]
            prefix = f"{line_idx:03d}: "
            # 텍스트가 경계선을 넘지 않도록 자름
            display_line = line[:max(0, ew - 8)]

            if line_count > MAX_LINES_PER_PAGE:
                la = curses.color_pair(2)
            elif line_count >= MAX_LINES_PER_PAGE - 10:
                la = curses.color_pair(1)
            else:
                la = 0

            try:
                self.stdscr.addstr(EDITOR_TOP + screen_row, 1, prefix, curses.A_DIM | la)
                self.stdscr.addstr(EDITOR_TOP + screen_row, 6, display_line, la)
            except curses.error:
                pass

        # ── 우측 패널 ─────────────────────────────────────────────────────
        if w > ew + 8:
            lc = len([l for l in self.lines if l.strip()])
            lc_attr = curses.color_pair(2) if lc > MAX_LINES_PER_PAGE else \
                      curses.color_pair(1) if lc >= MAX_LINES_PER_PAGE - 10 else \
                      curses.color_pair(3)
            dirty_attr = curses.color_pair(1) if self.dirty else curses.color_pair(3)

            # (row, text, attr) — ASCII only, 경계선 안 침범
            info = [
                (2,  "[SYSTEM]",                           curses.A_BOLD),
                (3,  f"STAT: {self.status}",               0),
                (4,  f"BANK: {self.bank}",                 0),
                (5,  f"PAGE: {self.current_page}/{len(self.pages)-1}", 0),
                (6,  f"LINES:{lc:4d}/128",                lc_attr),
                (7,  f"DIRTY: {len(self.dirty)}pg",       dirty_attr),

                (9,  "[SERIAL]",                           curses.A_BOLD),
                (10, ":connect [port]",                    0),
                (11, ":disconnect / :ports",               0),

                (13, "[PAGE TAB]",                         curses.A_BOLD),
                (14, ":np        new page",                0),
                (15, ":dp        del cur page",            0),
                (16, ":gp <n>    goto page n",             0),
                (17, ":lp [n]    SETPAGE snippet",         0),

                (19, "[UPLOAD]",                           curses.A_BOLD),
                (20, ":w  <bank>   all pages in order",    0),
                (21, ":wp <bank>   cur page only",         0),
                (22, ":run  :rst  :clear  :q",             0),

                (24, "[KEYS]",                             curses.A_BOLD),
                (25, "Tab / S-Tab  next/prev page",        0),
                (26, "ESC=CMD   PgUp/PgDn scroll",         0),
            ]

            for row, text, attr in info:
                if row >= h - 1:
                    continue
                self._rprint(row, sx, w, text, attr)

            # ── 업로드 로그 ───────────────────────────────────────────────
            log_start = 28
            if log_start < h - 1:
                self._rprint(log_start, sx, w, "[UPLOAD LOG]", curses.A_BOLD)
                visible = self.upload_log[-(h - log_start - 2):]
                for j, (pidx, result, cnt) in enumerate(visible):
                    row = log_start + 1 + j
                    if row >= h - 1:
                        break
                    if result == "OK":
                        color, mark = curses.color_pair(3), "OK"
                    elif result == "FAIL":
                        color, mark = curses.color_pair(2), "!!"
                    else:
                        color, mark = curses.A_DIM, "--"
                    self._rprint(row, sx, w, f"P{pidx:02d} [{mark}] {cnt:3d}L", color)

        # ── 하단 바 ───────────────────────────────────────────────────────
        try:
            if self.mode == "COMMAND":
                self.stdscr.addstr(h - 1, 0,
                    f":{self.cmd_buffer}_".ljust(w - 1), curses.A_BOLD)
            else:
                st = f" [{self.mode}] Ln{self.cursor_y+1}/{len(self.lines)}" \
                     f"  Tab=NextPage  ESC=CMD"
                self.stdscr.addstr(h - 1, 0, st[:w - 1])
        except curses.error:
            pass

        # ── 커서 ──────────────────────────────────────────────────────────
        try:
            if self.mode == "INSERT":
                sr = self.cursor_y - self.scroll_y
                self.stdscr.move(EDITOR_TOP + sr, self.cursor_x + 6)
            else:
                self.stdscr.move(h - 1, min(len(self.cmd_buffer) + 1, w - 1))
        except curses.error:
            pass

        self.stdscr.refresh()

    # ── SETPAGE boilerplate ────────────────────────────────────────────────
    def _make_setpage_snippet(self, target_page):
        lines = [f"; --> jump to page {target_page}"]
        if target_page <= 15:
            lines.append(f"LOAD {target_page}")
        else:
            a = min(target_page // 10, 15)
            b = 10
            c = target_page - a * b
            if 0 <= c <= 15:
                lines.append(f"LOAD {a}")
                lines.append(f"MUL {b}")
                if c > 0:
                    lines.append(f"ADD {c}")
            else:
                lines.append(f"LOAD {min(target_page, 15)}")
        lines.append("SLOT 15")
        lines.append("SETPAGE")
        return lines

    def _insert_setpage(self, src_idx, target_idx):
        if src_idx >= len(self.pages):
            return
        pg = self.pages[src_idx]
        while pg and pg[-1].strip() == "":
            pg.pop()
        pg.extend(self._make_setpage_snippet(target_idx))
        if not pg:
            pg.append("")
        self.dirty.add(src_idx)

    # ── 커맨드 핸들러 ─────────────────────────────────────────────────────
    def handle_command(self):
        raw = self.cmd_buffer.strip()
        self.cmd_buffer = ""
        if not raw:
            return True

        parts = raw.split()
        cmd = parts[0].lower()

        try:
            if cmd == "connect":
                global SERIAL_PORT
                SERIAL_PORT = parts[1] if len(parts) > 1 else find_arduino_port()
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
                    self.status = f"ERR:{str(e)[:14]}"

            elif cmd == "disconnect":
                if self.ser and self.ser.is_open:
                    self.ser.close()
                self.ser = None
                self.status = "OFFLINE"

            elif cmd == "ports":
                pts = serial.tools.list_ports.comports()
                self.status = " ".join(p.device for p in pts)[:22] if pts else "NO PORTS"

            elif cmd == "np":
                new_idx = self.current_page + 1
                self.pages.insert(new_idx, [""])
                self.dirty = {(i if i < new_idx else i + 1) for i in self.dirty}
                self._switch_page(new_idx)
                self.status = f"NEW P{new_idx}"

            elif cmd == "dp":
                if len(self.pages) == 1:
                    self.status = "LAST PAGE"
                else:
                    di = self.current_page
                    self.pages.pop(di)
                    self.dirty.discard(di)
                    self.dirty = {(i if i < di else i - 1) for i in self.dirty}
                    self._switch_page(min(di, len(self.pages) - 1))
                    self.status = f"DEL P{di}"

            elif cmd == "gp":
                if len(parts) < 2:
                    self.status = "USAGE: gp <n>"
                else:
                    n = int(parts[1])
                    if 0 <= n < len(self.pages):
                        self._switch_page(n)
                        self.status = f"GOTO P{n}"
                    else:
                        self.status = f"NO P{n} (0~{len(self.pages)-1})"

            elif cmd == "lp":
                target = int(parts[1]) if len(parts) > 1 else self.current_page + 1
                self._insert_setpage(self.current_page, target)
                self.status = f"SETP->P{target} INSERTED"

            elif cmd == "w":
                if len(parts) < 2:
                    self.status = "USAGE: w <bank>"
                else:
                    self.bank = int(parts[1])
                    self.upload_all(self.bank)

            elif cmd == "wp":
                if len(parts) < 2:
                    self.status = "USAGE: wp <bank>"
                else:
                    self.bank = int(parts[1])
                    self.upload_single(self.bank, self.current_page, self.current_page)

            elif cmd == "run":
                self.send_raw(":run")
                self.status = "RUNNING"

            elif cmd == "rst":
                self.send_raw(":rst")
                self.status = "HALTED"

            elif cmd == "clear":
                self.pages = [[""]]
                self.current_page = 0
                self.dirty.clear()
                self.upload_log.clear()
                self.cursor_y = self.cursor_x = self.scroll_y = 0
                self.send_raw(":clear")
                self.status = "CLEARED"

            elif cmd == "q":
                return False

            else:
                self.status = f"?: {cmd}"

        except Exception as e:
            self.status = f"ERR:{str(e)[:18]}"
        return True

    # ── 시리얼 ────────────────────────────────────────────────────────────
    def send_raw(self, msg):
        if self.ser and self.ser.is_open:
            self.ser.write(f"{msg}\n".encode())
            return True
        self.status = "NOT CONNECTED"
        return False

    # ── 단일 페이지 업로드 ────────────────────────────────────────────────
    def upload_single(self, bank, ram_page, editor_page_idx, quiet=False):
        if not self.ser or not self.ser.is_open:
            self.status = "NOT CONNECTED"
            return False

        pg = self.pages[editor_page_idx]
        effective = [l for l in pg if l.strip() and not l.strip().startswith(";")]

        if not effective:
            self.upload_log.append((editor_page_idx, "SKIP", 0))
            if not quiet:
                self.status = f"P{editor_page_idx} EMPTY, SKIP"
            return True

        if not quiet:
            self.status = f"UPLOAD P{editor_page_idx}->RAM P{ram_page}..."
            self.draw_gui()

        self.send_raw(":clear")
        time.sleep(0.08)

        for line in effective:
            self.send_raw(line.strip())
            time.sleep(0.015)

        self.ser.write(f":w {bank} {ram_page}\n".encode())
        time.sleep(0.5)

        resp = self.ser.read_all().decode('utf-8', errors='ignore')
        if "OK" in resp:
            self.dirty.discard(editor_page_idx)
            self.upload_log.append((editor_page_idx, "OK", len(effective)))
            if not quiet:
                self.status = f"OK P{editor_page_idx}->B{bank}P{ram_page} ({len(effective)}L)"
            return True
        else:
            self.upload_log.append((editor_page_idx, "FAIL", len(effective)))
            if not quiet:
                self.status = f"NO OK P{editor_page_idx} [{resp[:12]}]"
            return False

    # ── 전체 페이지 일괄 업로드 ───────────────────────────────────────────
    def upload_all(self, bank):
        if not self.ser or not self.ser.is_open:
            self.status = "NOT CONNECTED"
            return

        total = len(self.pages)
        failed = []
        self.upload_log = []
        for i in range(total):
            self.status = f"UPLOAD {i+1}/{total}  P{i}..."
            self.draw_gui()
            ok = self.upload_single(bank, i, i, quiet=True)
            if not ok:
                failed.append(i)
                time.sleep(0.3)

        if failed:
            self.status = f"DONE w/ FAIL: P{failed} B{bank}"
        else:
            self.status = f"ALL OK {total}pg B{bank} -- :run to start"

    # ── 메인 루프 ─────────────────────────────────────────────────────────
    def run(self):
        while True:
            self.draw_gui()
            ch = self.stdscr.getch()

            if self.mode == "INSERT":
                if ch == 27:
                    self.mode = "COMMAND"
                    self.cmd_buffer = ""

                elif ch == 9:  # Tab → 다음 페이지
                    self._switch_page((self.current_page + 1) % len(self.pages))

                elif ch == curses.KEY_BTAB:  # Shift+Tab → 이전 페이지
                    self._switch_page((self.current_page - 1) % len(self.pages))

                elif ch in (10, 13, curses.KEY_ENTER):
                    if len(self.lines) >= MAX_LINES_PER_PAGE:
                        self.status = "PAGE FULL! (128 lines max)"
                    else:
                        line = self.lines[self.cursor_y]
                        self.lines[self.cursor_y] = line[:self.cursor_x]
                        self.lines.insert(self.cursor_y + 1, line[self.cursor_x:])
                        self.cursor_y += 1
                        self.cursor_x = 0
                        self._mark_dirty()

                elif ch in (curses.KEY_BACKSPACE, 127, 8):
                    if self.cursor_x > 0:
                        line = self.lines[self.cursor_y]
                        self.lines[self.cursor_y] = line[:self.cursor_x - 1] + line[self.cursor_x:]
                        self.cursor_x -= 1
                        self._mark_dirty()
                    elif self.cursor_y > 0:
                        prev_len = len(self.lines[self.cursor_y - 1])
                        self.lines[self.cursor_y - 1] += self.lines.pop(self.cursor_y)
                        self.cursor_y -= 1
                        self.cursor_x = prev_len
                        self._mark_dirty()

                elif ch == curses.KEY_DC:
                    line = self.lines[self.cursor_y]
                    if self.cursor_x < len(line):
                        self.lines[self.cursor_y] = line[:self.cursor_x] + line[self.cursor_x + 1:]
                        self._mark_dirty()
                    elif self.cursor_y < len(self.lines) - 1:
                        self.lines[self.cursor_y] += self.lines.pop(self.cursor_y + 1)
                        self._mark_dirty()

                elif ch == curses.KEY_HOME:
                    self.cursor_x = 0

                elif ch == curses.KEY_END:
                    self.cursor_x = len(self.lines[self.cursor_y])

                elif ch == curses.KEY_LEFT:
                    if self.cursor_x > 0:
                        self.cursor_x -= 1
                    elif self.cursor_y > 0:
                        self.cursor_y -= 1
                        self.cursor_x = len(self.lines[self.cursor_y])

                elif ch == curses.KEY_RIGHT:
                    line = self.lines[self.cursor_y]
                    if self.cursor_x < len(line):
                        self.cursor_x += 1
                    elif self.cursor_y < len(self.lines) - 1:
                        self.cursor_y += 1
                        self.cursor_x = 0

                elif ch == curses.KEY_UP:
                    if self.cursor_y > 0:
                        self.cursor_y -= 1
                        self.cursor_x = min(self.cursor_x, len(self.lines[self.cursor_y]))

                elif ch == curses.KEY_DOWN:
                    if self.cursor_y < len(self.lines) - 1:
                        self.cursor_y += 1
                        self.cursor_x = min(self.cursor_x, len(self.lines[self.cursor_y]))

                elif ch == curses.KEY_PPAGE:
                    h, _ = self.stdscr.getmaxyx()
                    self.cursor_y = max(0, self.cursor_y - self._editor_rows(h))
                    self.cursor_x = min(self.cursor_x, len(self.lines[self.cursor_y]))

                elif ch == curses.KEY_NPAGE:
                    h, _ = self.stdscr.getmaxyx()
                    self.cursor_y = min(len(self.lines) - 1, self.cursor_y + self._editor_rows(h))
                    self.cursor_x = min(self.cursor_x, len(self.lines[self.cursor_y]))

                elif 32 <= ch <= 126:
                    line = self.lines[self.cursor_y]
                    self.lines[self.cursor_y] = line[:self.cursor_x] + chr(ch) + line[self.cursor_x:]
                    self.cursor_x += 1
                    self._mark_dirty()

            elif self.mode == "COMMAND":
                if ch in (10, 13, curses.KEY_ENTER):
                    if not self.handle_command():
                        break
                    self.mode = "INSERT"
                elif ch == 27:
                    self.cmd_buffer = ""
                    self.mode = "INSERT"
                elif ch in (curses.KEY_BACKSPACE, 127, 8):
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