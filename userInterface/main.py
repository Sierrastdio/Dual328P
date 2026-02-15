import serial
import serial.tools.list_ports
import time
from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Static, Input, TextArea
from textual.containers import Horizontal, Vertical
from textual.binding import Binding
from textual.events import Key

def find_arduino_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if 'Arduino' in port.description or 'CH340' in port.description or 'USB' in port.description:
            return port.device
    if ports:
        return ports[0].device
    return "COM4" ############################ 포트 설정 확인하기#############################

SERIAL_PORT = find_arduino_port()
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

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.ser = None

    def compose(self) -> ComposeResult:
        yield Header()
        yield Horizontal(
            Vertical(
                Static(" EDITOR (SMU v2.1) ", classes="panel_title"),
                TextArea("0000: ", id="code_editor"),
                id="left_panel"
            ),
            Vertical(
                Vertical(
                    Static(" SYSTEM STATUS ", classes="panel_title"),
                    Static(self.update_status(0, 0, "DISCONNECTED"), id="status_area", classes="stat_text"),
                    id="status_container"
                ),
                Vertical(
                    Static(" QUICK GUIDE ", classes="panel_title"),
                    Static(
                        " [COMMANDS]\n"
                        " :w <B> <P> - Write to ROM\n"
                        "               B: Bank (0-1)\n"
                        "               P: Page (0-127)\n"
                        " :run        - Start cores\n"
                        " :r          - Reset cores\n"
                        " :clear      - Clear editor\n"
                        " :q          - Quit\n\n"
                        " [INSTRUCTIONS]\n"
                        " LOAD/ADD/SUB/MUL <0-15>\n"
                        " AND/OR <0-15>\n"
                        " OUT, HALT, NOP\n"
                        " SETPAGE <0-15>\n\n"
                        " [EXAMPLE]\n"
                        " LOAD 10\n"
                        " ADD 5\n"
                        " MUL 2\n"
                        " OUT\n"
                        " HALT", 
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

    def update_status(self, bank, page, status="STANDBY"):
        base = "0x0000" if str(bank) == "0" else "0x4000"
        serial_status = "CONNECTED" if self.ser and self.ser.is_open else "DISCONNECTED"
        return (
            f" [HARDWARE]\n"
            f"  Port   : {SERIAL_PORT}\n"
            f"  Serial : {serial_status}\n"
            f"  Status : {status}\n\n"
            f" [TARGET]\n"
            f"  Bank   : {bank} (Core {int(bank)+1})\n"
            f"  Base   : {base}\n"
            f"  Page   : {page}/127\n"
            f"  Offset : 0x{int(page)*128:04X}"
        )

    def connect_serial(self):
        try:
            if self.ser and self.ser.is_open:
                self.ser.close()
            
            self.ser = serial.Serial(
                port=SERIAL_PORT,
                baudrate=BAUD_RATE,
                timeout=1,
                write_timeout=1
            )
            time.sleep(2)
            self.notify(f"Connected: {SERIAL_PORT}")
            return True
        except Exception as e:
            self.notify(f"Connection failed: {str(e)}", severity="error")
            self.ser = None
            return False

    def on_mount(self) -> None:
        self.editor = self.query_one("#code_editor")
        self.cmd_input = self.query_one("#cmd_input")
        self.editor.focus()
        
        if self.connect_serial():
            self.query_one("#status_area").update(
                self.update_status(0, 0, "READY")
            )

    def on_unmount(self) -> None:
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except:
                pass

    def on_key(self, event: Key) -> None:
        if event.key == "i" and self.cmd_input.has_focus:
            self.action_insert_mode()
            event.prevent_default()

    def action_insert_mode(self) -> None:
        self.cmd_input.display = False
        self.editor.focus()

    def on_text_area_changed(self, event: TextArea.Changed) -> None:
        editor = event.text_area
        lines = editor.text.split("\n")
        new_lines = []
        needs_update = False

        for i, line in enumerate(lines):
            prefix = f"{i:04d}: "
            if not line.startswith(prefix):
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
        if col < 6:
            event.text_area.move_cursor((row, 6))

    def action_command_mode(self) -> None:
        self.cmd_input.display = True
        self.cmd_input.value = ":"
        self.cmd_input.focus()

    def on_input_submitted(self, event: Input.Submitted) -> None:
        input_text = event.value.strip()
        args = input_text.split()
        if not args: 
            event.input.value = ":"
            return
        
        cmd = args[0]

        if cmd == ":w":
            bank = args[1] if len(args) > 1 else "0"
            page = args[2] if len(args) > 2 else "0"
            
            if self.upload(bank, page):
                self.query_one("#status_area").update(
                    self.update_status(bank, page, "LOADED")
                )
                
        elif cmd == ":run": 
            if self.send_command(":run"):
                self.query_one("#status_area").update(
                    self.update_status(0, 0, "RUNNING")
                )
                
        elif cmd == ":r": 
            if self.send_command(":r"):
                self.query_one("#status_area").update(
                    self.update_status(0, 0, "HALTED")
                )
                    
        elif cmd == ":clear": 
            self.editor.text = "0000: "
            self.send_command(":clear")
            self.notify("Editor cleared")
            
        elif cmd == ":q": 
            self.exit()
        
        event.input.value = ":"

    def send_command(self, cmd):
        """단순 명령 전송"""
        if not self.ser or not self.ser.is_open:
            self.notify("Not connected!", severity="error")
            return False
        
        try:
            self.ser.write(f"{cmd}\n".encode())
            time.sleep(0.1)
            response = self.ser.read_all().decode('utf-8', errors='ignore')
            return "OK" in response
        except Exception as e:
            self.notify(f"Failed: {e}", severity="error")
            return False

    def upload(self, bank, page):
        """코드 업로드"""
        if not self.ser or not self.ser.is_open:
            self.notify("Not connected!", severity="error")
            return False
        
        try:
            # 버퍼 클리어
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            
            # 이전 버퍼 클리어
            self.ser.write(b":clear\n")
            time.sleep(0.1)

            # 코드 라인별 전송 (간단!)
            line_count = 0
            for line in self.editor.text.split("\n"):
                inst = line[6:].strip()
                if inst:
                    self.ser.write(f"{inst}\n".encode())
                    time.sleep(0.03)
                    line_count += 1

            # 쓰기 명령
            self.ser.write(f":w {bank} {page}\n".encode())
            time.sleep(0.5)
            
            response = self.ser.read_all().decode('utf-8', errors='ignore')
            
            if "OK" in response:
                self.notify(f"Uploaded {line_count} lines → Bank {bank}, Page {page}")
                return True
            else:
                self.notify("Upload failed", severity="error")
                return False
                
        except Exception as e:
            self.notify(f"Error: {str(e)}", severity="error")
            return False

if __name__ == "__main__":
    print(f"Auto-detected port: {SERIAL_PORT}")
    IBM5100App().run()

'''
**예제 프로그램 1 - 간단한 연산:**
```
0000: LOAD 10
0001: ADD 5
0002: MUL 2
0003: OUT
0004: HALT
```
결과: (10 + 5) × 2 = 30

**예제 프로그램 2 - 페이징 사용:**
```
0000: LOAD 5
0001: SETPAGE 1
0002: OUT
0003: HALT
```

**예제 프로그램 3 - 비트 연산:**
```
0000: LOAD 15
0001: AND 12
0002: OR 1
0003: OUT
0004: HALT
```
결과: (15 & 12) | 1 = 13

**통신 프로토콜 (단순화):**
```
Python → Arduino:
LOAD 10          (어셈블리 라인 직접 전송)
ADD 5
OUT
HALT
:w 0 0           (Bank 0, Page 0에 쓰기)
:run             (실행)
:r               (리셋)
:clear           (클리어)

Arduino → Python:
OK               (모든 명령에 OK만 응답)
*/
'''