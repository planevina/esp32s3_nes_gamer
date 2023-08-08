#ifndef _CPU_MACRO_H
#define _CPU_MACRO_H

// #define  NES6502_DISASM
// #define  NES6502_JUMPTABLE
// Define this to enable decimal mode in ADC / SBC (not needed in NES)
// #define  NES6502_DECIMAL

#define  ADD_CYCLES(x) { remaining_cycles -= (x); cpu.total_cycles += (x); }
#define PAGE_CROSS_CHECK(addr, reg) { if ((reg) > (uint8_t) (addr)) ADD_CYCLES(1); }
#define EMPTY_READ(value)  // empty 
#define IMMEDIATE_BYTE(value) { value = bank_readbyte(PC++); }
#define ABSOLUTE_ADDR(address) { address = bank_readword(PC); PC += 2; }
#define ABSOLUTE(address, value) { ABSOLUTE_ADDR(address); value = mem_readbyte(address); }
#define ABSOLUTE_BYTE(value) { ABSOLUTE(temp, value); }
#define ABS_IND_X_ADDR(address) { ABSOLUTE_ADDR(address); address = (address + X) & 0xFFFF; }
#define ABS_IND_X(address, value) { ABS_IND_X_ADDR(address); value = mem_readbyte(address); }
#define ABS_IND_X_BYTE(value) { ABS_IND_X(temp, value); }
#define ABS_IND_X_BYTE_READ(value) { ABS_IND_X_ADDR(temp); PAGE_CROSS_CHECK(temp, X); value = mem_readbyte(temp); }
#define ABS_IND_Y_ADDR(address) { ABSOLUTE_ADDR(address); address = (address + Y) & 0xFFFF; }
#define ABS_IND_Y(address, value) { ABS_IND_Y_ADDR(address); value = mem_readbyte(address); }
#define ABS_IND_Y_BYTE(value) { ABS_IND_Y(temp, value); }
#define ABS_IND_Y_BYTE_READ(value) { ABS_IND_Y_ADDR(temp); PAGE_CROSS_CHECK(temp, Y); value = mem_readbyte(temp); }
#define ZERO_PAGE_ADDR(address) { IMMEDIATE_BYTE(address); }
#define ZERO_PAGE(address, value) { ZERO_PAGE_ADDR(address); value = ZP_READBYTE(address); }
#define ZERO_PAGE_BYTE(value) { ZERO_PAGE(btemp, value); }
#define ZP_IND_X_ADDR(address) { ZERO_PAGE_ADDR(address); address += X; }
#define ZP_IND_X(address, value) { ZP_IND_X_ADDR(address); value = ZP_READBYTE(address); }
#define ZP_IND_X_BYTE(value) { ZP_IND_X(btemp, value); }
#define ZP_IND_Y_ADDR(address) { ZERO_PAGE_ADDR(address); address += Y; }
#define ZP_IND_Y_BYTE(value) { ZP_IND_Y_ADDR(btemp); value = ZP_READBYTE(btemp); }
#define INDIR_X_ADDR(address) { ZERO_PAGE_ADDR(btemp); btemp += X; address = zp_readword(btemp); }
#define INDIR_X(address, value) { INDIR_X_ADDR(address); value = mem_readbyte(address); }
#define INDIR_X_BYTE(value) { INDIR_X(temp, value); }
#define INDIR_Y_ADDR(address) { ZERO_PAGE_ADDR(btemp); address = (zp_readword(btemp) + Y) & 0xFFFF; }
#define INDIR_Y(address, value) { INDIR_Y_ADDR(address); value = mem_readbyte(address); }
#define INDIR_Y_BYTE(value) { INDIR_Y(temp, value); }
#define INDIR_Y_BYTE_READ(value) { INDIR_Y_ADDR(temp); PAGE_CROSS_CHECK(temp, Y); value = mem_readbyte(temp); }
#define  PUSH(value)             stack[S--] = (uint8_t) (value)
#define  PULL()                  stack[++S]
#define  SCATTER_FLAGS(value) { n_flag = (value) & N_FLAG; v_flag = (value) & V_FLAG; b_flag = (value) & B_FLAG; \
    d_flag = (value) & D_FLAG; i_flag = (value) & I_FLAG; z_flag = (0 == ((value) & Z_FLAG)); c_flag = (value) & C_FLAG; }
