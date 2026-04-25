' 16*2를 연산하는 예제
' 32(16*2), 16 순서로 출력
' 명령어(컴파일 후 ASM) 기준으로 P1/P2가 번갈아 실행됨

LET 0 = 16
MUL 0 2                ' SLOT[0] * 2 라는 뜻.

PRINT 0
PRINT 0
HALT                   ' 직접 작성해도 되고, compiler.py가 마지막에 HALT를 1개 자동 추가함
