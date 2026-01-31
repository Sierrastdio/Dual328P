import serial
import time
from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Static, Input, TextArea
from textual.containers import Horizontal, Vertical
from textual.binding import Binding
from textual.events import Key
####################################윈도우 환경에선  COM3 같은 포트형식으로 변경 필요.
SERIAL_PORT = "/dev/ttyUSB0" 
BAUD_RATE = 115200

class IBM5100App(App):
    CSS = """
    Screen { background: #080C14; }
    #main_layout { height: 1fr; }
    #left_panel { width: 60%; border: solid #ffe900; margin: 1; }
    #right_panel { width: 40%; }
    #status_container, #guide_container { border: solid #ffe900; margin: 1; }
    #status_container { height: 40%; }
    #guide_container { height: 60%; border: dashed #ffe900; }
    .panel_title { background: #ffe900; color: #080C14; width: 100%; text-align: center; text-style: bold; }
    TextArea { background: #080C14; color: #ffe900; border: none; }
    TextArea > .text-area--cursor { background: #ffe900; color: #080C14; }
    .stat_text, .guide_text { color: #ffe900; padding: 1; }
    #cmd_input { background: #080C14; color: #ffe900; border: tall #ffe900; display: none; }
    Header { background: #ffe900; color: #080C14; text-style: bold; }
    Footer { background: #080C14; color: #ffe900; }
    """

    BINDINGS = [
        Binding("escape", "command_mode", "ESC: CMD"),
        Binding("i", "insert_mode", "i: INSERT"), 
    ]

    def compose(self) -> ComposeResult:
        yield Header()
        yield Horizontal(
            Vertical(
                Static(" EDITOR (SMU JIT ASM v2.1) ", classes="panel_title"),
                TextArea("0000: ", id="code_editor"),
                id="left_panel"
            ),
            Vertical(
                Vertical(
                    Static(" PAGING MONITOR ", classes="panel_title"),
                    Static(self.update_status(0, 0), id="status_area", classes="stat_text"),
                    id="status_container"
                ),
                Vertical(
                    Static(" COMMAND & ARCH ", classes="panel_title"),
                    Static(
                        " [COMMANDS]\n"
                        " :w <B> <P> - Load (B:0-1, P:0-127)\n"
                        " :run / :r (reset) / :clear / :q\n\n"
                        " [ISA ADDITIONS]\n"
                        " SETPAGE <0-15> - Switch Page\n\n"
                        " [MEM MAP]\n"
                        " Bank 0: Core 1 (0x0000-0x3FFF)\n"
                        " Bank 1: Core 2 (0x4000-0x7FFF)\n"
                        " 1 Page = 128 Bytes (Total 128 Pages)", 
                        classes="guide_text"
                    ),
                    id="guide_container"
                ),
                id="right_panel"
            ),
            id="main_layout"
        )
        yield Input(placeholder=":", id="cmd_input")
        yield Footer()

    def update_status(self, bank, page):
        base = "0x0000" if str(bank) == "0" else "0x4000"
        return (
            f" [HARDWARE STATE]\n"
            f"  TARGET BANK : {bank} (Core {int(bank)+1})\n"
            f"  BASE ADDR   : {base}\n"
            f"  ACTIVE PAGE : {page} (of 127)\n"
            f"  PAGE ADDR   : 0x{int(page)*128:04X}\n"
            f" --------------------------\n"
            f"  STATUS      : STANDBY"
        )

    def on_mount(self) -> None:
        self.editor = self.query_one("#code_editor")
        self.cmd_input = self.query_one("#cmd_input")
        self.editor.focus()
        try:
            self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
        except:
            self.ser = None

    def on_key(self, event: Key) -> None:
        if event.key == "i" and self.cmd_input.has_focus:
            self.action_insert_mode()
            event.prevent_default()

    def action_insert_mode(self) -> None:
        self.cmd_input.display = False
        self.editor.focus()

    # 줄 표시 문자(0000: ) 강제 고정 로직 강화
    def on_text_area_changed(self, event: TextArea.Changed) -> None:
        editor = event.text_area
        lines = editor.text.split("\n")
        new_lines = []
        needs_update = False

        for i, line in enumerate(lines):
            prefix = f"{i:04d}: "
            if not line.startswith(prefix):
                # 접두사가 없거나 망가진 경우 복구
                content = line[6:] if len(line) > 6 else ""
                new_lines.append(prefix + content)
                needs_update = True
            else:
                new_lines.append(line)

        if needs_update:
            current_cursor = editor.cursor_location
            editor.text = "\n".join(new_lines)
            editor.move_cursor(current_cursor)

    def on_text_area_selection_changed(self, event: TextArea.SelectionChanged) -> None:
        row, col = event.text_area.cursor_location
        if col < 6: # 커서가 prefix 영역으로 들어가지 못하게 방어
            event.text_area.move_cursor((row, 6))

    def action_command_mode(self) -> None:
        self.cmd_input.display = True
        self.cmd_input.value = ":"
        self.cmd_input.focus()

    def on_input_submitted(self, event: Input.Submitted) -> None:
        input_text = event.value.strip()
        args = input_text.split()
        if not args: return
        
        cmd = args[0]

        if cmd == ":w":
            bank = args[1] if len(args) > 1 else "0"
            page = args[2] if len(args) > 2 else "0"
            self.upload_paging(bank, page)
            self.query_one("#status_area").update(self.update_status(bank, page))
        elif cmd == ":run": 
            if self.ser: self.ser.write(b"RUN\n")
            self.notify("RUN COMMAND SENT")
        elif cmd == ":r": 
            if self.ser: self.ser.write(b"RESET\n")
            self.notify("RESET SENT")
        elif cmd == ":clear": 
            self.editor.text = "0000: "
        elif cmd == ":q": 
            self.exit()
        
        event.input.value = ":"

    def upload_paging(self, bank, page):
        if not self.ser: 
            self.notify("SERIAL ERROR", severity="error")
            return
        
        # 순차적 명령어 전송 (아두이노가 처리할 시간을 주기 위해 sleep 추가)
        try:
            self.ser.write(f"PAGE {page}\n".encode())
            time.sleep(0.1)
            self.ser.write(b"CLEAR\n")
            time.sleep(0.1)

            for line in self.editor.text.split("\n"):
                inst = line[6:].strip()
                if inst:
                    self.ser.write(f"ASM 1 {inst}\n".encode())
                    time.sleep(0.05) # 각 줄마다 충분한 전송 시간 확보

            self.ser.write(f"LOAD {bank}\n".encode())
            self.notify(f"BANK {bank} PAGE {page} LOADED")
        except Exception as e:
            self.notify(f"UPLOAD FAILED: {e}", severity="error")

if __name__ == "__main__":
    IBM5100App().run()