#define  COMBINE_FLAGS() ( (n_flag & N_FLAG) | (v_flag ? V_FLAG : 0) | R_FLAG | (b_flag ? B_FLAG : 0) | (d_flag ? D_FLAG : 0) \
                           | (i_flag ? I_FLAG : 0) | (z_flag ? 0 : Z_FLAG) | c_flag )
#define  SET_NZ_FLAGS(value)     n_flag = z_flag = (value);
#define RELATIVE_BRANCH(condition) { if (condition) { IMMEDIATE_BYTE(btemp); if (((int8_t) btemp + (PC & 0x00FF)) & 0x100) \
        ADD_CYCLES(1); ADD_CYCLES(3); PC += (int8_t) btemp; } else { PC++; ADD_CYCLES(2); } }
#define JUMP(address) { PC = bank_readword((address)); }
#define NMI_PROC() { PUSH(PC >> 8); PUSH(PC & 0xFF); b_flag = 0; PUSH(COMBINE_FLAGS()); i_flag = 1; JUMP(NMI_VECTOR); }
#define IRQ_PROC() { PUSH(PC >> 8); PUSH(PC & 0xFF); b_flag = 0; PUSH(COMBINE_FLAGS()); i_flag = 1; JUMP(IRQ_VECTOR); }

// Warning! NES CPU has no decimal mode, so by default this does no BCD!
#ifdef NES6502_DECIMAL
#define ADC(cycles, read_func) { read_func(data); if (d_flag) { temp = (A & 0x0F) + (data & 0x0F) + c_flag; if (temp >= 10) \
        temp = (temp - 10) | 0x10; temp += (A & 0xF0) + (data & 0xF0); z_flag = (A + data + c_flag) & 0xFF;  n_flag = temp; \
      v_flag = ((~(A ^ data)) & (A ^ temp) & 0x80); if (temp > 0x90) { temp += 0x60; c_flag = 1; } else { c_flag = 0; } \
      A = (uint8_t) temp; } else { temp = A + data + c_flag; c_flag = (temp >> 8) & 1; v_flag = ((~(A ^ data)) & (A ^ temp) & 0x80); \
      A = (uint8_t) temp; SET_NZ_FLAGS(A); } ADD_CYCLES(cycles); }
#else // NES6502_DECIMAL 
#define ADC(cycles, read_func) { read_func(data); temp = A + data + c_flag; c_flag = (temp >> 8) & 1; \
    v_flag = ((~(A ^ data)) & (A ^ temp) & 0x80); A = (uint8_t) temp; SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#endif // NES6502_DECIMAL 

#define ANC(cycles, read_func) { read_func(data); A &= data; c_flag = (n_flag & N_FLAG) >> 7; SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#define AND(cycles, read_func) { read_func(data); A &= data; SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#define ANE(cycles, read_func) { read_func(data); A = (A | 0xEE) & X & data; SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }

#ifdef NES6502_DECIMAL
#define ARR(cycles, read_func) { read_func(data); data &= A; if (d_flag) { temp = (data >> 1) | (c_flag << 7);  SET_NZ_FLAGS(temp); \
      v_flag = (temp ^ data) & 0x40; if (((data & 0x0F) + (data & 0x01)) > 5) temp = (temp & 0xF0) | ((temp + 0x6) & 0x0F); \
      if (((data & 0xF0) + (data & 0x10)) > 0x50) { temp = (temp & 0x0F) | ((temp + 0x60) & 0xF0); c_flag = 1; } else { \
        c_flag = 0; } A = (uint8_t) temp; } else { A = (data >> 1) | (c_flag << 7); SET_NZ_FLAGS(A); c_flag = (A & 0x40) >> 6; \
      v_flag = ((A >> 6) ^ (A >> 5)) & 1; } ADD_CYCLES(cycles); }
#else // NES6502_DECIMAL 
#define ARR(cycles, read_func) { read_func(data); data &= A; A = (data >> 1) | (c_flag << 7); \
    SET_NZ_FLAGS(A); c_flag = (A & 0x40) >> 6; v_flag = ((A >> 6) ^ (A >> 5)) & 1; ADD_CYCLES(cycles); }
