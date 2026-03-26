#!/usr/bin/env python3
"""
벤치마크 프로그램 생성기
"""

import serial
import time

class BenchmarkGenerator:
    def __init__(self, ser):
        self.ser = ser
        self.buffer = []
        
    def clear(self):
        """버퍼 초기화"""
        self.buffer = []
        
    def add_inst(self, inst, operand=None):
        """명령어 추가"""
        if operand is not None:
            self.buffer.append(f"{inst} {operand}")
        else:
            self.buffer.append(inst)
    
    def repeat(self, count, pattern):
        """패턴 반복"""
        for _ in range(count):
            for inst in pattern:
                self.buffer.append(inst)
    
    def write_page(self, bank, page):
        """페이지 쓰기"""
        # :clear 전송
        self.ser.write(b":clear\n")
        time.sleep(0.01)
        
        # 명령어 전송
        for inst in self.buffer[:128]:  # 페이지당 128개
            self.ser.write(f"{inst}\n".encode())
            time.sleep(0.001)
        
        # :w 전송
        self.ser.write(f":w {bank} {page}\n".encode())
        time.sleep(0.1)
        
        # 응답 확인
        response = self.ser.read(100).decode()
        print(response)
        
        # 다음 페이지 준비
        self.buffer = self.buffer[128:]
    
    # ========================================
    # 템플릿 함수들
    # ========================================
    
    def generate_simple_loop(self, iterations):
        """단순 반복 연산"""
        self.clear()
        for i in range(iterations):
            self.add_inst("LOAD", 1)
            self.add_inst("ADD", 1)
            self.add_inst("MUL", 2)
    
    def generate_matrix_mult(self, count):
        """행렬 곱셈 반복"""
        self.clear()
        
        # 초기화
        self.add_inst("LOAD", 1)
        self.add_inst("SLOT", 0)  # A[0][0]
        self.add_inst("LOAD", 2)
        self.add_inst("SLOT", 1)  # A[0][1]
        self.add_inst("LOAD", 3)
        self.add_inst("SLOT", 2)  # A[1][0]
        self.add_inst("LOAD", 4)
        self.add_inst("SLOT", 3)  # A[1][1]
        
        self.add_inst("LOAD", 2)
        self.add_inst("SLOT", 4)  # B[0][0]
        self.add_inst("LOAD", 0)
        self.add_inst("SLOT", 5)  # B[0][1]
        self.add_inst("LOAD", 0)
        self.add_inst("SLOT", 6)  # B[1][0]
        self.add_inst("LOAD", 2)
        self.add_inst("SLOT", 7)  # B[1][1]
        
        # count번 곱셈 반복
        for _ in range(count):
            # C[0][0] = A[0][0]*B[0][0] + A[0][1]*B[1][0]
            self.add_inst("FETCH", 0)
            self.add_inst("FETCH", 4)
            self.add_inst("MUL", 0)
            self.add_inst("PUSH")
            self.add_inst("FETCH", 1)
            self.add_inst("FETCH", 6)
            self.add_inst("MUL", 0)
            self.add_inst("POP")
            self.add_inst("ADD", 0)
            self.add_inst("SLOT", 8)
            
            # C[0][1]
            self.add_inst("FETCH", 0)
            self.add_inst("FETCH", 5)
            self.add_inst("MUL", 0)
            self.add_inst("PUSH")
            self.add_inst("FETCH", 1)
            self.add_inst("FETCH", 7)
            self.add_inst("MUL", 0)
            self.add_inst("POP")
            self.add_inst("ADD", 0)
            self.add_inst("SLOT", 9)
            
            # C[1][0]
            self.add_inst("FETCH", 2)
            self.add_inst("FETCH", 4)
            self.add_inst("MUL", 0)
            self.add_inst("PUSH")
            self.add_inst("FETCH", 3)
            self.add_inst("FETCH", 6)
            self.add_inst("MUL", 0)
            self.add_inst("POP")
            self.add_inst("ADD", 0)
            self.add_inst("SLOT", 10)
            
            # C[1][1]
            self.add_inst("FETCH", 2)
            self.add_inst("FETCH", 5)
            self.add_inst("MUL", 0)
            self.add_inst("PUSH")
            self.add_inst("FETCH", 3)
            self.add_inst("FETCH", 7)
            self.add_inst("MUL", 0)
            self.add_inst("POP")
            self.add_inst("ADD", 0)
            self.add_inst("SLOT", 11)
            
            # C를 A로 복사
            self.add_inst("FETCH", 8)
            self.add_inst("SLOT", 0)
            self.add_inst("FETCH", 9)
            self.add_inst("SLOT", 1)
            self.add_inst("FETCH", 10)
            self.add_inst("SLOT", 2)
            self.add_inst("FETCH", 11)
            self.add_inst("SLOT", 3)
    
    def generate_hash_benchmark(self, iterations):
        """해시 함수 벤치마크"""
        self.clear()
        
        # 초기 해시값
        self.add_inst("LOAD", 0)
        self.add_inst("SLOT", 0)  # hash = 0
        
        for i in range(iterations):
            data_value = (i * 7 + 13) % 16  # 의사 랜덤 데이터
            
            # hash = hash * 13 + data
            self.add_inst("FETCH", 0)
            self.add_inst("MUL", 13)
            self.add_inst("LOAD", data_value)
            self.add_inst("ADD", 0)
            self.add_inst("SLOT", 0)
    
    def generate_full_benchmark(self, bank):
        """전체 벤치마크 (128페이지)"""
        total_pages = 128
        insts_per_page = 40  # 페이지당 약 40개 명령어
        
        for page in range(total_pages):
            print(f"생성 중: Bank {bank}, Page {page}/{total_pages}")
            
            if page == 0:
                # 초기화
                self.generate_matrix_mult(1)
            elif page < 127:
                # 행렬 곱셈 반복
                self.generate_matrix_mult(1)
                
                # 다음 페이지로
                next_page = page + 1
                if next_page <= 15:
                    self.add_inst("LOAD", next_page)
                else:
                    # 15 초과는 연산으로
                    base = next_page % 16
                    mult = next_page // 16
                    self.add_inst("LOAD", base)
                    if mult > 0:
                        self.add_inst("MUL", mult)
                
                self.add_inst("SLOT", 15)  # PAGE_REG
                self.add_inst("SETPAGE")
            else:
                # 마지막 페이지: 결과 출력
                self.add_inst("FETCH", 0)
                self.add_inst("OUT")
                self.add_inst("FETCH", 14)  # INST_COUNT
                self.add_inst("OUT")
                self.add_inst("HALT")
            
            # 페이지 쓰기
            self.write_page(bank, page)
            
            # 버퍼 클리어
            self.clear()

