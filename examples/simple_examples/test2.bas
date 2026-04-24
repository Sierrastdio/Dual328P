' SETPAGE 시스템이 의도대로 동작하는지 알수 있다.

LET 0 = 10
MUL 1 3
PRINT 1

'-- 페이지 3으로 이동--
PAGE 3

' 슬롯 0의 값(10)에 2를 더함
ADD 0 2
PRINT 0
HALT