#endif // NES6502_DECIMAL 

#define ASL(cycles, read_func, write_func, addr) { read_func(addr, data); c_flag = data >> 7; data <<= 1; write_func(addr, data); \
    SET_NZ_FLAGS(data); ADD_CYCLES(cycles); }
#define ASL_A() { c_flag = A >> 7; A <<= 1; SET_NZ_FLAGS(A); ADD_CYCLES(2); }
#define ASR(cycles, read_func) { read_func(data); data &= A; c_flag = data & 1; A = data >> 1; SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#define BCC() { RELATIVE_BRANCH(0 == c_flag); }
#define BCS() { RELATIVE_BRANCH(0 != c_flag); }
#define BEQ() { RELATIVE_BRANCH(0 == z_flag); }
#define BIT_(cycles, read_func) { read_func(data); n_flag = data; v_flag = data & V_FLAG; z_flag = data & A; ADD_CYCLES(cycles); }
#define BMI() { RELATIVE_BRANCH(n_flag & N_FLAG); }
#define BNE() { RELATIVE_BRANCH(0 != z_flag); }
#define BPL() { RELATIVE_BRANCH(0 == (n_flag & N_FLAG)); }
#define BRK() { PC++; PUSH(PC >> 8); PUSH(PC & 0xFF); b_flag = 1; PUSH(COMBINE_FLAGS()); i_flag = 1; JUMP(IRQ_VECTOR); ADD_CYCLES(7); }
#define BVC() { RELATIVE_BRANCH(0 == v_flag); }
#define BVS() { RELATIVE_BRANCH(0 != v_flag); }
#define CLC() { c_flag = 0; ADD_CYCLES(2); }
#define CLD() { d_flag = 0; ADD_CYCLES(2); }
#define CLI() { i_flag = 0; ADD_CYCLES(2); if (cpu.int_pending && remaining_cycles > 0) { cpu.int_pending = 0; \
      IRQ_PROC(); ADD_CYCLES(INT_CYCLES); } }
#define CLV() { v_flag = 0; ADD_CYCLES(2); }
#define _COMPARE(reg, value) { temp = (reg) - (value); c_flag = ((temp & 0x100) >> 8) ^ 1; SET_NZ_FLAGS((uint8_t) temp); }
#define CMP(cycles, read_func) { read_func(data); _COMPARE(A, data); ADD_CYCLES(cycles); }
#define CPX(cycles, read_func) { read_func(data); _COMPARE(X, data); ADD_CYCLES(cycles); }
#define CPY(cycles, read_func) { read_func(data); _COMPARE(Y, data); ADD_CYCLES(cycles);  }
#define DCP(cycles, read_func, write_func, addr) { read_func(addr, data); data--; write_func(addr, data); CMP(cycles, EMPTY_READ); }
#define _DEC(cycles, read_func, write_func, addr) { read_func(addr, data); data--; write_func(addr, data); SET_NZ_FLAGS(data); \
    ADD_CYCLES(cycles); }
#define DEX() { X--; SET_NZ_FLAGS(X); ADD_CYCLES(2); }
#define DEY() { Y--; SET_NZ_FLAGS(Y); ADD_CYCLES(2); }
#define DOP(cycles) { PC++; ADD_CYCLES(cycles); }
#define EOR(cycles, read_func) { read_func(data); A ^= data; SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#define INC(cycles, read_func, write_func, addr) { read_func(addr, data); data++; write_func(addr, data); \
    SET_NZ_FLAGS(data); ADD_CYCLES(cycles); }
#define INX() { X++; SET_NZ_FLAGS(X); ADD_CYCLES(2); }
#define INY() { Y++; SET_NZ_FLAGS(Y); ADD_CYCLES(2); }
#define ISB(cycles, read_func, write_func, addr) { read_func(addr, data); data++; write_func(addr, data); SBC(cycles, EMPTY_READ); }

#ifdef NES6502_TESTOPS
#define JAM() { cpu_Jam(); }
#else // !NES6502_TESTOPS 
#define JAM() { PC--; cpu.jammed = true; cpu.int_pending = 0; ADD_CYCLES(2); }
#endif // !NES6502_TESTOPS 