# ========================================
# 메인 프로그램
# ========================================

def main():
    print("=== 벤치마크 생성기 ===")
    print("1. 단순 반복 (빠름)")
    print("2. 행렬 곱셈 (중간)")
    print("3. 해시 함수 (가벼움)")
    print("4. 전체 벤치마크 (128페이지)")
    
    choice = input("선택: ")
    
    # 시리얼 연결
    ser = serial.Serial(find_arduino_port(), 115200, timeout=2)
    time.sleep(2)
    
    gen = BenchmarkGenerator(ser)
    
    if choice == "1":
        iterations = int(input("반복 횟수: "))
        bank = int(input("Bank (0/1): "))
        page = int(input("Page (0~127): "))
        
        gen.generate_simple_loop(iterations)
        gen.write_page(bank, page)
    
    elif choice == "2":
        count = int(input("행렬 곱셈 횟수: "))
        bank = int(input("Bank (0/1): "))
        page = int(input("Page (0~127): "))
        
        gen.generate_matrix_mult(count)
        gen.write_page(bank, page)
    
    elif choice == "3":
        iterations = int(input("해시 반복 횟수: "))
        bank = int(input("Bank (0/1): "))
        page = int(input("Page (0~127): "))
        
        gen.generate_hash_benchmark(iterations)
        gen.write_page(bank, page)
    
    elif choice == "4":
        bank = int(input("Bank (0/1): "))
        
        print(f"\n전체 벤치마크 생성 시작 (Bank {bank})")
        gen.generate_full_benchmark(bank)
        print("완료!")
    
    ser.close()

if __name__ == "__main__":
    main()
