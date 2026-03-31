def generate_matrix_power():
    for page in range(128):
        print(f"; 페이지 {page}: A^{page+1}")
        
        if page == 0:
            # 초기화
            print("LOAD 1\nSLOT 0")
            print("LOAD 2\nSLOT 1")
            print("LOAD 3\nSLOT 2")
            print("LOAD 4\nSLOT 3")
            print("LOAD 2\nSLOT 4")
            print("LOAD 0\nSLOT 5")
            print("LOAD 0\nSLOT 6")
            print("LOAD 2\nSLOT 7")
        
        # 행렬 곱셈 (고정 코드)
        matrix_mult = """
FETCH 0
FETCH 4
MUL 0
PUSH
FETCH 1
FETCH 6
MUL 0
POP
ADD 0
SLOT 8

FETCH 0
FETCH 5
MUL 0
PUSH
FETCH 1
FETCH 7
MUL 0
POP
ADD 0
SLOT 9

FETCH 2
FETCH 4
MUL 0
PUSH
FETCH 3
FETCH 6
MUL 0
POP
ADD 0
SLOT 10

FETCH 2
FETCH 5
MUL 0
PUSH
FETCH 3
FETCH 7
MUL 0
POP
ADD 0
SLOT 11

FETCH 8
SLOT 0
FETCH 9
SLOT 1
FETCH 10
SLOT 2
FETCH 11
SLOT 3
"""
        print(matrix_mult)
        
        if page < 127:
            print(f"LOAD {page + 1}")
            print("SLOT 15")
            print("SETPAGE")
        else:
            print("FETCH 0\nOUT")
            print("FETCH 14\nOUT")
            print("HALT")
        
        print(f":w 0 {page}")
        print(":clear\n")