#define JMP_INDIRECT() { temp = bank_readword(PC); if (0xFF == (temp & 0xFF)) \
      PC = (bank_readbyte(temp & 0xFF00) << 8) | bank_readbyte(temp); else JUMP(temp); ADD_CYCLES(5); }
#define JMP_ABSOLUTE() { JUMP(PC); ADD_CYCLES(3); }
#define JSR() { PC++; PUSH(PC >> 8); PUSH(PC & 0xFF); JUMP(PC - 1); ADD_CYCLES(6); }
#define LAS(cycles, read_func) { read_func(data); A = X = S = (S & data); SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#define LAX(cycles, read_func) { read_func(A); X = A; SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#define LDA(cycles, read_func) { read_func(A); SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#define LDX(cycles, read_func) { read_func(X); SET_NZ_FLAGS(X); ADD_CYCLES(cycles); }
#define LDY(cycles, read_func) { read_func(Y); SET_NZ_FLAGS(Y); ADD_CYCLES(cycles); }
#define LSR(cycles, read_func, write_func, addr) { read_func(addr, data); c_flag = data & 1; data >>= 1; \
    write_func(addr, data); SET_NZ_FLAGS(data); ADD_CYCLES(cycles); }
#define LSR_A() { c_flag = A & 1; A >>= 1; SET_NZ_FLAGS(A); ADD_CYCLES(2); }
#define LXA(cycles, read_func) { read_func(data); A = X = ((A | 0xEE) & data); SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#define NOP_() { ADD_CYCLES(2); }
#define ORA(cycles, read_func) { read_func(data); A |= data; SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#define PHA() { PUSH(A); ADD_CYCLES(3); }
#define PHP() { PUSH(COMBINE_FLAGS() | B_FLAG); ADD_CYCLES(3); }
#define PLA() { A = PULL(); SET_NZ_FLAGS(A); ADD_CYCLES(4); }
#define PLP() { btemp = PULL(); SCATTER_FLAGS(btemp); ADD_CYCLES(4); }
#define RLA(cycles, read_func, write_func, addr) { read_func(addr, data); btemp = c_flag; c_flag = data >> 7; \
    data = (data << 1) | btemp; write_func(addr, data); A &= data; SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#define ROL(cycles, read_func, write_func, addr) { read_func(addr, data); btemp = c_flag; c_flag = data >> 7; \
    data = (data << 1) | btemp; write_func(addr, data); SET_NZ_FLAGS(data); ADD_CYCLES(cycles); }
#define ROL_A() { btemp = c_flag; c_flag = A >> 7; A = (A << 1) | btemp; SET_NZ_FLAGS(A); ADD_CYCLES(2); }
#define ROR(cycles, read_func, write_func, addr) { read_func(addr, data); btemp = c_flag << 7; c_flag = data & 1; \
    data = (data >> 1) | btemp; write_func(addr, data); SET_NZ_FLAGS(data); ADD_CYCLES(cycles); }
#define ROR_A() { btemp = c_flag << 7; c_flag = A & 1; A = (A >> 1) | btemp; SET_NZ_FLAGS(A); ADD_CYCLES(2); }
#define RRA(cycles, read_func, write_func, addr) { read_func(addr, data); btemp = c_flag << 7; c_flag = data & 1; \
    data = (data >> 1) | btemp; write_func(addr, data); ADC(cycles, EMPTY_READ); }
#define RTI() { btemp = PULL(); SCATTER_FLAGS(btemp); PC = PULL(); PC |= PULL() << 8; ADD_CYCLES(6); \
    if (0 == i_flag && cpu.int_pending && remaining_cycles > 0) { cpu.int_pending = 0; IRQ_PROC(); ADD_CYCLES(INT_CYCLES); } }
#define RTS() { PC = PULL(); PC = (PC | (PULL() << 8)) + 1; ADD_CYCLES(6); }
#define SAX(cycles, read_func, write_func, addr) { read_func(addr); data = A & X; write_func(addr, data); ADD_CYCLES(cycles); }

