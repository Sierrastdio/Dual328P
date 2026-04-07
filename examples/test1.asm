; 16*2를 연산에 성공했는지,
; 32, 16을 차례대로 출력함으로서 FETCH와 OUT 기능이 제대로 작동했는지 알수 있다.

LOAD 16
SLOT 0

MUL 2
SLOT 1

FETCH 1
OUT
FETCH 0
OUT