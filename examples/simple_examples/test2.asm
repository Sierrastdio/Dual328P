; SETPAGE 시스템이 의도대로 동작하는지 알수 있다.

LOAD 10
SLOT 0

MUL 3
SLOT 1

FETCH 1
OUT

;-- 페이지 3으로 이동--
LOAD 3
SLOT 15
SETPAGE
;-----------------

FETCH 0
ADD 2
OUT