#ifdef NES6502_DECIMAL
#define SBC(cycles, read_func) { read_func(data); temp = A - data - (c_flag ^ 1); if (d_flag) { uint8_t al, ah; \
      al = (A & 0x0F) - (data & 0x0F) - (c_flag ^ 1); ah = (A >> 4) - (data >> 4); if (al & 0x10) { al -= 6; ah--; \
      } if (ah & 0x10) { ah -= 6; c_flag = 0; } else { c_flag = 1; } v_flag = (A ^ temp) & (A ^ data) & 0x80; \
      SET_NZ_FLAGS(temp & 0xFF); A = (ah << 4) | (al & 0x0F); } else { v_flag = (A ^ temp) & (A ^ data) & 0x80; \
      c_flag = ((temp & 0x100) >> 8) ^ 1; A = (uint8_t) temp; SET_NZ_FLAGS(A & 0xFF); } ADD_CYCLES(cycles); }
#else
#define SBC(cycles, read_func) { read_func(data); temp = A - data - (c_flag ^ 1); v_flag = (A ^ data) & (A ^ temp) & 0x80; \
    c_flag = ((temp >> 8) & 1) ^ 1; A = (uint8_t) temp; SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#endif // NES6502_DECIMAL 
#define SBX(cycles, read_func) { read_func(data); temp = (A & X) - data; c_flag = ((temp >> 8) & 1) ^ 1; X = temp & 0xFF; \
    SET_NZ_FLAGS(X); ADD_CYCLES(cycles); }
#define SEC() { c_flag = 1; ADD_CYCLES(2); }
#define SED() { d_flag = 1; ADD_CYCLES(2); }
#define SEI() { i_flag = 1; ADD_CYCLES(2); }
#define SHA(cycles, read_func, write_func, addr) { read_func(addr); data = A & X & ((uint8_t) ((addr >> 8) + 1)); \
    write_func(addr, data); ADD_CYCLES(cycles); }
#define SHS(cycles, read_func, write_func, addr) { read_func(addr); S = A & X; data = S & ((uint8_t) ((addr >> 8) + 1)); \
    write_func(addr, data); ADD_CYCLES(cycles); }
#define SHX(cycles, read_func, write_func, addr) { read_func(addr); data = X & ((uint8_t) ((addr >> 8) + 1)); \
    write_func(addr, data); ADD_CYCLES(cycles); }
#define SHY(cycles, read_func, write_func, addr) { read_func(addr); data = Y & ((uint8_t) ((addr >> 8 ) + 1)); \
    write_func(addr, data); ADD_CYCLES(cycles); }
#define SLO(cycles, read_func, write_func, addr) { read_func(addr, data); c_flag = data >> 7; data <<= 1; \
    write_func(addr, data); A |= data; SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#define SRE(cycles, read_func, write_func, addr) { read_func(addr, data); c_flag = data & 1; data >>= 1; \
    write_func(addr, data); A ^= data; SET_NZ_FLAGS(A); ADD_CYCLES(cycles); }
#define STA(cycles, read_func, write_func, addr) { read_func(addr); write_func(addr, A); ADD_CYCLES(cycles); }
#define STX(cycles, read_func, write_func, addr) { read_func(addr); write_func(addr, X); ADD_CYCLES(cycles); }
#define STY(cycles, read_func, write_func, addr) { read_func(addr); write_func(addr, Y); ADD_CYCLES(cycles); }
#define TAX() { X = A; SET_NZ_FLAGS(X); ADD_CYCLES(2); }
#define TAY() { Y = A; SET_NZ_FLAGS(Y); ADD_CYCLES(2); }
#define TOP() { PC += 2; ADD_CYCLES(4); }
#define TSX() { X = S; SET_NZ_FLAGS(X); ADD_CYCLES(2); }
#define TXA() { A = X; SET_NZ_FLAGS(A); ADD_CYCLES(2); }
#define TXS() { S = X; ADD_CYCLES(2); }
#define TYA() { A = Y; SET_NZ_FLAGS(A); ADD_CYCLES(2); }

#define  ZP_READBYTE(addr)          ram[(addr)]
#define  ZP_WRITEBYTE(addr, value)  ram[(addr)] = (uint8_t) (value)

#endif