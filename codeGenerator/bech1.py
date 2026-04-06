def generate_polynomial():
    for page in range(128):
        coeff = page
        
        print(f"; 페이지 {page}: a_{page} = {coeff}")
        
        if page == 0:
            # 초기화
            print("LOAD 0")
            print("SLOT 0")
            print("LOAD 3")
            print("SLOT 1")
        else:
            # result = result * x + coeff
            print("FETCH 0")
            print("FETCH 1")
            print("MUL 0")
            
            # coeff 로드 (큰 수는 곱셈으로)
            if coeff <= 15:
                print(f"LOAD {coeff}")
            else:
                base = coeff % 16
                mult = coeff // 16
                print(f"LOAD {base}")
                if mult > 0:
                    print(f"MUL {min(mult, 15)}")
            
            print("ADD 0")
            print("SLOT 0")
        
        if page < 127:
            print(f"LOAD {page + 1}")
            print("SLOT 15")
            print("SETPAGE")
        else:
            print("FETCH 0")
            print("OUT")
            print("FETCH 14")
            print("OUT")
            print("HALT")
        
        print(f":w 0 {page}")
        print(":clear\n")

generate_polynomial()