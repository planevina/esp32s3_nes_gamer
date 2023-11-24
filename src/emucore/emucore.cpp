#include "emucore.h"
#include "cpumacro.h"
#include "display.h"
#include "../indev/controller.h"
#include <SD_MMC.h>


//输入设备
static int pad0_readcount, pad1_readcount, ppad_readcount, ark_readcount;

void input_strobe(void)
{
    pad0_readcount = 0;
    pad1_readcount = 0;
    ppad_readcount = 0;
    ark_readcount = 0;
}

//直接读取全局变量的值，全局变量的值在循环内input_refresh()函数内更新
uint8_t get_pad0(void)
{
    uint8_t value = gamepad_p1.KEY_VALUE;
    return (((value >> pad0_readcount++) & 1));
}

uint8_t get_pad1(void) 
{
    uint8_t value = gamepad_p2.KEY_VALUE;
    return (((value >> pad1_readcount++) & 1));
}

//模拟器部分开始
apu_t apu;
mmc_t mmc;

//nes_t nes;
nes_t *NESmachine;

//--------------------------------------------------------------------------------
//
//   以下是合并的游戏机模拟部分的代码，合并的C文件，H文件在emucore.h内
//
//--------------------------------------------------------------------------------
// ROM.C
// Max length for displayed filename
#define ROM_DISP_MAXLEN 20

#define ROM_FOURSCREEN 0x08
#define ROM_TRAINER 0x04
#define ROM_BATTERY 0x02
#define ROM_MIRRORTYPE 0x01
#define ROM_INES_MAGIC "NES\x1A"

#define TRAINER_OFFSET 0x1000
#define TRAINER_LENGTH 0x200
#define VRAM_LENGTH 0x2000
#define ROM_BANK_LENGTH 0x4000
#define VROM_BANK_LENGTH 0x2000
#define SRAM_BANK_LENGTH 0x0400
#define VRAM_BANK_LENGTH 0x2000

// Allocate space for SRAM

/*
int rom_allocsram(rominfo_t *rominfo)
{
    // Load up SRAM
    if (NULL == rominfo->sram)
        rominfo->sram = (uint8_t *)heap_caps_malloc(SRAM_BANK_LENGTH * rominfo->sram_banks, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == rominfo->sram)
    {
        /// gui_sendmsg(GUI_RED, "Could not allocate space for battery RAM");
        return -1;
    }

    // make damn sure SRAM is clear
    memset(rominfo->sram, 0, SRAM_BANK_LENGTH * rominfo->sram_banks);
    return 0;
}
*/

int rom_allocsram(rominfo_t **rominfo) {
  // Load up SRAM
  
  if (NULL == (*rominfo)->sram) (*rominfo)->sram = (uint8_t *)heap_caps_malloc(SRAM_BANK_LENGTH * (*rominfo)->sram_banks, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (NULL == (*rominfo)->sram) {
    ///gui_sendmsg(GUI_RED, "Could not allocate space for battery RAM");
    return -1;
  }
  // make damn sure SRAM is clear
  memset((*rominfo)->sram, 0, SRAM_BANK_LENGTH * (*rominfo)->sram_banks);
  return 0;
}

// If there's a trainer, load it in at $7000

void rom_loadtrainer(unsigned char **rom, rominfo_t *rominfo)
{
    if (rominfo->flags & ROM_FLAG_TRAINER)
    {
        memcpy(rominfo->sram + TRAINER_OFFSET, *rom, TRAINER_LENGTH);
        rom += TRAINER_LENGTH;
        /// nes_log_printf("Read in trainer at $7000\n");
    }
}

int rom_loadrom(unsigned char **rom, rominfo_t *rominfo)
{
    rominfo->rom = *rom;
    *rom += ROM_BANK_LENGTH * rominfo->rom_banks;

    // If there's VROM, allocate and stuff it in
    if (rominfo->vrom_banks)
    {
        rominfo->vrom = *rom;
        *rom += VROM_BANK_LENGTH * rominfo->vrom_banks;
    }
    else
    {
        if (NULL == rominfo->vram)
            rominfo->vram = (uint8_t *)heap_caps_malloc(VRAM_LENGTH, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (NULL == rominfo->vram)
        {
            /// gui_sendmsg(GUI_RED, "Could not allocate space for VRAM");
            return -1;
        }
        memset(rominfo->vram, 0, VRAM_LENGTH);
    }

    return 0;
}

#define RESERVED_LENGTH 8


int rom_getheader(unsigned char **rom, rominfo_t *rominfo)
{
    inesheader_t head;
    uint8_t reserved[RESERVED_LENGTH];
    bool header_dirty;

    SHOW_MSG_SERIAL("<Head:  ")
    SHOW_MSG_SERIAL((*rom)[0]);
    SHOW_MSG_SERIAL("  ");
    SHOW_MSG_SERIAL((*rom)[1]);
    SHOW_MSG_SERIAL("  ");
    SHOW_MSG_SERIAL((*rom)[2]);
    SHOW_MSG_SERIAL("  ");
    SHOW_MSG_SERIAL((*rom)[3]);
    SHOW_MSG_SERIAL(">")

    memcpy(&head, *rom, sizeof(head));
    *rom += sizeof(head);

    if (memcmp(head.ines_magic, ROM_INES_MAGIC, 4))
    {
        SHOW_MSG_SERIAL("<ROM is not a valid NES image>")
        return -1;
    }

    rominfo->rom_banks = head.rom_banks;
    rominfo->vrom_banks = head.vrom_banks;
    // iNES assumptions
    rominfo->sram_banks = 8; // 1kB banks, so 8KB
    rominfo->vram_banks = 1; // 8kB banks, so 8KB
    rominfo->mirror = (head.rom_type & ROM_MIRRORTYPE) ? MIRROR_VERT : MIRROR_HORIZ;
    rominfo->flags = 0;
    if (head.rom_type & ROM_BATTERY)
        rominfo->flags |= ROM_FLAG_BATTERY;
    if (head.rom_type & ROM_TRAINER)
        rominfo->flags |= ROM_FLAG_TRAINER;
    if (head.rom_type & ROM_FOURSCREEN)
        rominfo->flags |= ROM_FLAG_FOURSCREEN;
    // TODO: fourscreen a mirroring type?
    rominfo->mapper_number = head.rom_type >> 4;

    // Do a compare - see if we've got a clean extended header
    memset(reserved, 0, RESERVED_LENGTH);
    if (0 == memcmp(head.reserved, reserved, RESERVED_LENGTH))
    {
        // We were clean
        header_dirty = false;
        rominfo->mapper_number |= (head.mapper_hinybble & 0xF0);
    }
    else
    {
        header_dirty = true;

        // @!?#@! DiskDude.
        if (('D' == head.mapper_hinybble) && (0 == memcmp(head.reserved, "iskDude!", 8)))
        {
            /// nes_log_printf("`DiskDude!' found in ROM header, ignoring high mapper nybble\n");
        }
        else
        {
            /// nes_log_printf("ROM header dirty, possible problem\n");
            rominfo->mapper_number |= (head.mapper_hinybble & 0xF0);
        }
        /// rom_adddirty(rominfo->filename);
    }
    // Check for VS unisystem mapper
    if (99 == rominfo->mapper_number)
        rominfo->flags |= ROM_FLAG_VERSUS;
    return 0;
}

/* Free a ROM */

void rom_free(rominfo_t **rominfo)
{
    if (NULL == *rominfo)
    {
        return;
    }
    if ((*rominfo)->sram)
        free((*rominfo)->sram);
    if ((*rominfo)->rom)
        free((*rominfo)->rom);
    if ((*rominfo)->vrom)
        free((*rominfo)->vrom);
    if ((*rominfo)->vram)
        free((*rominfo)->vram);

    free(*rominfo);
}
//================================================================================

//--------------------------------------------------------------------------------
/// CPU.C:

// internal CPU context
static nes6502_context cpu;
static int remaining_cycles = 0; // so we can release timeslice
// memory region pointers
static uint8_t *ram = NULL, *stack = NULL;
static uint8_t null_page[NES6502_BANKSIZE];


#ifdef HOST_LITTLE_ENDIAN
static inline uint32_t zp_readword(register uint8_t address)
{
    return (uint32_t)(*(uint16_t *)(ram + address));
}
static inline uint32_t bank_readword(register uint32_t address)
{
    return (uint32_t)(*(uint16_t *)(cpu.mem_page[address >> NES6502_BANKSHIFT] + (address & NES6502_BANKMASK)));
}
#else  // !HOST_LITTLE_ENDIAN
static inline uint32_t zp_readword(register uint8_t address)
{
    uint32_t x = (uint32_t) * (uint16_t *)(ram + address);
    return (x << 8) | (x >> 8);
}
static inline uint32_t bank_readword(register uint32_t address)
{
    uint32_t x = (uint32_t) * (uint16_t *)(cpu.mem_page[address >> NES6502_BANKSHIFT] + (address & NES6502_BANKMASK));
    return (x << 8) | (x >> 8);
}
#endif // !HOST_LITTLE_ENDIAN

static inline uint8_t bank_readbyte(register uint32_t address)
{
    return cpu.mem_page[address >> NES6502_BANKSHIFT][address & NES6502_BANKMASK];
}
static inline void bank_writebyte(register uint32_t address, register uint8_t value)
{
    cpu.mem_page[address >> NES6502_BANKSHIFT][address & NES6502_BANKMASK] = value;
}
static uint8_t mem_readbyte(uint32_t address)
{
    nes6502_memread *mr;

    if (address < 0x800)
    { // RAM
        return ram[address];
    }
    else if (address >= 0x8000)
    {
        return bank_readbyte(address);
    }
    else
    {
        for (mr = cpu.read_handler; mr->min_range != 0xFFFFFFFF; mr++)
        {
            if (address >= mr->min_range && address <= mr->max_range)
                return mr->read_func(address);
        }
    }
    return bank_readbyte(address);
}

static void mem_writebyte(uint32_t address, uint8_t value)
{
    nes6502_memwrite *mw;
    if (address < 0x800)
    {
        ram[address] = value;
        return;
    }
    else
    {
        for (mw = cpu.write_handler; mw->min_range != 0xFFFFFFFF; mw++)
        {
            if (address >= mw->min_range && address <= mw->max_range)
            {
                mw->write_func(address, value);
                return;
            }
        }
    }
    bank_writebyte(address, value);
}

void nes6502_setcontext(nes6502_context *context);
void nes6502_setcontext(nes6502_context *context)
{
    int loop;
    cpu = *context;
    for (loop = 0; loop < NES6502_NUMBANKS; loop++)
    {
        if (NULL == cpu.mem_page[loop])
            cpu.mem_page[loop] = null_page;
    }
    ram = cpu.mem_page[0]; // quick zero-page/RAM references
    stack = ram + STACK_OFFSET;
}

void nes6502_getcontext(nes6502_context *context);
void nes6502_getcontext(nes6502_context *context)
{
    int loop;
    *context = cpu;
    for (loop = 0; loop < NES6502_NUMBANKS; loop++)
    {
        if (null_page == context->mem_page[loop])
            context->mem_page[loop] = NULL;
    }
}

uint8_t nes6502_getbyte(uint32_t address)
{
    return bank_readbyte(address);
}

uint32_t nes6502_getcycles(bool reset_flag)
{
    uint32_t cycles = cpu.total_cycles;
    if (reset_flag)
        cpu.total_cycles = 0;
    return cycles;
}

#define  GET_GLOBAL_REGS() { PC = cpu.pc_reg; A = cpu.a_reg; X = cpu.x_reg; Y = cpu.y_reg; SCATTER_FLAGS(cpu.p_reg); S = cpu.s_reg; }
#define  STORE_LOCAL_REGS() { cpu.pc_reg = PC; cpu.a_reg = A; cpu.x_reg = X; cpu.y_reg = Y; cpu.p_reg = COMBINE_FLAGS(); cpu.s_reg = S; }

#ifndef MIN
  #define  MIN(a,b)    (((a) < (b)) ? (a) : (b))
#endif 

#ifdef NES6502_JUMPTABLE
#define  OPCODE_BEGIN(xx)  op##xx:
#define  OPCODE_END if (remaining_cycles <= 0) goto end_execute; goto *opcode_table[bank_readbyte(PC++)];
#else // !NES6502_JUMPTABLE 
#define  OPCODE_BEGIN(xx)  case 0x##xx:
#define  OPCODE_END        break;
#endif // !NES6502_JUMPTABLE 


int nes6502_execute(int timeslice_cycles)
{
    int old_cycles = cpu.total_cycles;

    uint32_t temp, addr;  // for macros
    uint8_t btemp, baddr; // for macros
    uint8_t data;

    uint8_t n_flag, v_flag, b_flag;
    uint8_t d_flag, i_flag, z_flag, c_flag;

    uint32_t PC;
    uint8_t A, X, Y, S;

#ifdef NES6502_JUMPTABLE
    static const void *opcode_table[256] =
        {
            &&op00, &&op01, &&op02, &&op03, &&op04, &&op05, &&op06, &&op07,
            &&op08, &&op09, &&op0A, &&op0B, &&op0C, &&op0D, &&op0E, &&op0F,
            &&op10, &&op11, &&op12, &&op13, &&op14, &&op15, &&op16, &&op17,
            &&op18, &&op19, &&op1A, &&op1B, &&op1C, &&op1D, &&op1E, &&op1F,
            &&op20, &&op21, &&op22, &&op23, &&op24, &&op25, &&op26, &&op27,
            &&op28, &&op29, &&op2A, &&op2B, &&op2C, &&op2D, &&op2E, &&op2F,
            &&op30, &&op31, &&op32, &&op33, &&op34, &&op35, &&op36, &&op37,
            &&op38, &&op39, &&op3A, &&op3B, &&op3C, &&op3D, &&op3E, &&op3F,
            &&op40, &&op41, &&op42, &&op43, &&op44, &&op45, &&op46, &&op47,
            &&op48, &&op49, &&op4A, &&op4B, &&op4C, &&op4D, &&op4E, &&op4F,
            &&op50, &&op51, &&op52, &&op53, &&op54, &&op55, &&op56, &&op57,
            &&op58, &&op59, &&op5A, &&op5B, &&op5C, &&op5D, &&op5E, &&op5F,
            &&op60, &&op61, &&op62, &&op63, &&op64, &&op65, &&op66, &&op67,
            &&op68, &&op69, &&op6A, &&op6B, &&op6C, &&op6D, &&op6E, &&op6F,
            &&op70, &&op71, &&op72, &&op73, &&op74, &&op75, &&op76, &&op77,
            &&op78, &&op79, &&op7A, &&op7B, &&op7C, &&op7D, &&op7E, &&op7F,
            &&op80, &&op81, &&op82, &&op83, &&op84, &&op85, &&op86, &&op87,
            &&op88, &&op89, &&op8A, &&op8B, &&op8C, &&op8D, &&op8E, &&op8F,
            &&op90, &&op91, &&op92, &&op93, &&op94, &&op95, &&op96, &&op97,
            &&op98, &&op99, &&op9A, &&op9B, &&op9C, &&op9D, &&op9E, &&op9F,
            &&opA0, &&opA1, &&opA2, &&opA3, &&opA4, &&opA5, &&opA6, &&opA7,
            &&opA8, &&opA9, &&opAA, &&opAB, &&opAC, &&opAD, &&opAE, &&opAF,
            &&opB0, &&opB1, &&opB2, &&opB3, &&opB4, &&opB5, &&opB6, &&opB7,
            &&opB8, &&opB9, &&opBA, &&opBB, &&opBC, &&opBD, &&opBE, &&opBF,
            &&opC0, &&opC1, &&opC2, &&opC3, &&opC4, &&opC5, &&opC6, &&opC7,
            &&opC8, &&opC9, &&opCA, &&opCB, &&opCC, &&opCD, &&opCE, &&opCF,
            &&opD0, &&opD1, &&opD2, &&opD3, &&opD4, &&opD5, &&opD6, &&opD7,
            &&opD8, &&opD9, &&opDA, &&opDB, &&opDC, &&opDD, &&opDE, &&opDF,
            &&opE0, &&opE1, &&opE2, &&opE3, &&opE4, &&opE5, &&opE6, &&opE7,
            &&opE8, &&opE9, &&opEA, &&opEB, &&opEC, &&opED, &&opEE, &&opEF,
            &&opF0, &&opF1, &&opF2, &&opF3, &&opF4, &&opF5, &&opF6, &&opF7,
            &&opF8, &&opF9, &&opFA, &&opFB, &&opFC, &&opFD, &&opFE, &&opFF
        };
#endif // NES6502_JUMPTABLE

    remaining_cycles = timeslice_cycles;
    GET_GLOBAL_REGS();

    // check for DMA cycle burning
    if (cpu.burn_cycles && remaining_cycles > 0)
    {
        int burn_for;
        burn_for = MIN(remaining_cycles, cpu.burn_cycles);
        ADD_CYCLES(burn_for);
        cpu.burn_cycles -= burn_for;
    }
    if (0 == i_flag && cpu.int_pending && remaining_cycles > 0)
    {
        cpu.int_pending = 0;
        IRQ_PROC();
        ADD_CYCLES(INT_CYCLES);
    }
#ifdef NES6502_JUMPTABLE
    OPCODE_END
#else  // !NES6502_JUMPTABLE

    // Continue until we run out of cycles
    while (remaining_cycles > 0)
    {
        switch (bank_readbyte(PC++))
        {
#endif // !NES6502_JUMPTABLE

    OPCODE_BEGIN(00) // BRK
    BRK();
    OPCODE_END

    OPCODE_BEGIN(01) // ORA ($nn,X)
    ORA(6, INDIR_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(02) // JAM
    OPCODE_BEGIN(12) // JAM
    OPCODE_BEGIN(22) // JAM
    OPCODE_BEGIN(32) // JAM
    OPCODE_BEGIN(42) // JAM
    OPCODE_BEGIN(52) // JAM
    OPCODE_BEGIN(62) // JAM
    OPCODE_BEGIN(72) // JAM
    OPCODE_BEGIN(92) // JAM
    OPCODE_BEGIN(B2) // JAM
    OPCODE_BEGIN(D2) // JAM
    OPCODE_BEGIN(F2) // JAM
    JAM();
    // kill the CPU
    remaining_cycles = 0;
    OPCODE_END

    OPCODE_BEGIN(03)
    SLO(8, INDIR_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(04) // NOP $nn
    OPCODE_BEGIN(44) // NOP $nn
    OPCODE_BEGIN(64) // NOP $nn
    DOP(3);
    OPCODE_END

    OPCODE_BEGIN(05) // ORA $nn
    ORA(3, ZERO_PAGE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(06) // ASL $nn
    ASL(5, ZERO_PAGE, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(07) // SLO $nn
    SLO(5, ZERO_PAGE, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(08) // PHP
    PHP();
    OPCODE_END

    OPCODE_BEGIN(09) // ORA #$nn
    ORA(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(0A) // ASL A
    ASL_A();
    OPCODE_END

    OPCODE_BEGIN(0B) // ANC #$nn
    ANC(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(0C) // NOP $nnnn
    TOP();
    OPCODE_END

    OPCODE_BEGIN(0D) // ORA $nnnn
    ORA(4, ABSOLUTE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(0E) // ASL $nnnn
    ASL(6, ABSOLUTE, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(0F) // SLO $nnnn
    SLO(6, ABSOLUTE, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(10) // BPL $nnnn
    BPL();
    OPCODE_END

    OPCODE_BEGIN(11) // ORA ($nn),Y
    ORA(5, INDIR_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(13) // SLO ($nn),Y
    SLO(8, INDIR_Y, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(14) // NOP $nn,X
    OPCODE_BEGIN(34) // NOP
    OPCODE_BEGIN(54) // NOP $nn,X
    OPCODE_BEGIN(74) // NOP $nn,X
    OPCODE_BEGIN(D4) // NOP $nn,X
    OPCODE_BEGIN(F4) // NOP ($nn,X)
    DOP(4);
    OPCODE_END

    OPCODE_BEGIN(15) // ORA $nn,X
    ORA(4, ZP_IND_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(16) // ASL $nn,X
    ASL(6, ZP_IND_X, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(17) // SLO $nn,X
    SLO(6, ZP_IND_X, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(18) // CLC
    CLC();
    OPCODE_END

    OPCODE_BEGIN(19) // ORA $nnnn,Y
    ORA(4, ABS_IND_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(1A) // NOP
    OPCODE_BEGIN(3A) // NOP
    OPCODE_BEGIN(5A) // NOP
    OPCODE_BEGIN(7A) // NOP
    OPCODE_BEGIN(DA) // NOP
    OPCODE_BEGIN(FA) // NOP
    NOP_();
    OPCODE_END

    OPCODE_BEGIN(1B) // SLO $nnnn,Y
    SLO(7, ABS_IND_Y, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(1C) // NOP $nnnn,X
    OPCODE_BEGIN(3C) // NOP $nnnn,X
    OPCODE_BEGIN(5C) // NOP $nnnn,X
    OPCODE_BEGIN(7C) // NOP $nnnn,X
    OPCODE_BEGIN(DC) // NOP $nnnn,X
    OPCODE_BEGIN(FC) // NOP $nnnn,X
    TOP();
    OPCODE_END

    OPCODE_BEGIN(1D) // ORA $nnnn,X
    ORA(4, ABS_IND_X_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(1E) // ASL $nnnn,X
    ASL(7, ABS_IND_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(1F) // SLO $nnnn,X
    SLO(7, ABS_IND_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(20) // JSR $nnnn
    JSR();
    OPCODE_END

    OPCODE_BEGIN(21) // AND ($nn,X)
    AND(6, INDIR_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(23) // RLA ($nn,X)
    RLA(8, INDIR_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(24) // BIT $nn
    BIT_(3, ZERO_PAGE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(25) // AND $nn
    AND(3, ZERO_PAGE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(26) // ROL $nn
    ROL(5, ZERO_PAGE, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(27) // RLA $nn
    RLA(5, ZERO_PAGE, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(28) // PLP
    PLP();
    OPCODE_END

    OPCODE_BEGIN(29) // AND #$nn
    AND(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(2A) // ROL A
    ROL_A();
    OPCODE_END

    OPCODE_BEGIN(2B) // ANC #$nn
    ANC(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(2C) // BIT $nnnn
    BIT_(4, ABSOLUTE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(2D) // AND $nnnn
    AND(4, ABSOLUTE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(2E) // ROL $nnnn
    ROL(6, ABSOLUTE, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(2F) // RLA $nnnn
    RLA(6, ABSOLUTE, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(30) // BMI $nnnn
    BMI();
    OPCODE_END

    OPCODE_BEGIN(31) // AND ($nn),Y
    AND(5, INDIR_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(33) // RLA ($nn),Y
    RLA(8, INDIR_Y, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(35) // AND $nn,X
    AND(4, ZP_IND_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(36) // ROL $nn,X
    ROL(6, ZP_IND_X, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(37) // RLA $nn,X
    RLA(6, ZP_IND_X, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(38) // SEC
    SEC();
    OPCODE_END

    OPCODE_BEGIN(39) // AND $nnnn,Y
    AND(4, ABS_IND_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(3B) // RLA $nnnn,Y
    RLA(7, ABS_IND_Y, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(3D) // AND $nnnn,X
    AND(4, ABS_IND_X_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(3E) // ROL $nnnn,X
    ROL(7, ABS_IND_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(3F) // RLA $nnnn,X
    RLA(7, ABS_IND_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(40) // RTI
    RTI();
    OPCODE_END

    OPCODE_BEGIN(41) // EOR ($nn,X)
    EOR(6, INDIR_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(43) // SRE ($nn,X)
    SRE(8, INDIR_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(45) // EOR $nn
    EOR(3, ZERO_PAGE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(46) // LSR $nn
    LSR(5, ZERO_PAGE, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(47) // SRE $nn
    SRE(5, ZERO_PAGE, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(48) // PHA
    PHA();
    OPCODE_END

    OPCODE_BEGIN(49) // EOR #$nn
    EOR(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(4A) // LSR A
    LSR_A();
    OPCODE_END

    OPCODE_BEGIN(4B) // ASR #$nn
    ASR(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(4C) // JMP $nnnn
    JMP_ABSOLUTE();
    OPCODE_END

    OPCODE_BEGIN(4D) // EOR $nnnn
    EOR(4, ABSOLUTE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(4E) // LSR $nnnn
    LSR(6, ABSOLUTE, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(4F) // SRE $nnnn
    SRE(6, ABSOLUTE, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(50) // BVC $nnnn
    BVC();
    OPCODE_END

    OPCODE_BEGIN(51) // EOR ($nn),Y
    EOR(5, INDIR_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(53) // SRE ($nn),Y
    SRE(8, INDIR_Y, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(55) // EOR $nn,X
    EOR(4, ZP_IND_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(56) // LSR $nn,X
    LSR(6, ZP_IND_X, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(57) // SRE $nn,X
    SRE(6, ZP_IND_X, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(58) // CLI
    CLI();
    OPCODE_END

    OPCODE_BEGIN(59) // EOR $nnnn,Y
    EOR(4, ABS_IND_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(5B) // SRE $nnnn,Y
    SRE(7, ABS_IND_Y, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(5D) // EOR $nnnn,X
    EOR(4, ABS_IND_X_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(5E) // LSR $nnnn,X
    LSR(7, ABS_IND_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(5F) // SRE $nnnn,X
    SRE(7, ABS_IND_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(60) // RTS
    RTS();
    OPCODE_END

    OPCODE_BEGIN(61) // ADC ($nn,X)
    ADC(6, INDIR_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(63) // RRA ($nn,X)
    RRA(8, INDIR_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(65) // ADC $nn
    ADC(3, ZERO_PAGE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(66) // ROR $nn
    ROR(5, ZERO_PAGE, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(67) // RRA $nn
    RRA(5, ZERO_PAGE, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(68) // PLA
    PLA();
    OPCODE_END

    OPCODE_BEGIN(69) // ADC #$nn
    ADC(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(6A) // ROR A
    ROR_A();
    OPCODE_END

    OPCODE_BEGIN(6B) // ARR #$nn
    ARR(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(6C) // JMP ($nnnn)
    JMP_INDIRECT();
    OPCODE_END

    OPCODE_BEGIN(6D) // ADC $nnnn
    ADC(4, ABSOLUTE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(6E) // ROR $nnnn
    ROR(6, ABSOLUTE, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(6F) // RRA $nnnn
    RRA(6, ABSOLUTE, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(70) // BVS $nnnn
    BVS();
    OPCODE_END

    OPCODE_BEGIN(71) // ADC ($nn),Y
    ADC(5, INDIR_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(73) // RRA ($nn),Y
    RRA(8, INDIR_Y, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(75) // ADC $nn,X
    ADC(4, ZP_IND_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(76) // ROR $nn,X
    ROR(6, ZP_IND_X, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(77) // RRA $nn,X
    RRA(6, ZP_IND_X, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(78) // SEI
    SEI();
    OPCODE_END

    OPCODE_BEGIN(79) // ADC $nnnn,Y
    ADC(4, ABS_IND_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(7B) // RRA $nnnn,Y
    RRA(7, ABS_IND_Y, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(7D) // ADC $nnnn,X
    ADC(4, ABS_IND_X_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(7E) // ROR $nnnn,X
    ROR(7, ABS_IND_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(7F) // RRA $nnnn,X
    RRA(7, ABS_IND_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(80) // NOP #$nn
    OPCODE_BEGIN(82) // NOP #$nn
    OPCODE_BEGIN(89) // NOP #$nn
    OPCODE_BEGIN(C2) // NOP #$nn
    OPCODE_BEGIN(E2) // NOP #$nn
    DOP(2);
    OPCODE_END

    OPCODE_BEGIN(81) // STA ($nn,X)
    STA(6, INDIR_X_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(83) // SAX ($nn,X)
    SAX(6, INDIR_X_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(84) // STY $nn
    STY(3, ZERO_PAGE_ADDR, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(85) // STA $nn
    STA(3, ZERO_PAGE_ADDR, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(86) // STX $nn
    STX(3, ZERO_PAGE_ADDR, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(87) // SAX $nn
    SAX(3, ZERO_PAGE_ADDR, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(88) // DEY
    DEY();
    OPCODE_END

    OPCODE_BEGIN(8A) // TXA
    TXA();
    OPCODE_END

    OPCODE_BEGIN(8B) // ANE #$nn
    ANE(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(8C) // STY $nnnn
    STY(4, ABSOLUTE_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(8D) // STA $nnnn
    STA(4, ABSOLUTE_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(8E) // STX $nnnn
    STX(4, ABSOLUTE_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(8F) // SAX $nnnn
    SAX(4, ABSOLUTE_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(90) // BCC $nnnn
    BCC();
    OPCODE_END

    OPCODE_BEGIN(91) // STA ($nn),Y
    STA(6, INDIR_Y_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(93) // SHA ($nn),Y
    SHA(6, INDIR_Y_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(94) // STY $nn,X
    STY(4, ZP_IND_X_ADDR, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(95) // STA $nn,X
    STA(4, ZP_IND_X_ADDR, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(96) // STX $nn,Y
    STX(4, ZP_IND_Y_ADDR, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(97) // SAX $nn,Y
    SAX(4, ZP_IND_Y_ADDR, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(98) // TYA
    TYA();
    OPCODE_END

    OPCODE_BEGIN(99) // STA $nnnn,Y
    STA(5, ABS_IND_Y_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(9A) // TXS
    TXS();
    OPCODE_END

    OPCODE_BEGIN(9B) // SHS $nnnn,Y
    SHS(5, ABS_IND_Y_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(9C) // SHY $nnnn,X
    SHY(5, ABS_IND_X_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(9D) // STA $nnnn,X
    STA(5, ABS_IND_X_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(9E) // SHX $nnnn,Y
    SHX(5, ABS_IND_Y_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(9F) // SHA $nnnn,Y
    SHA(5, ABS_IND_Y_ADDR, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(A0) // LDY #$nn
    LDY(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(A1) // LDA ($nn,X)
    LDA(6, INDIR_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(A2) // LDX #$nn
    LDX(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(A3) // LAX ($nn,X)
    LAX(6, INDIR_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(A4) // LDY $nn
    LDY(3, ZERO_PAGE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(A5) // LDA $nn
    LDA(3, ZERO_PAGE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(A6) // LDX $nn
    LDX(3, ZERO_PAGE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(A7) // LAX $nn
    LAX(3, ZERO_PAGE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(A8) // TAY
    TAY();
    OPCODE_END

    OPCODE_BEGIN(A9) // LDA #$nn
    LDA(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(AA) // TAX
    TAX();
    OPCODE_END

    OPCODE_BEGIN(AB) // LXA #$nn
    LXA(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(AC) // LDY $nnnn
    LDY(4, ABSOLUTE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(AD) // LDA $nnnn
    LDA(4, ABSOLUTE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(AE) // LDX $nnnn
    LDX(4, ABSOLUTE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(AF) // LAX $nnnn
    LAX(4, ABSOLUTE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(B0) // BCS $nnnn
    BCS();
    OPCODE_END

    OPCODE_BEGIN(B1) // LDA ($nn),Y
    LDA(5, INDIR_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(B3) // LAX ($nn),Y
    LAX(5, INDIR_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(B4) // LDY $nn,X
    LDY(4, ZP_IND_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(B5) // LDA $nn,X
    LDA(4, ZP_IND_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(B6) // LDX $nn,Y
    LDX(4, ZP_IND_Y_BYTE);
    OPCODE_END

    OPCODE_BEGIN(B7) // LAX $nn,Y
    LAX(4, ZP_IND_Y_BYTE);
    OPCODE_END

    OPCODE_BEGIN(B8) // CLV
    CLV();
    OPCODE_END

    OPCODE_BEGIN(B9) // LDA $nnnn,Y
    LDA(4, ABS_IND_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(BA) // TSX
    TSX();
    OPCODE_END

    OPCODE_BEGIN(BB) // LAS $nnnn,Y
    LAS(4, ABS_IND_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(BC) // LDY $nnnn,X
    LDY(4, ABS_IND_X_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(BD) // LDA $nnnn,X
    LDA(4, ABS_IND_X_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(BE) // LDX $nnnn,Y
    LDX(4, ABS_IND_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(BF) // LAX $nnnn,Y
    LAX(4, ABS_IND_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(C0) // CPY #$nn
    CPY(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(C1) // CMP ($nn,X)
    CMP(6, INDIR_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(C3) // DCP ($nn,X)
    DCP(8, INDIR_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(C4) // CPY $nn
    CPY(3, ZERO_PAGE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(C5) // CMP $nn
    CMP(3, ZERO_PAGE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(C6) // DEC $nn
    _DEC(5, ZERO_PAGE, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(C7) // DCP $nn
    DCP(5, ZERO_PAGE, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(C8) // INY
    INY();
    OPCODE_END

    OPCODE_BEGIN(C9) // CMP #$nn
    CMP(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(CA) // DEX
    DEX();
    OPCODE_END

    OPCODE_BEGIN(CB) // SBX #$nn
    SBX(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(CC) // CPY $nnnn
    CPY(4, ABSOLUTE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(CD) // CMP $nnnn
    CMP(4, ABSOLUTE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(CE) // DEC $nnnn
    _DEC(6, ABSOLUTE, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(CF) // DCP $nnnn
    DCP(6, ABSOLUTE, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(D0) // BNE $nnnn
    BNE();
    OPCODE_END

    OPCODE_BEGIN(D1) // CMP ($nn),Y
    CMP(5, INDIR_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(D3) // DCP ($nn),Y
    DCP(8, INDIR_Y, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(D5) // CMP $nn,X
    CMP(4, ZP_IND_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(D6) // DEC $nn,X
    _DEC(6, ZP_IND_X, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(D7) // DCP $nn,X
    DCP(6, ZP_IND_X, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(D8) // CLD
    CLD();
    OPCODE_END

    OPCODE_BEGIN(D9) // CMP $nnnn,Y
    CMP(4, ABS_IND_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(DB) // DCP $nnnn,Y
    DCP(7, ABS_IND_Y, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(DD) // CMP $nnnn,X
    CMP(4, ABS_IND_X_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(DE) // DEC $nnnn,X
    _DEC(7, ABS_IND_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(DF) // DCP $nnnn,X
    DCP(7, ABS_IND_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(E0) // CPX #$nn
    CPX(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(E1) // SBC ($nn,X)
    SBC(6, INDIR_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(E3) // ISB ($nn,X)
    ISB(8, INDIR_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(E4) // CPX $nn
    CPX(3, ZERO_PAGE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(E5) // SBC $nn
    SBC(3, ZERO_PAGE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(E6) // INC $nn
    INC(5, ZERO_PAGE, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(E7) // ISB $nn
    ISB(5, ZERO_PAGE, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(E8) // INX
    INX();
    OPCODE_END

    OPCODE_BEGIN(E9) // SBC #$nn
    OPCODE_BEGIN(EB) // USBC #$nn
    SBC(2, IMMEDIATE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(EA) // NOP
    NOP_();
    OPCODE_END

    OPCODE_BEGIN(EC) // CPX $nnnn
    CPX(4, ABSOLUTE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(ED) // SBC $nnnn
    SBC(4, ABSOLUTE_BYTE);
    OPCODE_END

    OPCODE_BEGIN(EE) // INC $nnnn
    INC(6, ABSOLUTE, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(EF) // ISB $nnnn
    ISB(6, ABSOLUTE, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(F0) // BEQ $nnnn
    BEQ();
    OPCODE_END

    OPCODE_BEGIN(F1) // SBC ($nn),Y
    SBC(5, INDIR_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(F3) // ISB ($nn),Y
    ISB(8, INDIR_Y, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(F5) // SBC $nn,X
    SBC(4, ZP_IND_X_BYTE);
    OPCODE_END

    OPCODE_BEGIN(F6) // INC $nn,X
    INC(6, ZP_IND_X, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(F7) // ISB $nn,X
    ISB(6, ZP_IND_X, ZP_WRITEBYTE, baddr);
    OPCODE_END

    OPCODE_BEGIN(F8) // SED
    SED();
    OPCODE_END

    OPCODE_BEGIN(F9) // SBC $nnnn,Y
    SBC(4, ABS_IND_Y_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(FB) // ISB $nnnn,Y
    ISB(7, ABS_IND_Y, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(FD) // SBC $nnnn,X
    SBC(4, ABS_IND_X_BYTE_READ);
    OPCODE_END

    OPCODE_BEGIN(FE) // INC $nnnn,X
    INC(7, ABS_IND_X, mem_writebyte, addr);
    OPCODE_END

    OPCODE_BEGIN(FF) // ISB $nnnn,X
    ISB(7, ABS_IND_X, mem_writebyte, addr);
    OPCODE_END
#ifdef NES6502_JUMPTABLE
end_execute:
#else  // !NES6502_JUMPTABLE
        }
    }
#endif // !NES6502_JUMPTABLE
    STORE_LOCAL_REGS();
    return (cpu.total_cycles - old_cycles);
}

void nes6502_reset(void) // Issue a CPU Reset
{
    cpu.p_reg = Z_FLAG | R_FLAG | I_FLAG;     // Reserved bit always 1
    cpu.int_pending = 0;                      // No pending interrupts
    cpu.int_latency = 0;                      // No latent interrupts
    cpu.pc_reg = bank_readword(RESET_VECTOR); // Fetch reset vector
    cpu.burn_cycles = RESET_CYCLES;
    cpu.jammed = false;
}

#define  DECLARE_LOCAL_REGS uint32_t PC; uint8_t A, X, Y, S; uint8_t n_flag, v_flag, b_flag; uint8_t d_flag, i_flag, z_flag, c_flag;

void nes6502_nmi(void)
{ // Non-maskable interrupt
    DECLARE_LOCAL_REGS

    if (false == cpu.jammed)
    {
        GET_GLOBAL_REGS();
        NMI_PROC();
        cpu.burn_cycles += INT_CYCLES;
        STORE_LOCAL_REGS();
    }
}

void nes6502_irq(void)
{ // Interrupt request
    DECLARE_LOCAL_REGS
    if (false == cpu.jammed)
    {
        GET_GLOBAL_REGS();
        if (0 == i_flag)
        {
            IRQ_PROC();
            cpu.burn_cycles += INT_CYCLES;
        }
        else
        {
            cpu.int_pending = 1;
        }
        STORE_LOCAL_REGS();
    }
}

void nes6502_irq_clear(void)
{
   cpu.p_reg &= I_FLAG;
}

void nes6502_burn(int cycles)
{ // Set dead cycle period
    cpu.burn_cycles += cycles;
}

void nes6502_release(void)
{ // Release our timeslice
    remaining_cycles = 0;
}
//--------------------------------------------------------------------------------
// NES.C part
void nes_setfiq(uint8_t value)
{
    NESmachine->fiq_state = value;
    NESmachine->fiq_cycles = (int)NES_FIQ_PERIOD;
}
void nes_nmi(void)
{
    nes6502_nmi();
}
//--------------------------------------------------------------------------------
// PPU.C:

// PPU access
#define PPU_MEM(x) NESmachine->ppu->page[(x) >> 10][(x)]

// Background (color 0) and solid sprite pixel flags
#define BG_TRANS 0x80
#define SP_PIXEL 0x40
#define BG_CLEAR(V) ((V)&BG_TRANS)
#define BG_SOLID(V) (0 == BG_CLEAR(V))
#define SP_CLEAR(V) (0 == ((V)&SP_PIXEL))

// Full BG color
#define FULLBG (NESmachine->ppu->palette[0] | BG_TRANS)

void ppu_displaysprites(bool display)
{
    NESmachine->ppu->drawsprites = display;
}

void ppu_getcontext(ppu_t *dest_ppu)
{
    int nametab[4];

    //*dest_ppu = ppu;
    dest_ppu=NESmachine->ppu;

    // we can't just copy contexts here, because more than likely,
    // the top 8 pages of the ppu are pointing to internal PPU memory,
    // which means we need to recalculate the page pointers.
    // TODO: we can either get rid of the page pointing in the code,
    // or add more robust checks to make sure that pages 8-15 are
    // definitely pointing to internal PPU RAM, not just something
    // that some crazy mapper paged in.

  nametab[0] = (NESmachine->ppu->page[8] - NESmachine->ppu->nametab + 0x2000) >> 10;
  nametab[1] = (NESmachine->ppu->page[9] - NESmachine->ppu->nametab + 0x2400) >> 10;
  nametab[2] = (NESmachine->ppu->page[10] - NESmachine->ppu->nametab + 0x2800) >> 10;
  nametab[3] = (NESmachine->ppu->page[11] - NESmachine->ppu->nametab + 0x2C00) >> 10;

    dest_ppu->page[8] = dest_ppu->nametab + (nametab[0] << 10) - 0x2000;
    dest_ppu->page[9] = dest_ppu->nametab + (nametab[1] << 10) - 0x2400;
    dest_ppu->page[10] = dest_ppu->nametab + (nametab[2] << 10) - 0x2800;
    dest_ppu->page[11] = dest_ppu->nametab + (nametab[3] << 10) - 0x2C00;
    dest_ppu->page[12] = dest_ppu->page[8] - 0x1000;
    dest_ppu->page[13] = dest_ppu->page[9] - 0x1000;
    dest_ppu->page[14] = dest_ppu->page[10] - 0x1000;
    dest_ppu->page[15] = dest_ppu->page[11] - 0x1000;
}

void ppu_setcontext(ppu_t *src_ppu)
{
   int nametab[4];
   //ASSERT(src_ppu);
   //ppu = *src_ppu;

   /* we can't just copy contexts here, because more than likely,
   ** the top 8 pages of the ppu are pointing to internal PPU memory,
   ** which means we need to recalculate the page pointers.
   ** TODO: we can either get rid of the page pointing in the code,
   ** or add more robust checks to make sure that pages 8-15 are
   ** definitely pointing to internal PPU RAM, not just something
   ** that some crazy mapper paged in.
   */
   nametab[0] = (src_ppu->page[8] - src_ppu->nametab + 0x2000) >> 10;
   nametab[1] = (src_ppu->page[9] - src_ppu->nametab + 0x2400) >> 10;
   nametab[2] = (src_ppu->page[10] - src_ppu->nametab + 0x2800) >> 10;
   nametab[3] = (src_ppu->page[11] - src_ppu->nametab + 0x2C00) >> 10;

   NESmachine->ppu->page[8] = NESmachine->ppu->nametab + (nametab[0] << 10) - 0x2000;
  NESmachine->ppu->page[9] = NESmachine->ppu->nametab + (nametab[1] << 10) - 0x2400;
  NESmachine->ppu->page[10] = NESmachine->ppu->nametab + (nametab[2] << 10) - 0x2800;
  NESmachine->ppu->page[11] = NESmachine->ppu->nametab + (nametab[3] << 10) - 0x2C00;
  NESmachine->ppu->page[12] = NESmachine->ppu->page[8] - 0x1000;
  NESmachine->ppu->page[13] = NESmachine->ppu->page[9] - 0x1000;
  NESmachine->ppu->page[14] = NESmachine->ppu->page[10] - 0x1000;
  NESmachine->ppu->page[15] = NESmachine->ppu->page[11] - 0x1000;
}

ppu_t *temp;

ppu_t *ppu_create(void)
{
    static bool pal_generated = false;


    if (NULL == temp)
        temp = (ppu_t *)heap_caps_malloc(sizeof(ppu_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == temp)
        return NULL;

    memset(temp, 0, sizeof(ppu_t));

    temp->latchfunc = NULL;
    temp->vromswitch = NULL;
    temp->vram_present = false;
    temp->drawsprites = true;
    //与原版有区别
    // TODO: probably a better way to do this...
    if (false == pal_generated)
    {
        /// pal_generate();
        pal_generated = true;
    }

    /// ppu_setdefaultpal(temp);

    return temp;
}

void ppu_setpage(int size, int page_num, uint8_t *location)
{
    // deliberately fall through
    switch (size)
  {
    case 8:
      NESmachine->ppu->page[page_num++] = location;
      NESmachine->ppu->page[page_num++] = location;
      NESmachine->ppu->page[page_num++] = location;
      NESmachine->ppu->page[page_num++] = location;
    case 4:
      NESmachine->ppu->page[page_num++] = location;
      NESmachine->ppu->page[page_num++] = location;
    case 2:
      NESmachine->ppu->page[page_num++] = location;
    case 1:
      NESmachine->ppu->page[page_num++] = location;
      break;
  }
}

// make sure $3000-$3F00 mirrors $2000-$2F00
void ppu_mirrorhipages(void)
{
  NESmachine->ppu->page[12] = NESmachine->ppu->page[8] - 0x1000;
  NESmachine->ppu->page[13] = NESmachine->ppu->page[9] - 0x1000;
  NESmachine->ppu->page[14] = NESmachine->ppu->page[10] - 0x1000;
  NESmachine->ppu->page[15] = NESmachine->ppu->page[11] - 0x1000;
}

void ppu_mirror(int nt1, int nt2, int nt3, int nt4)
{
  NESmachine->ppu->page[8] = NESmachine->ppu->nametab + (nt1 << 10) - 0x2000;
  NESmachine->ppu->page[9] = NESmachine->ppu->nametab + (nt2 << 10) - 0x2400;
  NESmachine->ppu->page[10] = NESmachine->ppu->nametab + (nt3 << 10) - 0x2800;
  NESmachine->ppu->page[11] = NESmachine->ppu->nametab + (nt4 << 10) - 0x2C00;
  NESmachine->ppu->page[12] = NESmachine->ppu->page[8] - 0x1000;
  NESmachine->ppu->page[13] = NESmachine->ppu->page[9] - 0x1000;
  NESmachine->ppu->page[14] = NESmachine->ppu->page[10] - 0x1000;
  NESmachine->ppu->page[15] = NESmachine->ppu->page[11] - 0x1000;
}

// bleh, for snss
uint8_t *ppu_getpage(int page) {
  return NESmachine->ppu->page[page];
}

void mem_trash(uint8_t *buffer, int length)
{
    int i;

    for (i = 0; i < length; i++)
        buffer[i] = (uint8_t)rand();
}

// reset state of ppu
void ppu_reset()
{
    /// if (HARD_RESET == reset_type)
    mem_trash(NESmachine->ppu->oam, 256);
    /// memset(ppu.oam, 0, 256);

    NESmachine->ppu->ctrl0 = 0;
    NESmachine->ppu->ctrl1 = PPU_CTRL1F_OBJON | PPU_CTRL1F_BGON;
    NESmachine->ppu->stat = 0;
    NESmachine->ppu->flipflop = 0;
    NESmachine->ppu->vaddr_latch = 0x2000;
    NESmachine->ppu->vaddr = NESmachine->ppu->vaddr_latch = 0x2000;

    NESmachine->ppu->oam_addr = 0;
    NESmachine->ppu->tile_xofs = 0;

    NESmachine->ppu->latch = 0;
    NESmachine->ppu->vram_accessible = true;
}

// we render a scanline of graphics first so we know exactly
// where the sprite 0 strike is going to occur (in terms of
// cpu cycles), using the relation that 3 pixels == 1 cpu cycle
void ppu_setstrike(int x_loc)
{
    if (false == NESmachine->ppu->strikeflag)
  {
    NESmachine->ppu->strikeflag = true;

    // 3 pixels per cpu cycle
    NESmachine->ppu->strike_cycle = nes6502_getcycles(false) + (x_loc / 3);
  }
}

void ppu_oamdma(uint8_t value)
{
    uint32_t cpu_address;
    uint8_t oam_loc;

    cpu_address = (uint32_t)(value << 8);

    // Sprite DMA starts at the current SPRRAM address
    oam_loc = NESmachine->ppu->oam_addr;
  do {
    NESmachine->ppu->oam[oam_loc++] = nes6502_getbyte(cpu_address++);
  } while (oam_loc != NESmachine->ppu->oam_addr);

    // TODO: enough with houdini
    cpu_address -= 256;
    // Odd address in $2003
    if ((NESmachine->ppu->oam_addr >> 2) & 1) {
    for (oam_loc = 4; oam_loc < 8; oam_loc++) NESmachine->ppu->oam[oam_loc] = nes6502_getbyte(cpu_address++);
    cpu_address += 248;
    for (oam_loc = 0; oam_loc < 4; oam_loc++) NESmachine->ppu->oam[oam_loc] = nes6502_getbyte(cpu_address++);
  } else { // Even address in $2003
    for (oam_loc = 0; oam_loc < 8; oam_loc++)
      NESmachine->ppu->oam[oam_loc] = nes6502_getbyte(cpu_address++);
  }

    // make the CPU spin for DMA cycles
    nes6502_burn(513);
    nes6502_release();
}

void ppu_writehigh(uint32_t address, uint8_t value)
{
    switch (address)
    {
    case PPU_OAMDMA:
        ppu_oamdma(value);
        break;

    case PPU_JOY0:
        // VS system VROM switching - bleh!
        if (NESmachine->ppu->vromswitch) NESmachine->ppu->vromswitch(value);

        // see if we need to strobe them joypads
        value &= 1;
        if (0 == value && NESmachine->ppu->strobe) input_strobe();
      NESmachine->ppu->strobe = value;
        break;

    case PPU_JOY1: // frame IRQ control
        nes_setfiq(value);
        break;
    default:
        break;
    }
}

uint8_t ppu_readhigh(uint32_t address)
{
    uint8_t value;
    switch (address)
    {
    case PPU_JOY0:
        value = get_pad0();
        break;
    case PPU_JOY1:
    ///      value = 0;
        value = get_pad1();
        break;
    default:
        value = 0xFF;
        break;
    }
    return value;
}

// Read from $2000-$2007
uint8_t ppu_read(uint32_t address)
{
    uint8_t value;
    // handle mirrored reads up to $3FFF
    switch (address & 0x2007)
    {
    case PPU_STAT:
        value = (NESmachine->ppu->stat & 0xE0) | (NESmachine->ppu->latch & 0x1F);

      if (NESmachine->ppu->strikeflag) {
        if (nes6502_getcycles(false) >= NESmachine->ppu->strike_cycle)
          value |= PPU_STATF_STRIKE;
      }

        // clear both vblank flag and vram address flipflop
        NESmachine->ppu->stat &= ~PPU_STATF_VBLANK;
      NESmachine->ppu->flipflop = 0;
        break;

    case PPU_VDATA:
        // buffered VRAM reads
        value = NESmachine->ppu->latch = NESmachine->ppu->vdata_latch;

      // VRAM only accessible during VBL
      if ((NESmachine->ppu->bg_on || NESmachine->ppu->obj_on) && !NESmachine->ppu->vram_accessible) {
        NESmachine->ppu->vdata_latch = 0xFF;
        ///nes_log_printf("VRAM read at $%04X, scanline %d\n", NESmachine->ppu->vaddr, nes_getcontextptr()->scanline);
      } else {
        uint32_t addr = NESmachine->ppu->vaddr;
        if (addr >= 0x3000) addr -= 0x1000;
        NESmachine->ppu->vdata_latch = PPU_MEM(addr);
      }
      NESmachine->ppu->vaddr += NESmachine->ppu->vaddr_inc;
      NESmachine->ppu->vaddr &= 0x3FFF;
        break;

    case PPU_OAMDATA:
    case PPU_CTRL0:
    case PPU_CTRL1:
    case PPU_OAMADDR:
    case PPU_SCROLL:
    case PPU_VADDR:
    default:
        value = NESmachine->ppu->latch;
        break;
    }

    return value;
}

// Write to $2000-$2007
void ppu_write(uint32_t address, uint8_t value) {
  // write goes into ppu latch...
  NESmachine->ppu->latch = value;

  switch (address & 0x2007)
  {
    case PPU_CTRL0:
      NESmachine->ppu->ctrl0 = value;
      NESmachine->ppu->obj_height = (value & PPU_CTRL0F_OBJ16) ? 16 : 8;
      NESmachine->ppu->bg_base = (value & PPU_CTRL0F_BGADDR) ? 0x1000 : 0;
      NESmachine->ppu->obj_base = (value & PPU_CTRL0F_OBJADDR) ? 0x1000 : 0;
      NESmachine->ppu->vaddr_inc = (value & PPU_CTRL0F_ADDRINC) ? 32 : 1;
      NESmachine->ppu->tile_nametab = value & PPU_CTRL0F_NAMETAB;

      // Mask out bits 10 & 11 in the ppu latch
      NESmachine->ppu->vaddr_latch &= ~0x0C00;
      NESmachine->ppu->vaddr_latch |= ((value & 3) << 10);
      break;
    case PPU_CTRL1:
      NESmachine->ppu->ctrl1 = value;
      NESmachine->ppu->obj_on = (value & PPU_CTRL1F_OBJON) ? true : false;
      NESmachine->ppu->bg_on = (value & PPU_CTRL1F_BGON) ? true : false;
      NESmachine->ppu->obj_mask = (value & PPU_CTRL1F_OBJMASK) ? false : true;
      NESmachine->ppu->bg_mask = (value & PPU_CTRL1F_BGMASK) ? false : true;
      break;
    case PPU_OAMADDR:
      NESmachine->ppu->oam_addr = value;
      break;
    case PPU_OAMDATA:
      NESmachine->ppu->oam[NESmachine->ppu->oam_addr++] = value;
      break;
    case PPU_SCROLL:
      if (0 == NESmachine->ppu->flipflop) {
        // Mask out bits 4 - 0 in the ppu latch
        NESmachine->ppu->vaddr_latch &= ~0x001F;
        NESmachine->ppu->vaddr_latch |= (value >> 3);    // Tile number
        NESmachine->ppu->tile_xofs = (value & 7);  // Tile offset (0-7 pix)
      } else {
        // Mask out bits 14-12 and 9-5 in the ppu latch
        NESmachine->ppu->vaddr_latch &= ~0x73E0;
        NESmachine->ppu->vaddr_latch |= ((value & 0xF8) << 2);   // Tile number
        NESmachine->ppu->vaddr_latch |= ((value & 7) << 12);     // Tile offset (0-7 pix)
      }
      NESmachine->ppu->flipflop ^= 1;
      break;
    case PPU_VADDR:
      if (0 == NESmachine->ppu->flipflop) {
        // Mask out bits 15-8 in ppu latch
        NESmachine->ppu->vaddr_latch &= ~0xFF00;
        NESmachine->ppu->vaddr_latch |= ((value & 0x3F) << 8);
      } else {
        // Mask out bits 7-0 in ppu latch
        NESmachine->ppu->vaddr_latch &= ~0x00FF;
        NESmachine->ppu->vaddr_latch |= value;
        NESmachine->ppu->vaddr = NESmachine->ppu->vaddr_latch;
      }
      NESmachine->ppu->flipflop ^= 1;
      break;
    case PPU_VDATA:
      if (NESmachine->ppu->vaddr < 0x3F00) {
        // VRAM only accessible during scanlines 241-260
        if ((NESmachine->ppu->bg_on || NESmachine->ppu->obj_on) && !NESmachine->ppu->vram_accessible) {
          ///nes_log_printf("VRAM write to $%04X, scanline %d\n", NESmachine->ppu->vaddr, nes_getcontextptr()->scanline);
          PPU_MEM(NESmachine->ppu->vaddr) = 0xFF; // corrupt
        } else {
          uint32_t addr = NESmachine->ppu->vaddr;
          if (false == NESmachine->ppu->vram_present && addr >= 0x3000) NESmachine->ppu->vaddr -= 0x1000;
          PPU_MEM(addr) = value;
        }
      } else {
        if (0 == (NESmachine->ppu->vaddr & 0x0F)) {
          int i;
          for (i = 0; i < 8; i ++) NESmachine->ppu->palette[i << 2] = (value & 0x3F) | BG_TRANS;
        } else if (NESmachine->ppu->vaddr & 3) {
          NESmachine->ppu->palette[NESmachine->ppu->vaddr & 0x1F] = value & 0x3F;
        }
      }
      NESmachine->ppu->vaddr += NESmachine->ppu->vaddr_inc;
      NESmachine->ppu->vaddr &= 0x3FFF;
      break;
    default:
      break;
  }
}

//与原版不同，原版这里有build调色板
// rendering routines
static inline void draw_bgtile(uint8_t *surface, uint8_t pat1, uint8_t pat2, const uint8_t *colors)
{
    uint32_t pattern = ((pat2 & 0xAA) << 8) | ((pat2 & 0x55) << 1) | ((pat1 & 0xAA) << 7) | (pat1 & 0x55);
    *surface++ = colors[(pattern >> 14) & 3];
    *surface++ = colors[(pattern >> 6) & 3];
    *surface++ = colors[(pattern >> 12) & 3];
    *surface++ = colors[(pattern >> 4) & 3];
    *surface++ = colors[(pattern >> 10) & 3];
    *surface++ = colors[(pattern >> 2) & 3];
    *surface++ = colors[(pattern >> 8) & 3];
    *surface = colors[pattern & 3];
}

inline int draw_oamtile(uint8_t *surface, uint8_t attrib, uint8_t pat1, uint8_t pat2, const uint8_t *col_tbl, bool check_strike)
{
    int strike_pixel = -1;
    uint32_t color = ((pat2 & 0xAA) << 8) | ((pat2 & 0x55) << 1) | ((pat1 & 0xAA) << 7) | (pat1 & 0x55);    
    // sprite is not 100% transparent
    if (color)
    {
        uint8_t colors[8];
        // swap pixels around if our tile is flipped
        if (0 == (attrib & OAMF_HFLIP))
        {
            colors[0] = (color >> 14) & 3;
            colors[1] = (color >> 6) & 3;
            colors[2] = (color >> 12) & 3;
            colors[3] = (color >> 4) & 3;
            colors[4] = (color >> 10) & 3;
            colors[5] = (color >> 2) & 3;
            colors[6] = (color >> 8) & 3;
            colors[7] = color & 3;
        }
        else
        {
            colors[7] = (color >> 14) & 3;
            colors[6] = (color >> 6) & 3;
            colors[5] = (color >> 12) & 3;
            colors[4] = (color >> 4) & 3;
            colors[3] = (color >> 10) & 3;
            colors[2] = (color >> 2) & 3;
            colors[1] = (color >> 8) & 3;
            colors[0] = color & 3;
        }

        // check for solid sprite pixel overlapping solid bg pixel
        if (check_strike)
        {
            if (colors[0] && BG_SOLID(surface[0]))
                strike_pixel = 0;
            else if (colors[1] && BG_SOLID(surface[1]))
                strike_pixel = 1;
            else if (colors[2] && BG_SOLID(surface[2]))
                strike_pixel = 2;
            else if (colors[3] && BG_SOLID(surface[3]))
                strike_pixel = 3;
            else if (colors[4] && BG_SOLID(surface[4]))
                strike_pixel = 4;
            else if (colors[5] && BG_SOLID(surface[5]))
                strike_pixel = 5;
            else if (colors[6] && BG_SOLID(surface[6]))
                strike_pixel = 6;
            else if (colors[7] && BG_SOLID(surface[7]))
                strike_pixel = 7;
        }

        // draw the character
        if (attrib & OAMF_BEHIND)
        {
            if (colors[0])
                surface[0] = SP_PIXEL | (BG_CLEAR(surface[0]) ? col_tbl[colors[0]] : surface[0]);
            if (colors[1])
                surface[1] = SP_PIXEL | (BG_CLEAR(surface[1]) ? col_tbl[colors[1]] : surface[1]);
            if (colors[2])
                surface[2] = SP_PIXEL | (BG_CLEAR(surface[2]) ? col_tbl[colors[2]] : surface[2]);
            if (colors[3])
                surface[3] = SP_PIXEL | (BG_CLEAR(surface[3]) ? col_tbl[colors[3]] : surface[3]);
            if (colors[4])
                surface[4] = SP_PIXEL | (BG_CLEAR(surface[4]) ? col_tbl[colors[4]] : surface[4]);
            if (colors[5])
                surface[5] = SP_PIXEL | (BG_CLEAR(surface[5]) ? col_tbl[colors[5]] : surface[5]);
            if (colors[6])
                surface[6] = SP_PIXEL | (BG_CLEAR(surface[6]) ? col_tbl[colors[6]] : surface[6]);
            if (colors[7])
                surface[7] = SP_PIXEL | (BG_CLEAR(surface[7]) ? col_tbl[colors[7]] : surface[7]);
        }
        else
        {
            if (colors[0] && SP_CLEAR(surface[0]))
                surface[0] = SP_PIXEL | col_tbl[colors[0]];
            if (colors[1] && SP_CLEAR(surface[1]))
                surface[1] = SP_PIXEL | col_tbl[colors[1]];
            if (colors[2] && SP_CLEAR(surface[2]))
                surface[2] = SP_PIXEL | col_tbl[colors[2]];
            if (colors[3] && SP_CLEAR(surface[3]))
                surface[3] = SP_PIXEL | col_tbl[colors[3]];
            if (colors[4] && SP_CLEAR(surface[4]))
                surface[4] = SP_PIXEL | col_tbl[colors[4]];
            if (colors[5] && SP_CLEAR(surface[5]))
                surface[5] = SP_PIXEL | col_tbl[colors[5]];
            if (colors[6] && SP_CLEAR(surface[6]))
                surface[6] = SP_PIXEL | col_tbl[colors[6]];
            if (colors[7] && SP_CLEAR(surface[7]))
                surface[7] = SP_PIXEL | col_tbl[colors[7]];
        }
    }

    return strike_pixel;
}

void ppu_renderbg(uint8_t *vidbuf)
{
    uint8_t *bmp_ptr, *data_ptr, *tile_ptr, *attrib_ptr;
    uint32_t refresh_vaddr, bg_offset, attrib_base;
    int tile_count;
    uint8_t tile_index, x_tile, y_tile;
    uint8_t col_high, attrib, attrib_shift;

    // draw a line of transparent background color if bg is disabled
    if (false == NESmachine->ppu->bg_on)
    {
        memset(vidbuf, FULLBG, NES_SCREEN_WIDTH);
        return;
    }

    bmp_ptr = vidbuf - NESmachine->ppu->tile_xofs; // scroll x
  refresh_vaddr = 0x2000 + (NESmachine->ppu->vaddr & 0x0FE0); // mask out x tile
  x_tile = NESmachine->ppu->vaddr & 0x1F;
  y_tile = (NESmachine->ppu->vaddr >> 5) & 0x1F; // to simplify calculations
  bg_offset = ((NESmachine->ppu->vaddr >> 12) & 7) + NESmachine->ppu->bg_base; // offset in y tile

    // calculate initial values
    tile_ptr = &PPU_MEM(refresh_vaddr + x_tile); // pointer to tile index
    attrib_base = (refresh_vaddr & 0x2C00) + 0x3C0 + ((y_tile & 0x1C) << 1);
    attrib_ptr = &PPU_MEM(attrib_base + (x_tile >> 2));
    attrib = *attrib_ptr++;
    attrib_shift = (x_tile & 2) + ((y_tile & 2) << 1);
    col_high = ((attrib >> attrib_shift) & 3) << 2;    
    // ppu fetches 33 tiles
    tile_count = 33;
    while (tile_count--)
    {
        // Tile number from nametable
        tile_index = *tile_ptr++;
        data_ptr = &PPU_MEM(bg_offset + (tile_index << 4));
        
        // Handle $FD/$FE tile VROM switching (PunchOut)
        if (NESmachine->ppu->latchfunc) NESmachine->ppu->latchfunc(NESmachine->ppu->bg_base, tile_index);

    ///if (tile_count > 1)
      draw_bgtile(bmp_ptr, data_ptr[0], data_ptr[8], NESmachine->ppu->palette + col_high);

    bmp_ptr += 8;

        x_tile++;

        if (0 == (x_tile & 1))
        { // check every 2 tiles
            if (0 == (x_tile & 3))
            { // check every 4 tiles
                if (32 == x_tile)
                { // check every 32 tiles
                    x_tile = 0;
                    refresh_vaddr ^= (1 << 10); // switch nametable
                    attrib_base ^= (1 << 10);
                    // recalculate pointers
                    tile_ptr = &PPU_MEM(refresh_vaddr);
                    attrib_ptr = &PPU_MEM(attrib_base);
                }
                // Get the attribute byte
                attrib = *attrib_ptr++;
            }
            attrib_shift ^= 2;
            col_high = ((attrib >> attrib_shift) & 3) << 2;
        }
    }

    // Blank left hand column if need be
    if (NESmachine->ppu->bg_mask)
    {
        uint32_t *buf_ptr = (uint32_t *)vidbuf;
        uint32_t bg_clear = FULLBG | FULLBG << 8 | FULLBG << 16 | FULLBG << 24;
        ((uint32_t *)buf_ptr)[0] = bg_clear;
        ((uint32_t *)buf_ptr)[1] = bg_clear;
    }
}

// OAM entry
typedef struct obj_s
{
    uint8_t y_loc;
    uint8_t tile;
    uint8_t atr;
    uint8_t x_loc;
} obj_t;

// TODO: fetch valid OAM a scanline before, like the Real Thing
void ppu_renderoam(uint8_t *vidbuf, int scanline)
{
    uint8_t *buf_ptr;
    uint32_t vram_offset, savecol[2] = {0};
    int sprite_num, spritecount;
    obj_t *sprite_ptr;
    uint8_t sprite_height;
    if (false == NESmachine->ppu->obj_on)
        return;
    // Get our buffer pointer
    buf_ptr = vidbuf;
    // Save left hand column?
    if (NESmachine->ppu->obj_mask)
    {
        savecol[0] = ((uint32_t *)buf_ptr)[0];
        savecol[1] = ((uint32_t *)buf_ptr)[1];
    }

    sprite_height = NESmachine->ppu->obj_height;
  vram_offset = NESmachine->ppu->obj_base;
    spritecount = 0;

    sprite_ptr = (obj_t *) NESmachine->ppu->oam;
    for (sprite_num = 0; sprite_num < 64; sprite_num++, sprite_ptr++)
    {
        uint8_t *data_ptr, *bmp_ptr;
        uint32_t vram_adr;
        int y_offset;
        uint8_t tile_index, attrib, col_high;
        uint8_t sprite_y, sprite_x;
        bool check_strike;
        int strike_pixel;
        sprite_y = sprite_ptr->y_loc + 1;
        // Check to see if sprite is out of range
        if ((sprite_y > scanline) || (sprite_y <= (scanline - sprite_height)) || (0 == sprite_y) || (sprite_y >= 240))
            continue;

        sprite_x = sprite_ptr->x_loc;
        tile_index = sprite_ptr->tile;
        attrib = sprite_ptr->atr;
        bmp_ptr = buf_ptr + sprite_x;
        // Handle $FD/$FE tile VROM switching (PunchOut)
    if (NESmachine->ppu->latchfunc) NESmachine->ppu->latchfunc(vram_offset, tile_index);
    // Get upper two bits of color
    col_high = ((attrib & 3) << 2);
    // 8x16 even sprites use $0000, odd use $1000
    if (16 == NESmachine->ppu->obj_height) vram_adr = ((tile_index & 1) << 12) | ((tile_index & 0xFE) << 4);
    else vram_adr = vram_offset + (tile_index << 4);
    // Get the address of the tile
    data_ptr = &PPU_MEM(vram_adr);
    // Calculate offset (line within the sprite)
    y_offset = scanline - sprite_y;
    if (y_offset > 7) y_offset += 8;
    // Account for vertical flippage
    if (attrib & OAMF_VFLIP) {
      if (16 == NESmachine->ppu->obj_height) y_offset -= 23;
      else y_offset -= 7;
      data_ptr -= y_offset;
    } else {
      data_ptr += y_offset;
    }

    // if we're on sprite 0 and sprite 0 strike flag isn't set, check for a strike
    check_strike = (0 == sprite_num) && (false == NESmachine->ppu->strikeflag);
    strike_pixel = draw_oamtile(bmp_ptr, attrib, data_ptr[0], data_ptr[8], NESmachine->ppu->palette + 16 + col_high, check_strike);
    if (strike_pixel >= 0) ppu_setstrike(strike_pixel);

    // maximum of 8 sprites per scanline
    if (++spritecount == PPU_MAXSPRITE) {
      NESmachine->ppu->stat |= PPU_STATF_MAXSPRITE;
      break;
    }
  }
  // Restore lefthand column
  if (NESmachine->ppu->obj_mask) {
    ((uint32_t *) buf_ptr)[0] = savecol[0];
    ((uint32_t *) buf_ptr)[1] = savecol[1];
  }
}

// Fake rendering a line - This is needed for sprite 0 hits when we're skipping drawing a frame
void ppu_fakeoam(int scanline)
{
    uint8_t *data_ptr;
    obj_t *sprite_ptr;
    uint32_t vram_adr, color;
    int y_offset;
    uint8_t pat1, pat2;
    uint8_t tile_index, attrib;
    uint8_t sprite_height, sprite_y, sprite_x;

    // we don't need to be here if strike flag is set
  if (false == NESmachine->ppu->obj_on || NESmachine->ppu->strikeflag) return;

  sprite_height = NESmachine->ppu->obj_height;
  sprite_ptr = (obj_t *) NESmachine->ppu->oam;
  sprite_y = sprite_ptr->y_loc + 1;
  // Check to see if sprite is out of range
  if ((sprite_y > scanline) || (sprite_y <= (scanline - sprite_height)) || (0 == sprite_y) || (sprite_y > 240)) return;
  sprite_x = sprite_ptr->x_loc;
  tile_index = sprite_ptr->tile;
  attrib = sprite_ptr->atr;
  // 8x16 even sprites use $0000, odd use $1000
  if (16 == NESmachine->ppu->obj_height) vram_adr = ((tile_index & 1) << 12) | ((tile_index & 0xFE) << 4);
  else vram_adr = NESmachine->ppu->obj_base + (tile_index << 4);
  data_ptr = &PPU_MEM(vram_adr);
  // Calculate offset (line within the sprite)
  y_offset = scanline - sprite_y;
  if (y_offset > 7) y_offset += 8;

  // Account for vertical flippage
  if (attrib & OAMF_VFLIP) {
    if (16 == NESmachine->ppu->obj_height) 
        y_offset -= 23;
    else 
        y_offset -= 7;
    data_ptr -= y_offset;
    }
    else
    {
        data_ptr += y_offset;
    }

    // check for a solid sprite 0 pixel
    pat1 = data_ptr[0];
    pat2 = data_ptr[8];
    color = ((pat2 & 0xAA) << 8) | ((pat2 & 0x55) << 1) | ((pat1 & 0xAA) << 7) | (pat1 & 0x55);

    if (color)
    {
        uint8_t colors[8];

        // buckle up, it's going to get ugly...
        if (0 == (attrib & OAMF_HFLIP))
        {
            colors[0] = (color >> 14) & 3;
            colors[1] = (color >> 6) & 3;
            colors[2] = (color >> 12) & 3;
            colors[3] = (color >> 4) & 3;
            colors[4] = (color >> 10) & 3;
            colors[5] = (color >> 2) & 3;
            colors[6] = (color >> 8) & 3;
            colors[7] = color & 3;
        }
        else
        {
            colors[7] = (color >> 14) & 3;
            colors[6] = (color >> 6) & 3;
            colors[5] = (color >> 12) & 3;
            colors[4] = (color >> 4) & 3;
            colors[3] = (color >> 10) & 3;
            colors[2] = (color >> 2) & 3;
            colors[1] = (color >> 8) & 3;
            colors[0] = color & 3;
        }

        if (colors[0])
            ppu_setstrike(sprite_x + 0);
        else if (colors[1])
            ppu_setstrike(sprite_x + 1);
        else if (colors[2])
            ppu_setstrike(sprite_x + 2);
        else if (colors[3])
            ppu_setstrike(sprite_x + 3);
        else if (colors[4])
            ppu_setstrike(sprite_x + 4);
        else if (colors[5])
            ppu_setstrike(sprite_x + 5);
        else if (colors[6])
            ppu_setstrike(sprite_x + 6);
        else if (colors[7])
            ppu_setstrike(sprite_x + 7);
    }
}

bool ppu_enabled(void)
{
    return (NESmachine->ppu->bg_on || NESmachine->ppu->obj_on);
}

void ppu_renderscanline(int scanline, bool draw_flag)
{
    uint8_t *buf = SCREENMEMORY + scanline * NES_SCREEN_WIDTH;
    //uint8_t *buf = SCREENMEMORY[scanline];
    //uint8_t *buf = SCREENMEMORY_LINE[scanline+1];
    // start scanline - transfer ppu latch into vaddr
    if (NESmachine->ppu->bg_on || NESmachine->ppu->obj_on)  {
    if (0 == scanline)    {
      NESmachine->ppu->vaddr = NESmachine->ppu->vaddr_latch;
    } else {
      NESmachine->ppu->vaddr &= ~0x041F;
      NESmachine->ppu->vaddr |= (NESmachine->ppu->vaddr_latch & 0x041F);
    }
  }

    if (draw_flag)
        ppu_renderbg(buf);

    // TODO: fetch obj data 1 scanline before
    if (true == NESmachine->ppu->drawsprites && true == draw_flag)
        ppu_renderoam(buf, scanline);
    else
        ppu_fakeoam(scanline);
}

void ppu_endscanline(int scanline)
{
    // modify vram address at end of scanline
    if (scanline < 240 && (NESmachine->ppu->bg_on || NESmachine->ppu->obj_on))
    {
        int ytile;

        // check for max 3 bit y tile offset
        if (7 == (NESmachine->ppu->vaddr >> 12)) {
      NESmachine->ppu->vaddr &= ~0x7000;      // clear y tile offset
      ytile = (NESmachine->ppu->vaddr >> 5) & 0x1F;

      if (29 == ytile) {
        NESmachine->ppu->vaddr &= ~0x03E0;   // clear y tile
        NESmachine->ppu->vaddr ^= 0x0800;    // toggle nametable
      } else if (31 == ytile) {
        NESmachine->ppu->vaddr &= ~0x03E0;   // clear y tile
      } else {
        NESmachine->ppu->vaddr += 0x20;      // increment y tile
      }
    } else {
      NESmachine->ppu->vaddr += 0x1000;       // increment tile y offset
        }
    }
}

void ppu_checknmi(void)
{
    if (NESmachine->ppu->ctrl0 & PPU_CTRL0F_NMI) nes_nmi();
}

void ppu_scanline(int scanline, bool draw_flag)
{
    if (scanline < NES_SCREEN_HEIGHT)
    {
        // Lower the Max Sprite per scanline flag
        NESmachine->ppu->stat &= ~PPU_STATF_MAXSPRITE;
        ppu_renderscanline(scanline, draw_flag);
    }
    else if (241 == scanline)
    {
        NESmachine->ppu->stat |= PPU_STATF_VBLANK;
        NESmachine->ppu->vram_accessible = true;
    }
    else if (261 == scanline)
    {
        NESmachine->ppu->stat &= ~PPU_STATF_VBLANK;
        NESmachine->ppu->strikeflag = false;
        NESmachine->ppu->strike_cycle = (uint32_t) - 1;
        NESmachine->ppu->vram_accessible = false;
    }
}

void ppu_destroy(ppu_t **src_ppu)
{
    if (*src_ppu)
    {
        free(*src_ppu);
        *src_ppu = NULL;
    }
}


//--------------------------------------------------------------------------------
// APU.C
//#define APU_OVERSAMPLE

#define APU_VOLUME_DECAY(x) ((x) -= ((x) >> 7))

#define APU_RECTANGLE_OUTPUT(channel) (apu.rectangle[channel].output_vol)
#define APU_TRIANGLE_OUTPUT (apu.triangle.output_vol + (apu.triangle.output_vol >> 2))
#define APU_NOISE_OUTPUT ((apu.noise.output_vol + apu.noise.output_vol + apu.noise.output_vol) >> 2)
#define APU_DMC_OUTPUT ((apu.dmc.output_vol + apu.dmc.output_vol + apu.dmc.output_vol) >> 2)

// look up table madness
int32_t decay_lut[16];
int vbl_lut[32];
int trilength_lut[128];

// noise lookups for both modes
#ifndef REALTIME_NOISE
int8_t noise_long_lut[APU_NOISE_32K];
int8_t noise_short_lut[APU_NOISE_93];
#endif // !REALTIME_NOISE

// vblank length table used for rectangles, triangle, noise
const uint8_t vbl_length[32] = {
    5, 127,
    10, 1,
    19, 2,
    40, 3,
    80, 4,
    30, 5,
    7, 6,
    13, 7,
    6, 8,
    12, 9,
    24, 10,
    48, 11,
    96, 12,
    36, 13,
    8, 14,
    16, 15};

// frequency limit of rectangle channels
const int freq_limit[8] =
    {
        0x3FF, 0x555, 0x666, 0x71C, 0x787, 0x7C1, 0x7E0, 0x7F0};

// noise frequency lookup table
const int noise_freq[16] =
    {
        4, 8, 16, 32, 64, 96, 128, 160,
        202, 254, 380, 508, 762, 1016, 2034, 4068};

// DMC transfer freqs
const int dmc_clocks[16] =
    {
        428, 380, 340, 320, 286, 254, 226, 214,
        190, 160, 142, 128, 106, 85, 72, 54};

// ratios of pos/neg pulse for rectangle waves
const int duty_flip[4] = {2, 4, 8, 12};

void apu_setcontext(apu_t *src_apu);
void apu_setcontext(apu_t *src_apu)
{
    apu = *src_apu;
}

void apu_getcontext(apu_t *dest_apu);
void apu_getcontext(apu_t *dest_apu)
{
    *dest_apu = apu;
}

void apu_setchan(int chan, bool enabled)
{
    if (enabled)
        apu.mix_enable |= (1 << chan);
    else
        apu.mix_enable &= ~(1 << chan);
}

// emulation of the 15-bit shift register the
// NES uses to generate pseudo-random series
// for the white noise channel
#ifdef REALTIME_NOISE
int8_t shift_register15(uint8_t xor_tap)
{
    int sreg = 0x4000;
    int bit0, tap, bit14;

    bit0 = sreg & 1;
    tap = (sreg & xor_tap) ? 1 : 0;
    bit14 = (bit0 ^ tap);
    sreg >>= 1;
    sreg |= (bit14 << 14);
    return (bit0 ^ 1);
}
#else  // !REALTIME_NOISE
void shift_register15(int8_t *buf, int count)
{
    int sreg = 0x4000;
    int bit0, bit1, bit6, bit14;

    if (count == APU_NOISE_93)
    {
        while (count--)
        {
            bit0 = sreg & 1;
            bit6 = (sreg & 0x40) >> 6;
            bit14 = (bit0 ^ bit6);
            sreg >>= 1;
            sreg |= (bit14 << 14);
            *buf++ = bit0 ^ 1;
        }
    }
    else
    { // 32K noise
        while (count--)
        {
            bit0 = sreg & 1;
            bit1 = (sreg & 2) >> 1;
            bit14 = (bit0 ^ bit1);
            sreg >>= 1;
            sreg |= (bit14 << 14);
            *buf++ = bit0 ^ 1;
        }
    }
}
#endif // !REALTIME_NOISE

// RECTANGLE WAVE
// ==============
// reg0: 0-3=volume, 4=envelope, 5=hold, 6-7=duty cycle
// reg1: 0-2=sweep shifts, 3=sweep inc/dec, 4-6=sweep length, 7=sweep on
// reg2: 8 bits of freq
// reg3: 0-2=high freq, 7-4=vbl length counter
//
#ifdef APU_OVERSAMPLE

#define APU_MAKE_RECTANGLE(ch)                                                                                                             \
    int32_t apu_rectangle_##ch(void)                                                                                                       \
    {                                                                                                                                      \
        int32_t output, total;                                                                                                             \
        int num_times;                                                                                                                     \
                                                                                                                                           \
        APU_VOLUME_DECAY(apu.rectangle[ch].output_vol);                                                                                    \
                                                                                                                                           \
        if (false == apu.rectangle[ch].enabled || 0 == apu.rectangle[ch].vbl_length)                                                       \
            return APU_RECTANGLE_OUTPUT(ch);                                                                                               \
                                                                                                                                           \
        if (false == apu.rectangle[ch].holdnote)                                                                                           \
            apu.rectangle[ch].vbl_length--;                                                                                                \
                                                                                                                                           \
        apu.rectangle[ch].env_phase -= 4;                                                                                                  \
        while (apu.rectangle[ch].env_phase < 0)                                                                                            \
        {                                                                                                                                  \
            apu.rectangle[ch].env_phase += apu.rectangle[ch].env_delay;                                                                    \
                                                                                                                                           \
            if (apu.rectangle[ch].holdnote)                                                                                                \
                apu.rectangle[ch].env_vol = (apu.rectangle[ch].env_vol + 1) & 0x0F;                                                        \
            else if (apu.rectangle[ch].env_vol < 0x0F)                                                                                     \
                apu.rectangle[ch].env_vol++;                                                                                               \
        }                                                                                                                                  \
                                                                                                                                           \
        if (apu.rectangle[ch].freq < 8 || (false == apu.rectangle[ch].sweep_inc && apu.rectangle[ch].freq > apu.rectangle[ch].freq_limit)) \
            return APU_RECTANGLE_OUTPUT(ch);                                                                                               \
                                                                                                                                           \
        if (apu.rectangle[ch].sweep_on && apu.rectangle[ch].sweep_shifts)                                                                  \
        {                                                                                                                                  \
            apu.rectangle[ch].sweep_phase -= 2;                                                                                            \
            while (apu.rectangle[ch].sweep_phase < 0)                                                                                      \
            {                                                                                                                              \
                apu.rectangle[ch].sweep_phase += apu.rectangle[ch].sweep_delay;                                                            \
                                                                                                                                           \
                if (apu.rectangle[ch].sweep_inc)                                                                                           \
                {                                                                                                                          \
                    if (0 == ch)                                                                                                           \
                        apu.rectangle[ch].freq += ~(apu.rectangle[ch].freq >> apu.rectangle[ch].sweep_shifts);                             \
                    else                                                                                                                   \
                        apu.rectangle[ch].freq -= (apu.rectangle[ch].freq >> apu.rectangle[ch].sweep_shifts);                              \
                }                                                                                                                          \
                else                                                                                                                       \
                {                                                                                                                          \
                    apu.rectangle[ch].freq += (apu.rectangle[ch].freq >> apu.rectangle[ch].sweep_shifts);                                  \
                }                                                                                                                          \
            }                                                                                                                              \
        }                                                                                                                                  \
                                                                                                                                           \
        apu.rectangle[ch].accum -= apu.cycle_rate;                                                                                         \
        if (apu.rectangle[ch].accum >= 0)                                                                                                  \
            return APU_RECTANGLE_OUTPUT(ch);                                                                                               \
                                                                                                                                           \
        if (apu.rectangle[ch].fixed_envelope)                                                                                              \
            output = apu.rectangle[ch].volume << 8;                                                                                        \
        else                                                                                                                               \
            output = (apu.rectangle[ch].env_vol ^ 0x0F) << 8;                                                                              \
                                                                                                                                           \
        num_times = total = 0;                                                                                                             \
                                                                                                                                           \
        while (apu.rectangle[ch].accum < 0)                                                                                                \
        {                                                                                                                                  \
            apu.rectangle[ch].accum += apu.rectangle[ch].freq + 1;                                                                         \
            apu.rectangle[ch].adder = (apu.rectangle[ch].adder + 1) & 0x0F;                                                                \
                                                                                                                                           \
            if (apu.rectangle[ch].adder < apu.rectangle[ch].duty_flip)                                                                     \
                total += output;                                                                                                           \
            else                                                                                                                           \
                total -= output;                                                                                                           \
                                                                                                                                           \
            num_times++;                                                                                                                   \
        }                                                                                                                                  \
                                                                                                                                           \
        apu.rectangle[ch].output_vol = total / num_times;                                                                                  \
        return APU_RECTANGLE_OUTPUT(ch);                                                                                                   \
    }

#else // !APU_OVERSAMPLE
#define APU_MAKE_RECTANGLE(ch)                                                                                                             \
    int32_t apu_rectangle_##ch(void)                                                                                                       \
    {                                                                                                                                      \
        int32_t output;                                                                                                                    \
                                                                                                                                           \
        APU_VOLUME_DECAY(apu.rectangle[ch].output_vol);                                                                                    \
                                                                                                                                           \
        if (false == apu.rectangle[ch].enabled || 0 == apu.rectangle[ch].vbl_length)                                                       \
            return APU_RECTANGLE_OUTPUT(ch);                                                                                               \
                                                                                                                                           \
        if (false == apu.rectangle[ch].holdnote)                                                                                           \
            apu.rectangle[ch].vbl_length--;                                                                                                \
                                                                                                                                           \
        apu.rectangle[ch].env_phase -= 4;                                                                                                  \
        while (apu.rectangle[ch].env_phase < 0)                                                                                            \
        {                                                                                                                                  \
            apu.rectangle[ch].env_phase += apu.rectangle[ch].env_delay;                                                                    \
                                                                                                                                           \
            if (apu.rectangle[ch].holdnote)                                                                                                \
                apu.rectangle[ch].env_vol = (apu.rectangle[ch].env_vol + 1) & 0x0F;                                                        \
            else if (apu.rectangle[ch].env_vol < 0x0F)                                                                                     \
                apu.rectangle[ch].env_vol++;                                                                                               \
        }                                                                                                                                  \
                                                                                                                                           \
        if (apu.rectangle[ch].freq < 8 || (false == apu.rectangle[ch].sweep_inc && apu.rectangle[ch].freq > apu.rectangle[ch].freq_limit)) \
            return APU_RECTANGLE_OUTPUT(ch);                                                                                               \
                                                                                                                                           \
        if (apu.rectangle[ch].sweep_on && apu.rectangle[ch].sweep_shifts)                                                                  \
        {                                                                                                                                  \
            apu.rectangle[ch].sweep_phase -= 2;                                                                                            \
            while (apu.rectangle[ch].sweep_phase < 0)                                                                                      \
            {                                                                                                                              \
                apu.rectangle[ch].sweep_phase += apu.rectangle[ch].sweep_delay;                                                            \
                                                                                                                                           \
                if (apu.rectangle[ch].sweep_inc)                                                                                           \
                {                                                                                                                          \
                    if (0 == ch)                                                                                                           \
                        apu.rectangle[ch].freq += ~(apu.rectangle[ch].freq >> apu.rectangle[ch].sweep_shifts);                             \
                    else                                                                                                                   \
                        apu.rectangle[ch].freq -= (apu.rectangle[ch].freq >> apu.rectangle[ch].sweep_shifts);                              \
                }                                                                                                                          \
                else                                                                                                                       \
                {                                                                                                                          \
                    apu.rectangle[ch].freq += (apu.rectangle[ch].freq >> apu.rectangle[ch].sweep_shifts);                                  \
                }                                                                                                                          \
            }                                                                                                                              \
        }                                                                                                                                  \
                                                                                                                                           \
        apu.rectangle[ch].accum -= apu.cycle_rate;                                                                                         \
        if (apu.rectangle[ch].accum >= 0)                                                                                                  \
            return APU_RECTANGLE_OUTPUT(ch);                                                                                               \
                                                                                                                                           \
        while (apu.rectangle[ch].accum < 0)                                                                                                \
        {                                                                                                                                  \
            apu.rectangle[ch].accum += (apu.rectangle[ch].freq + 1);                                                                       \
            apu.rectangle[ch].adder = (apu.rectangle[ch].adder + 1) & 0x0F;                                                                \
        }                                                                                                                                  \
                                                                                                                                           \
        if (apu.rectangle[ch].fixed_envelope)                                                                                              \
            output = apu.rectangle[ch].volume << 8;                                                                                        \
        else                                                                                                                               \
            output = (apu.rectangle[ch].env_vol ^ 0x0F) << 8;                                                                              \
                                                                                                                                           \
        if (0 == apu.rectangle[ch].adder)                                                                                                  \
            apu.rectangle[ch].output_vol = output;                                                                                         \
        else if (apu.rectangle[ch].adder == apu.rectangle[ch].duty_flip)                                                                   \
            apu.rectangle[ch].output_vol = -output;                                                                                        \
                                                                                                                                           \
        return APU_RECTANGLE_OUTPUT(ch);                                                                                                   \
    }

#endif // !APU_OVERSAMPLE

// generate the functions
APU_MAKE_RECTANGLE(0)
APU_MAKE_RECTANGLE(1)

// TRIANGLE WAVE
// =============
// reg0: 7=holdnote, 6-0=linear length counter
// reg2: low 8 bits of frequency
// reg3: 7-3=length counter, 2-0=high 3 bits of frequency
//
int32_t apu_triangle(void)
{
    APU_VOLUME_DECAY(apu.triangle.output_vol);

    if (false == apu.triangle.enabled || 0 == apu.triangle.vbl_length)
        return APU_TRIANGLE_OUTPUT;

    if (apu.triangle.counter_started)
    {
        if (apu.triangle.linear_length > 0)
            apu.triangle.linear_length--;
        if (apu.triangle.vbl_length && false == apu.triangle.holdnote)
            apu.triangle.vbl_length--;
    }
    else if (false == apu.triangle.holdnote && apu.triangle.write_latency)
    {
        if (--apu.triangle.write_latency == 0)
            apu.triangle.counter_started = true;
    }

    if (0 == apu.triangle.linear_length || apu.triangle.freq < 4) // inaudible
        return APU_TRIANGLE_OUTPUT;

    apu.triangle.accum -= apu.cycle_rate;
    while (apu.triangle.accum < 0)
    {
        apu.triangle.accum += apu.triangle.freq;
        apu.triangle.adder = (apu.triangle.adder + 1) & 0x1F;

        if (apu.triangle.adder & 0x10)
            apu.triangle.output_vol -= (2 << 8);
        else
            apu.triangle.output_vol += (2 << 8);
    }

    return APU_TRIANGLE_OUTPUT;
}

// WHITE NOISE CHANNEL
// ===================
// reg0: 0-3=volume, 4=envelope, 5=hold
// reg2: 7=small(93 byte) sample,3-0=freq lookup
// reg3: 7-4=vbl length counter
//
// TODO: AAAAAAAAAAAAAAAAAAAAAAAA!  #ifdef MADNESS!

int32_t apu_noise(void)
{
    int32_t outvol;

#if defined(APU_OVERSAMPLE) && defined(REALTIME_NOISE)
#else  // !(APU_OVERSAMPLE && REALTIME_NOISE)
    int32_t noise_bit;
#endif // !(APU_OVERSAMPLE && REALTIME_NOISE)
#ifdef APU_OVERSAMPLE
    int num_times;
    int32_t total;
#endif // APU_OVERSAMPLE

    APU_VOLUME_DECAY(apu.noise.output_vol);

    if (false == apu.noise.enabled || 0 == apu.noise.vbl_length)
        return APU_NOISE_OUTPUT;

    // vbl length counter
    if (false == apu.noise.holdnote)
        apu.noise.vbl_length--;

    // envelope decay at a rate of (env_delay + 1) / 240 secs
    apu.noise.env_phase -= 4; // 240/60
    while (apu.noise.env_phase < 0)
    {
        apu.noise.env_phase += apu.noise.env_delay;

        if (apu.noise.holdnote)
            apu.noise.env_vol = (apu.noise.env_vol + 1) & 0x0F;
        else if (apu.noise.env_vol < 0x0F)
            apu.noise.env_vol++;
    }

    apu.noise.accum -= apu.cycle_rate;
    if (apu.noise.accum >= 0)
        return APU_NOISE_OUTPUT;

#ifdef APU_OVERSAMPLE
    if (apu.noise.fixed_envelope)
        outvol = apu.noise.volume << 8; // fixed volume
    else
        outvol = (apu.noise.env_vol ^ 0x0F) << 8;

    num_times = total = 0;
#endif // APU_OVERSAMPLE

    while (apu.noise.accum < 0)
    {
        apu.noise.accum += apu.noise.freq;

#ifdef REALTIME_NOISE

#ifdef APU_OVERSAMPLE
        if (shift_register15(apu.noise.xor_tap))
            total += outvol;
        else
            total -= outvol;

        num_times++;
#else  // !APU_OVERSAMPLE
        noise_bit = shift_register15(apu.noise.xor_tap);
#endif // !APU_OVERSAMPLE

#else // !REALTIME_NOISE
        apu.noise.cur_pos++;

        if (apu.noise.short_sample)
        {
            if (APU_NOISE_93 == apu.noise.cur_pos)
                apu.noise.cur_pos = 0;
        }
        else
        {
            if (APU_NOISE_32K == apu.noise.cur_pos)
                apu.noise.cur_pos = 0;
        }

#ifdef APU_OVERSAMPLE
        if (apu.noise.short_sample)
            noise_bit = noise_short_lut[apu.noise.cur_pos];
        else
            noise_bit = noise_long_lut[apu.noise.cur_pos];

        if (noise_bit)
            total += outvol;
        else
            total -= outvol;

        num_times++;
#endif // APU_OVERSAMPLE
#endif // !REALTIME_NOISE
    }

#ifdef APU_OVERSAMPLE
    apu.noise.output_vol = total / num_times;
#else // !APU_OVERSAMPLE
    if (apu.noise.fixed_envelope)
        outvol = apu.noise.volume << 8; // fixed volume
    else
        outvol = (apu.noise.env_vol ^ 0x0F) << 8;

#ifndef REALTIME_NOISE
    if (apu.noise.short_sample)
        noise_bit = noise_short_lut[apu.noise.cur_pos];
    else
        noise_bit = noise_long_lut[apu.noise.cur_pos];
#endif // !REALTIME_NOISE

    if (noise_bit)
        apu.noise.output_vol = outvol;
    else
        apu.noise.output_vol = -outvol;
#endif // !APU_OVERSAMPLE
    return APU_NOISE_OUTPUT;
}

inline void apu_dmcreload(void)
{
    apu.dmc.address = apu.dmc.cached_addr;
    apu.dmc.dma_length = apu.dmc.cached_dmalength;
    apu.dmc.irq_occurred = false;
}

// DELTA MODULATION CHANNEL
// =========================
// reg0: 7=irq gen, 6=looping, 3-0=pointer to clock table
// reg1: output dc level, 6 bits unsigned
// reg2: 8 bits of 64-byte aligned address offset : $C000 + (value * 64)
// reg3: length, (value * 16) + 1
//
static int32_t apu_dmc(void)
{
    int delta_bit;

    APU_VOLUME_DECAY(apu.dmc.output_vol);

    // only process when channel is alive
    if (apu.dmc.dma_length)
    {
        apu.dmc.accum -= apu.cycle_rate;

        while (apu.dmc.accum < 0)
        {
            apu.dmc.accum += apu.dmc.freq;

            delta_bit = (apu.dmc.dma_length & 7) ^ 7;

            if (7 == delta_bit)
            {
                apu.dmc.cur_byte = nes6502_getbyte(apu.dmc.address);

                // steal a cycle from CPU
                nes6502_burn(1);

                // prevent wraparound
                if (0xFFFF == apu.dmc.address)
                    apu.dmc.address = 0x8000;
                else
                    apu.dmc.address++;
            }

            if (--apu.dmc.dma_length == 0)
            {
                // if loop bit set, we're cool to retrigger sample
                if (apu.dmc.looping)
                {
                    apu_dmcreload();
                }
                else
                {
                    // check to see if we should generate an irq
                    if (apu.dmc.irq_gen)
                    {
                        apu.dmc.irq_occurred = true;
                        if (apu.irq_callback)
                            apu.irq_callback();
                    }

                    // bodge for timestamp queue
                    apu.dmc.enabled = false;
                    break;
                }
            }

            // positive delta
            if (apu.dmc.cur_byte & (1 << delta_bit))
            {
                if (apu.dmc.regs[1] < 0x7D)
                {
                    apu.dmc.regs[1] += 2;
                    apu.dmc.output_vol += (2 << 8);
                }
            }
            else
            { // negative delta
                if (apu.dmc.regs[1] > 1)
                {
                    apu.dmc.regs[1] -= 2;
                    apu.dmc.output_vol -= (2 << 8);
                }
            }
        }
    }
    return APU_DMC_OUTPUT;
}

void apu_write(uint32_t address, uint8_t value)
{

    if (SOUND_ENABLED)
    {

        int chan;
        switch (address)
        {
        // rectangles
        case APU_WRA0:
        case APU_WRB0:
            chan = (address & 4) >> 2;
            apu.rectangle[chan].regs[0] = value;
            apu.rectangle[chan].volume = value & 0x0F;
            apu.rectangle[chan].env_delay = decay_lut[value & 0x0F];
            apu.rectangle[chan].holdnote = (value & 0x20) ? true : false;
            apu.rectangle[chan].fixed_envelope = (value & 0x10) ? true : false;
            apu.rectangle[chan].duty_flip = duty_flip[value >> 6];
            break;
        case APU_WRA1:
        case APU_WRB1:
            chan = (address & 4) >> 2;
            apu.rectangle[chan].regs[1] = value;
            apu.rectangle[chan].sweep_on = (value & 0x80) ? true : false;
            apu.rectangle[chan].sweep_shifts = value & 7;
            apu.rectangle[chan].sweep_delay = decay_lut[(value >> 4) & 7];
            apu.rectangle[chan].sweep_inc = (value & 0x08) ? true : false;
            apu.rectangle[chan].freq_limit = freq_limit[value & 7];
            break;
        case APU_WRA2:
        case APU_WRB2:
            chan = (address & 4) >> 2;
            apu.rectangle[chan].regs[2] = value;
            apu.rectangle[chan].freq = (apu.rectangle[chan].freq & ~0xFF) | value;
            break;
        case APU_WRA3:
        case APU_WRB3:
            chan = (address & 4) >> 2;
            apu.rectangle[chan].regs[3] = value;
            apu.rectangle[chan].vbl_length = vbl_lut[value >> 3];
            apu.rectangle[chan].env_vol = 0;
            apu.rectangle[chan].freq = ((value & 7) << 8) | (apu.rectangle[chan].freq & 0xFF);
            apu.rectangle[chan].adder = 0;
            break;
        // triangle
        case APU_WRC0:
            apu.triangle.regs[0] = value;
            apu.triangle.holdnote = (value & 0x80) ? true : false;

            if (false == apu.triangle.counter_started && apu.triangle.vbl_length)
                apu.triangle.linear_length = trilength_lut[value & 0x7F];
            break;
        case APU_WRC2:
            apu.triangle.regs[1] = value;
            apu.triangle.freq = (((apu.triangle.regs[2] & 7) << 8) + value) + 1;
            break;
        case APU_WRC3:
            apu.triangle.regs[2] = value;
            // this is somewhat of a hack.  there appears to be some latency on
            // the Real Thing between when trireg0 is written to and when the
            // linear length counter actually begins its countdown.  we want to
            // prevent the case where the program writes to the freq regs first,
            // then to reg 0, and the counter accidentally starts running because
            // of the sound queue's timestamp processing.
            //
            // set latency to a couple hundred cycles -- should be plenty of time
            // for the 6502 code to do a couple of table dereferences and load up
            // the other triregs

            apu.triangle.write_latency = (int)(228 / apu.cycle_rate);
            apu.triangle.freq = (((value & 7) << 8) + apu.triangle.regs[1]) + 1;
            apu.triangle.vbl_length = vbl_lut[value >> 3];
            apu.triangle.counter_started = false;
            apu.triangle.linear_length = trilength_lut[apu.triangle.regs[0] & 0x7F];
            break;
        // noise
        case APU_WRD0:
            apu.noise.regs[0] = value;
            apu.noise.env_delay = decay_lut[value & 0x0F];
            apu.noise.holdnote = (value & 0x20) ? true : false;
            apu.noise.fixed_envelope = (value & 0x10) ? true : false;
            apu.noise.volume = value & 0x0F;
            break;
        case APU_WRD2:
            apu.noise.regs[1] = value;
            apu.noise.freq = noise_freq[value & 0x0F];
#ifdef REALTIME_NOISE
            apu.noise.xor_tap = (value & 0x80) ? 0x40 : 0x02;
#else  // !REALTIME_NOISE
       // detect transition from long->short sample
            if ((value & 0x80) && false == apu.noise.short_sample)
            {
                // recalculate short noise buffer
                shift_register15(noise_short_lut, APU_NOISE_93);
                apu.noise.cur_pos = 0;
            }
            apu.noise.short_sample = (value & 0x80) ? true : false;
#endif // !REALTIME_NOISE
            break;
        case APU_WRD3:
            apu.noise.regs[2] = value;
            apu.noise.vbl_length = vbl_lut[value >> 3];
            apu.noise.env_vol = 0; // reset envelope
            break;
        // DMC
        case APU_WRE0:
            apu.dmc.regs[0] = value;
            apu.dmc.freq = dmc_clocks[value & 0x0F];
            apu.dmc.looping = (value & 0x40) ? true : false;
            if (value & 0x80)
            {
                apu.dmc.irq_gen = true;
            }
            else
            {
                apu.dmc.irq_gen = false;
                apu.dmc.irq_occurred = false;
            }
            break;
        case APU_WRE1: // 7-bit DAC
            // add the _delta_ between written value and
            // current output level of the volume reg
            value &= 0x7F; // bit 7 ignored
            apu.dmc.output_vol += ((value - apu.dmc.regs[1]) << 8);
            apu.dmc.regs[1] = value;
            break;
        case APU_WRE2:
            apu.dmc.regs[2] = value;
            apu.dmc.cached_addr = 0xC000 + (uint16_t)(value << 6);
            break;
        case APU_WRE3:
            apu.dmc.regs[3] = value;
            apu.dmc.cached_dmalength = ((value << 4) + 1) << 3;
            break;
        case APU_SMASK:
            // bodge for timestamp queue
            apu.dmc.enabled = (value & 0x10) ? true : false;
            apu.enable_reg = value;

            for (chan = 0; chan < 2; chan++)
            {
                if (value & (1 << chan))
                {
                    apu.rectangle[chan].enabled = true;
                }
                else
                {
                    apu.rectangle[chan].enabled = false;
                    apu.rectangle[chan].vbl_length = 0;
                }
            }

            if (value & 0x04)
            {
                apu.triangle.enabled = true;
            }
            else
            {
                apu.triangle.enabled = false;
                apu.triangle.vbl_length = 0;
                apu.triangle.linear_length = 0;
                apu.triangle.counter_started = false;
                apu.triangle.write_latency = 0;
            }

            if (value & 0x08)
            {
                apu.noise.enabled = true;
            }
            else
            {
                apu.noise.enabled = false;
                apu.noise.vbl_length = 0;
            }

            if (value & 0x10)
            {
                if (0 == apu.dmc.dma_length)
                    apu_dmcreload();
            }
            else
            {
                apu.dmc.dma_length = 0;
            }
            apu.dmc.irq_occurred = false;
            break;
        // unused, but they get hit in some mem-clear loops
        case 0x4009:
        case 0x400D:
            break;
        default:
            break;
        }
    }
}

// Read from $4000-$4017
uint8_t apu_read(uint32_t address)
{
    uint8_t value;
    if (SOUND_ENABLED)
    {

        switch (address)
        {
        case APU_SMASK:
            value = 0;
            // Return 1 in 0-5 bit pos if a channel is playing
            if (apu.rectangle[0].enabled && apu.rectangle[0].vbl_length)
                value |= 0x01;
            if (apu.rectangle[1].enabled && apu.rectangle[1].vbl_length)
                value |= 0x02;
            if (apu.triangle.enabled && apu.triangle.vbl_length)
                value |= 0x04;
            if (apu.noise.enabled && apu.noise.vbl_length)
                value |= 0x08;

            // bodge for timestamp queue
            if (apu.dmc.enabled)
                value |= 0x10;
            if (apu.dmc.irq_occurred)
                value |= 0x80;
            if (apu.irqclear_callback)
                value |= apu.irqclear_callback();
            break;
        default:
            value = (address >> 8); // heavy capacitance on data bus
            break;
        }
    }
    return value;
}

#define CLIP_OUTPUT16(out) { if (out > 0x7FFF) out = 0x7FFF; else if (out < -0x8000) out = -0x8000; }

void apu_process(void *buffer, int num_samples)
{
    static int32_t prev_sample = 0;
    uint16_t *buf16;
    uint8_t *buf8;

    if (NULL != buffer)
    {
        // bleh
        apu.buffer = buffer;

        buf16 = (uint16_t *)buffer;
        buf8 = (uint8_t *)buffer;

        while (num_samples--)
        {
            int32_t next_sample, accum = 0;
            if (apu.mix_enable & 0x01)
                accum += apu_rectangle_0();
            if (apu.mix_enable & 0x02)
                accum += apu_rectangle_1();
            if (apu.mix_enable & 0x04)
                accum += apu_triangle();
            if (apu.mix_enable & 0x08)
                accum += apu_noise();
            if (apu.mix_enable & 0x10)
                accum += apu_dmc();
            if (apu.ext && (apu.mix_enable & 0x20))
                accum += apu.ext->process();

            // do any filtering
            if (APU_FILTER_NONE != apu.filter_type)
            {
                next_sample = accum;

                if (APU_FILTER_LOWPASS == apu.filter_type)
                {
                    accum += prev_sample;
                    accum >>= 1;
                }
                else
                    accum = (accum + accum + accum + prev_sample) >> 2;

                prev_sample = next_sample;
            }

            // do clipping
            CLIP_OUTPUT16(accum);

            // signed 16-bit output, unsigned 8-bit
            if (16 == apu.sample_bits)
                *buf16++ = (uint16_t)accum;
            else
                *buf8++ = (accum >> 8) ^ 0x80;
        }
    }
}

// set the filter type
void apu_setfilter(int filter_type)
{
    apu.filter_type = filter_type;
}

void apu_reset(void)
{
    uint32_t address;

    // initialize all channel members
    for (address = 0x4000; address <= 0x4013; address++)
        apu_write(address, 0);
    apu_write(0x4015, 0);
    if (apu.ext && NULL != apu.ext->reset)
        apu.ext->reset();
}

void apu_build_luts(int num_samples)
{
    int i;

    // lut used for enveloping and frequency sweeps
    for (i = 0; i < 16; i++)
        decay_lut[i] = num_samples * (i + 1);

    // used for note length, based on vblanks and size of audio buffer
    for (i = 0; i < 32; i++)
        vbl_lut[i] = vbl_length[i] * num_samples;

    // triangle wave channel's linear length table
    for (i = 0; i < 128; i++)
        trilength_lut[i] = (int)(0.25 * i * num_samples);

#ifndef REALTIME_NOISE
    // generate noise samples 这被注销了
    shift_register15(noise_long_lut, APU_NOISE_32K);
    shift_register15(noise_short_lut, APU_NOISE_93);
#endif // !REALTIME_NOISE
}

void apu_setparams(double base_freq, int sample_rate, int refresh_rate, int sample_bits)
{
    apu.sample_rate = sample_rate;
    apu.refresh_rate = refresh_rate;
    apu.sample_bits = sample_bits;
    apu.num_samples = sample_rate / refresh_rate;
    if (0 == base_freq)
        apu.base_freq = APU_BASEFREQ;
    else
        apu.base_freq = base_freq;
    apu.cycle_rate = (float)(apu.base_freq / sample_rate);

    // build various lookup tables for apu
    apu_build_luts(apu.num_samples);

    apu_reset();
}

apu_t *temp_apu;
// Initializes emulated sound hardware, creates waveforms/voices
apu_t *apu_create(double base_freq, int sample_rate, int refresh_rate, int sample_bits);
apu_t *apu_create(double base_freq, int sample_rate, int refresh_rate, int sample_bits)
{
    
    int channel;

    if (NULL == temp_apu)
        temp_apu = (apu_t *)heap_caps_malloc(sizeof(apu_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == temp_apu)
        return NULL;

    memset(temp_apu, 0, sizeof(apu_t));

    // set the update routine
    temp_apu->process = apu_process;
    temp_apu->ext = NULL;

    // clear the callbacks
    temp_apu->irq_callback = NULL;
    temp_apu->irqclear_callback = NULL;

    apu_setcontext(temp_apu);
    apu_setparams(base_freq, sample_rate, refresh_rate, sample_bits);
    for (channel = 0; channel < 6; channel++)
        apu_setchan(channel, true);
    apu_setfilter(APU_FILTER_WEIGHTED);
    apu_getcontext(temp_apu);
    return temp_apu;
}

void apu_destroy(apu_t **src_apu);
void apu_destroy(apu_t **src_apu)
{
    if (*src_apu)
    {
        ///    if ((*src_apu)->ext && NULL != (*src_apu)->ext->shutdown) (*src_apu)->ext->shutdown();
        free(*src_apu);
        *src_apu = NULL;
    }
}


void apu_setext(apu_t *src_apu, apuext_t *ext)
{
    src_apu->ext = ext;

    // initialize it
    if (src_apu->ext && NULL != src_apu->ext->init)
        src_apu->ext->init();
}
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------


nes_t *nes_getcontextptr(void)
{
    return NESmachine; ///
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
///(MMC.C) part

rominfo_t *mmc_getinfo(void)
{
    return mmc.cart;
}

void ppu_setlatchfunc(ppulatchfunc_t func);
void ppu_setlatchfunc(ppulatchfunc_t func) {
  NESmachine->ppu->latchfunc = func;
}
void ppu_setvromswitch(ppuvromswitch_t func);
void ppu_setvromswitch(ppuvromswitch_t func) {
  NESmachine->ppu->vromswitch = func;
}

//--------------------------------------------------------------------------------
// MMC.C
#define MMC_8KROM (mmc.cart->rom_banks * 2)
#define MMC_16KROM (mmc.cart->rom_banks)
#define MMC_32KROM (mmc.cart->rom_banks / 2)
#define MMC_8KVROM (mmc.cart->vrom_banks)
#define MMC_4KVROM (mmc.cart->vrom_banks * 2)
#define MMC_2KVROM (mmc.cart->vrom_banks * 4)
#define MMC_1KVROM (mmc.cart->vrom_banks * 8)

#define MMC_LAST8KROM (MMC_8KROM - 1)
#define MMC_LAST16KROM (MMC_16KROM - 1)
#define MMC_LAST32KROM (MMC_32KROM - 1)
#define MMC_LAST8KVROM (MMC_8KVROM - 1)
#define MMC_LAST4KVROM (MMC_4KVROM - 1)
#define MMC_LAST2KVROM (MMC_2KVROM - 1)
#define MMC_LAST1KVROM (MMC_1KVROM - 1)


void mmc_setcontext(mmc_t *src_mmc)
{
    mmc = *src_mmc;
}


void mmc_getcontext(mmc_t *dest_mmc)
{
    *dest_mmc = mmc;
}

// VROM bankswitching
void mmc_bankvrom(int size, uint32_t address, int bank)
{
    if (0 == mmc.cart->vrom_banks)
        return;

    switch (size)
    {
    case 1:
        if (bank == MMC_LASTBANK)
            bank = MMC_LAST1KVROM;
        ppu_setpage(1, address >> 10, &mmc.cart->vrom[(bank % MMC_1KVROM) << 10] - address);
        break;

    case 2:
        if (bank == MMC_LASTBANK)
            bank = MMC_LAST2KVROM;
        ppu_setpage(2, address >> 10, &mmc.cart->vrom[(bank % MMC_2KVROM) << 11] - address);
        break;

    case 4:
        if (bank == MMC_LASTBANK)
            bank = MMC_LAST4KVROM;
        ppu_setpage(4, address >> 10, &mmc.cart->vrom[(bank % MMC_4KVROM) << 12] - address);
        break;

    case 8:
        if (bank == MMC_LASTBANK)
            bank = MMC_LAST8KVROM;
        ppu_setpage(8, 0, &mmc.cart->vrom[(bank % MMC_8KVROM) << 13]);
        break;

    default:
        log_d("???\n");
        true;
        /// nes_log_printf("invalid VROM bank size %d\n", size);
    }
}

// ROM bankswitching
void mmc_bankrom(int size, uint32_t address, int bank)
{
    nes6502_context mmc_cpu;

    nes6502_getcontext(&mmc_cpu);

    switch (size)
    {
    case 8:
        if (bank == MMC_LASTBANK)
            bank = MMC_LAST8KROM;
        {
            int page = address >> NES6502_BANKSHIFT;
            mmc_cpu.mem_page[page] = &mmc.cart->rom[(bank % MMC_8KROM) << 13];
            mmc_cpu.mem_page[page + 1] = mmc_cpu.mem_page[page] + 0x1000;
        }

        break;

    case 16:
        if (bank == MMC_LASTBANK)
            bank = MMC_LAST16KROM;
        {
            int page = address >> NES6502_BANKSHIFT;
            mmc_cpu.mem_page[page] = &mmc.cart->rom[(bank % MMC_16KROM) << 14];
            mmc_cpu.mem_page[page + 1] = mmc_cpu.mem_page[page] + 0x1000;
            mmc_cpu.mem_page[page + 2] = mmc_cpu.mem_page[page] + 0x2000;
            mmc_cpu.mem_page[page + 3] = mmc_cpu.mem_page[page] + 0x3000;
        }
        break;

    case 32:
        if (bank == MMC_LASTBANK)
            bank = MMC_LAST32KROM;

        mmc_cpu.mem_page[8] = &mmc.cart->rom[(bank % MMC_32KROM) << 15];
        mmc_cpu.mem_page[9] = mmc_cpu.mem_page[8] + 0x1000;
        mmc_cpu.mem_page[10] = mmc_cpu.mem_page[8] + 0x2000;
        mmc_cpu.mem_page[11] = mmc_cpu.mem_page[8] + 0x3000;
        mmc_cpu.mem_page[12] = mmc_cpu.mem_page[8] + 0x4000;
        mmc_cpu.mem_page[13] = mmc_cpu.mem_page[8] + 0x5000;
        mmc_cpu.mem_page[14] = mmc_cpu.mem_page[8] + 0x6000;
        mmc_cpu.mem_page[15] = mmc_cpu.mem_page[8] + 0x7000;
        break;

    default:
        /// nes_log_printf("invalid ROM bank size %d\n", size);
        break;
    }

    nes6502_setcontext(&mmc_cpu);
}

//--------------------------------------------------------------------------------
#include "mappers.h"
//--------------------------------------------------------------------------------
// MMCLIST.C:
// implemented mapper interfaces
const mapintf_t *mappers[] = {
    &map0_intf,
    &map1_intf,
    &map2_intf,
    &map3_intf,
    &map4_intf,
    &map5_intf,
    &map7_intf,
    &map8_intf,
    &map9_intf,
    &map10_intf, //新增
    &map11_intf,
    &map15_intf,
    &map16_intf,
    &map18_intf,
    &map19_intf,
    &map21_intf,
    &map22_intf,
    &map23_intf,
    &map24_intf,
    &map25_intf,
    &map32_intf,
    &map33_intf,
    &map34_intf,
    &map40_intf,
    &map41_intf,
    &map42_intf,
    &map46_intf,
    &map50_intf,
    &map64_intf,
    &map65_intf,
    &map66_intf,
    &map70_intf,
    &map73_intf, //新增
    &map74_intf, //新增
    &map75_intf,
    &map78_intf,
    &map79_intf,
    &map85_intf,
    &map87_intf,
    &map93_intf,
    &map94_intf,
    &map99_intf,
    &map160_intf,
    &map229_intf,
    &map231_intf,
    NULL};
// Check to see if this mapper is supported
bool mmc_peek(int map_num)
{
    mapintf_t **map_ptr = (mapintf_t **)mappers;

    while (NULL != *map_ptr)
    {
        if ((*map_ptr)->number == map_num)
            return true;
        map_ptr++;
    }

    return false;
}

static void mmc_setpages(void)
{
    /// nes_log_printf("setting up mapper %d\n", mmc.intf->number);

    // Switch ROM into CPU space, set VROM/VRAM (done for ALL ROMs)
    mmc_bankrom(16, 0x8000, 0);
    mmc_bankrom(16, 0xC000, MMC_LASTBANK);
    mmc_bankvrom(8, 0x0000, 0);

    if (mmc.cart->flags & ROM_FLAG_FOURSCREEN)
    {
        ppu_mirror(0, 1, 2, 3);
    }
    else
    {
        if (MIRROR_VERT == mmc.cart->mirror)
            ppu_mirror(0, 1, 0, 1);
        else
            ppu_mirror(0, 0, 1, 1);
    }

    // if we have no VROM, switch in VRAM
    // TODO: fix this hack implementation
    if (0 == mmc.cart->vrom_banks)
    {
        ppu_setpage(8, 0, mmc.cart->vram);
        ppu_mirrorhipages();
    }
}

// Mapper initialization routine
void mmc_reset(void)
{
    mmc_setpages();

    ppu_setlatchfunc(NULL);
    ppu_setvromswitch(NULL);

    if (mmc.intf->init)
        mmc.intf->init();
}


void mmc_destroy(mmc_t **nes_mmc)
{
    if (*nes_mmc)
        free(*nes_mmc);
}

mmc_t *mmc_temp;
mapintf_t **map_ptr;

mmc_t *mmc_create(rominfo_t *rominfo)
{
    for (map_ptr = (mapintf_t **)mappers; (*map_ptr)->number != rominfo->mapper_number; map_ptr++)
    {
        if (NULL == *map_ptr)
            return NULL; // Should *never* happen
    }

    if (NULL == mmc_temp) mmc_temp = (mmc_t *)heap_caps_malloc(sizeof(mmc_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == mmc_temp)
        return NULL;

    memset(temp, 0, sizeof(mmc_t));

    mmc_temp->intf = *map_ptr;
    mmc_temp->cart = rominfo;

    mmc_setcontext(mmc_temp);
    return mmc_temp;
}
//--------------------------------------------------------------------------------
// Reset NES hardware
void nes_reset()
{
    memset(NESmachine->cpu->mem_page[0], 0, NES_RAMSIZE);
    if (NESmachine->rominfo->vram)
        mem_trash(NESmachine->rominfo->vram, 0x2000 * NESmachine->rominfo->vram_banks);
    /// if (nes.rominfo->vram) memset(nes.rominfo->vram, 0, VRAM_LENGTH);
    /// if (nes.rominfo->vram) memset(nes.rominfo->vram, 0, 0x2000 * nes.rominfo->vram_banks);

    if (SOUND_ENABLED)
        apu_reset();
    ppu_reset();
    mmc_reset();
    nes6502_reset();

    NESmachine->scanline = 241;
}

//--------------------------------------------------------------------------------
uint8_t nes_clearfiq(void)
{
    if (NESmachine->fiq_occurred) {
    NESmachine->fiq_occurred = false;
        return 0x40;
    }
    return 0;
}

nes_t *machine;
sndinfo_t osd_sound;
// Initialize NES CPU, hardware, etc.
nes_t *nes_create(void)
{
    int i;
    if (NULL == machine)
        machine = (nes_t *)heap_caps_malloc(sizeof(nes_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == machine)
        return NULL;

    memset(machine, 0, sizeof(nes_t));
    ////machine->autoframeskip = true;
    // cpu
    if (NULL == machine->cpu)
        machine->cpu = (nes6502_context *)heap_caps_malloc(sizeof(nes6502_context), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == machine->cpu)
        goto _fail;

    memset(machine->cpu, 0, sizeof(nes6502_context));

    // allocate 2kB RAM
    if (NULL == machine->cpu->mem_page[0])
        machine->cpu->mem_page[0] = (uint8_t *)heap_caps_malloc(NES_RAMSIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == machine->cpu->mem_page[0])
        goto _fail;

    /// if (NULL != machine->cpu->mem_page[0]) memset(machine->cpu->mem_page[0], 0, sizeof(NES_RAMSIZE));

    // point all pages at NULL for now
    for (i = 1; i < NES6502_NUMBANKS; i++)
        machine->cpu->mem_page[i] = NULL;

    machine->cpu->read_handler = machine->readhandler;
    machine->cpu->write_handler = machine->writehandler;


    // apu
    if (SOUND_ENABLED)
    {
        osd_getsoundinfo(&osd_sound);
        /// machine->apu = apu_create(0, 25, NES_REFRESH_RATE, 256); //25fps
        machine->apu = apu_create(0, osd_sound.sample_rate, NES_REFRESH_RATE, osd_sound.bps);

        if (NULL == machine->apu)
            goto _fail;

        // set the IRQ routines
        machine->apu->irq_callback = nes_irq;
        machine->apu->irqclear_callback = nes_clearfiq;
    }

    // ppu
    machine->ppu = ppu_create();
    if (NULL == machine->ppu)
        goto _fail;

    return machine;

_fail:
    //与原版不同，原版这里没注释
    /// nes_destroy(&machine);
    return NULL;
}

static uint8_t ram_read(uint32_t address)
{
    return NESmachine->cpu->mem_page[0][address & (NES_RAMSIZE - 1)];
}

static void ram_write(uint32_t address, uint8_t value)
{
    NESmachine->cpu->mem_page[0][address & (NES_RAMSIZE - 1)] = value;
}

static void write_protect(uint32_t address, uint8_t value)
{
    // don't allow write to go through
    UNUSED_NES(address);
    UNUSED_NES(value);
}

static uint8_t read_protect(uint32_t address)
{
    // don't allow read to go through
    UNUSED_NES(address);

    return 0xFF;
}

#define  LAST_MEMORY_HANDLER  { -1, -1, NULL }
// read/write handlers for standard NES
static nes6502_memread default_readhandler[] = {
    {0x0800, 0x1FFF, ram_read},
    {0x2000, 0x3FFF, ppu_read},
    {0x4000, 0x4015, apu_read},
    {0x4016, 0x4017, ppu_readhigh},
    LAST_MEMORY_HANDLER};

static nes6502_memwrite default_writehandler[] = {
    {0x0800, 0x1FFF, ram_write},
    {0x2000, 0x3FFF, ppu_write},
    {0x4000, 0x4013, apu_write},
    {0x4015, 0x4015, apu_write},
    {0x4014, 0x4017, ppu_writehigh},
    LAST_MEMORY_HANDLER};

// this big nasty boy sets up the address handlers that the CPU uses

void build_address_handlers(nes_t *machine)
{
    int count, num_handlers = 0;
    mapintf_t *intf;

    intf = machine->mmc->intf;

    memset(machine->readhandler, 0, sizeof(machine->readhandler));
    memset(machine->writehandler, 0, sizeof(machine->writehandler));

    for (count = 0; num_handlers < MAX_MEM_HANDLERS; count++, num_handlers++)
    {
        if (NULL == default_readhandler[count].read_func)
            break;

        memcpy(&machine->readhandler[num_handlers], &default_readhandler[count], sizeof(nes6502_memread));
    }

    if (intf->sound_ext && SOUND_ENABLED)
    {
        if (NULL != intf->sound_ext->mem_read)
        {
            for (count = 0; num_handlers < MAX_MEM_HANDLERS; count++, num_handlers++)
            {
                if (NULL == intf->sound_ext->mem_read[count].read_func)
                    break;
                memcpy(&machine->readhandler[num_handlers], &intf->sound_ext->mem_read[count], sizeof(nes6502_memread));
            }
        }
    }

    if (NULL != intf->mem_read)
    {
        for (count = 0; num_handlers < MAX_MEM_HANDLERS; count++, num_handlers++)
        {
            if (NULL == intf->mem_read[count].read_func)
                break;

            memcpy(&machine->readhandler[num_handlers], &intf->mem_read[count], sizeof(nes6502_memread));
        }
    }

    // TODO: poof! numbers
    machine->readhandler[num_handlers].min_range = 0x4018;
    machine->readhandler[num_handlers].max_range = 0x5FFF;
    machine->readhandler[num_handlers].read_func = read_protect;
    num_handlers++;
    machine->readhandler[num_handlers].min_range = -1;
    machine->readhandler[num_handlers].max_range = -1;
    machine->readhandler[num_handlers].read_func = NULL;
    num_handlers++;

    num_handlers = 0;

    for (count = 0; num_handlers < MAX_MEM_HANDLERS; count++, num_handlers++)
    {
        if (NULL == default_writehandler[count].write_func)
            break;

        memcpy(&machine->writehandler[num_handlers], &default_writehandler[count], sizeof(nes6502_memwrite));
    }

    if (intf->sound_ext)
    {
        if (NULL != intf->sound_ext->mem_write)
        {
            for (count = 0; num_handlers < MAX_MEM_HANDLERS; count++, num_handlers++)
            {
                if (NULL == intf->sound_ext->mem_write[count].write_func)
                    break;
                memcpy(&machine->writehandler[num_handlers], &intf->sound_ext->mem_write[count], sizeof(nes6502_memwrite));
            }
        }
    }

    if (NULL != intf->mem_write)
    {
        for (count = 0; num_handlers < MAX_MEM_HANDLERS; count++, num_handlers++)
        {
            if (NULL == intf->mem_write[count].write_func)
                break;
            memcpy(&machine->writehandler[num_handlers], &intf->mem_write[count], sizeof(nes6502_memwrite));
        }
    }

    // catch-all for bad writes
    // TODO: poof! numbers
    machine->writehandler[num_handlers].min_range = 0x4018;
    machine->writehandler[num_handlers].max_range = 0x5FFF;
    machine->writehandler[num_handlers].write_func = write_protect;
    num_handlers++;
    machine->writehandler[num_handlers].min_range = 0x8000;
    machine->writehandler[num_handlers].max_range = 0xFFFF;
    machine->writehandler[num_handlers].write_func = write_protect;
    num_handlers++;
    machine->writehandler[num_handlers].min_range = -1;
    machine->writehandler[num_handlers].max_range = -1;
    machine->writehandler[num_handlers].write_func = NULL;
    num_handlers++;
}


void nes_setcontext(nes_t *machine)
{
    if (SOUND_ENABLED)
        apu_setcontext(machine->apu);
    ppu_setcontext(machine->ppu);
    nes6502_setcontext(machine->cpu);
    mmc_setcontext(machine->mmc);

    //nes = *machine;
}

// raise an IRQ
void nes_irq(void)
{
    nes6502_irq();
}

void nes_checkfiq(int cycles)
{
    NESmachine->fiq_cycles -= cycles;
  if (NESmachine->fiq_cycles <= 0) {
    NESmachine->fiq_cycles += (int) NES_FIQ_PERIOD;
    if (0 == (NESmachine->fiq_state & 0xC0)) {
      NESmachine->fiq_occurred = true;
      nes6502_irq();
    }
  }
}

void nes_renderframe(bool draw_flag)
{
    int elapsed_cycles;
    mapintf_t *mapintf = NESmachine->mmc->intf;
    int in_vblank = 0;

    while (262 != NESmachine->scanline)
    {
        ppu_scanline(NESmachine->scanline, draw_flag);
        if (241 == NESmachine->scanline)
        {
            // 7-9 cycle delay between when VINT flag goes up and NMI is taken
            elapsed_cycles = nes6502_execute(7);
            NESmachine->scanline_cycles -= elapsed_cycles;
            nes_checkfiq(elapsed_cycles);

            ppu_checknmi();
            if (mapintf->vblank)
                mapintf->vblank();
            in_vblank = 1;
        }

        if (mapintf->hblank)
            mapintf->hblank(in_vblank);
        
        NESmachine->scanline_cycles = 0; //原版没有，但是我去掉之后也没用

        NESmachine->scanline_cycles += (float) NES_SCANLINE_CYCLES;
        elapsed_cycles = nes6502_execute((int)NESmachine->scanline_cycles);
        NESmachine->scanline_cycles -= (float)elapsed_cycles;
        nes_checkfiq(elapsed_cycles);

        ppu_endscanline(NESmachine->scanline);
        NESmachine->scanline++;
    }
    NESmachine->scanline = 0;
}

void nes_destroy(nes_t **machine)
{
    if (*machine)
    {
        rom_free(&(*machine)->rominfo);
        mmc_destroy(&(*machine)->mmc);
        ppu_destroy(&(*machine)->ppu);
        apu_destroy(&(*machine)->apu);
        if ((*machine)->cpu)
        {
            if ((*machine)->cpu->mem_page[0])
                free((*machine)->cpu->mem_page[0]);
            free((*machine)->cpu);
        }

        free(*machine);
        *machine = NULL;
    }
}

NES_STATE_BLOCKS * NesStateBlocks;

int NES_state_save(char* filename) 
{ 
   NesStateBlocks = (NES_STATE_BLOCKS*)heap_caps_malloc(sizeof(NES_STATE_BLOCKS), MALLOC_CAP_SPIRAM);
//--------------------------------------------------------------------------------
//BASE BLOCK
//--------------------------------------------------------------------------------

   NesStateBlocks->fiq_occurred = NESmachine->fiq_occurred;
   NesStateBlocks->fiq_state = NESmachine->fiq_state;
   NesStateBlocks->fiq_cycles = NESmachine->fiq_cycles;
   NesStateBlocks->scanline = NESmachine->scanline;

   NesStateBlocks->jammed = cpu.jammed;
   NesStateBlocks->int_pending = cpu.int_pending;
   NesStateBlocks->int_latency = cpu.int_latency;
   NesStateBlocks->total_cycles = cpu.total_cycles;
   NesStateBlocks->burn_cycles = cpu.burn_cycles;
  
   NesStateBlocks->regA = cpu.a_reg;
   NesStateBlocks->regX = cpu.x_reg;
   NesStateBlocks->regY = cpu.y_reg;
   NesStateBlocks->regFlags = cpu.p_reg;
   NesStateBlocks->regStack = cpu.s_reg;
   NesStateBlocks->regPc = cpu.pc_reg;

   NesStateBlocks->remaining_cycles=remaining_cycles;

   NesStateBlocks->reg2000 = NESmachine->ppu->ctrl0;
   NesStateBlocks->reg2001 = NESmachine->ppu->ctrl1;

   memcpy(NesStateBlocks->cpuRam, &cpu.mem_page[0][0], 0x800);
   memcpy(NesStateBlocks->spriteRam, &NESmachine->ppu->oam[0], 0x100);
   memcpy(NesStateBlocks->ppuRam, &NESmachine->ppu->nametab[0], 0x1000);
   memcpy(NesStateBlocks->null_page,&null_page[0], NES6502_BANKSIZE);

   for (uint8_t i = 0; i < 32; i++) NesStateBlocks->palette[i] = NESmachine->ppu->palette[i];

   NesStateBlocks->mirrorState[0] = (NESmachine->ppu->page[8] + 0x2000 - NESmachine->ppu->nametab) / 0x400;
   NesStateBlocks->mirrorState[1] = (NESmachine->ppu->page[9] + 0x2400 - NESmachine->ppu->nametab) / 0x400;
   NesStateBlocks->mirrorState[2] = (NESmachine->ppu->page[10] + 0x2800 - NESmachine->ppu->nametab) / 0x400;
   NesStateBlocks->mirrorState[3] = (NESmachine->ppu->page[11] + 0x2C00 - NESmachine->ppu->nametab) / 0x400;

   NesStateBlocks->vramAddress = NESmachine->ppu->vaddr;
   NesStateBlocks->spriteRamAddress = NESmachine->ppu->oam_addr;
   NesStateBlocks->tileXOffset = NESmachine->ppu->tile_xofs;

   NesStateBlocks->PPU_stat = NESmachine->ppu->stat;
   NesStateBlocks->PPU_vaddr_latch = NESmachine->ppu->vaddr_latch;
   NesStateBlocks->PPU_vaddr_inc = NESmachine->ppu->vaddr_inc;
   NesStateBlocks->PPU_tile_nametab = NESmachine->ppu->tile_nametab;
   NesStateBlocks->PPU_flipflop = NESmachine->ppu->flipflop;

   NesStateBlocks->PPU_obj_height = NESmachine->ppu->obj_height;
   NesStateBlocks->PPU_obj_base = NESmachine->ppu->obj_base;
   NesStateBlocks->PPU_bg_base = NESmachine->ppu->bg_base;
   NesStateBlocks->PPU_bg_on = NESmachine->ppu->bg_on;
   NesStateBlocks->PPU_obj_on = NESmachine->ppu->obj_on;
   NesStateBlocks->PPU_obj_mask = NESmachine->ppu->obj_mask;
   NesStateBlocks->PPU_bg_mask = NESmachine->ppu->bg_mask;
   NesStateBlocks->PPU_latch = NESmachine->ppu->latch;
   NesStateBlocks->PPU_vdata_latch = NESmachine->ppu->vdata_latch;
   NesStateBlocks->PPU_strobe = NESmachine->ppu->strobe;
   NesStateBlocks->PPU_strikeflag = NESmachine->ppu->strikeflag;
   NesStateBlocks->PPU_strike_cycle = NESmachine->ppu->strike_cycle;
   NesStateBlocks->PPU_vram_accessible = NESmachine->ppu->vram_accessible;
   NesStateBlocks->PPU_vram_present = NESmachine->ppu->vram_present;
   NesStateBlocks->PPU_drawsprites = NESmachine->ppu->drawsprites;
  
//--------------------------------------------------------------------------------
//VRAM BLOCK
//--------------------------------------------------------------------------------

   if (NESmachine->rominfo->vram) {  
      if (NESmachine->rominfo->vram_banks > 2)
      {
         printf("too many VRAM banks: %d\n", NESmachine->rominfo->vram_banks);
         return -1;
      }
      NesStateBlocks->vramSize = VRAM_8K * NESmachine->rominfo->vram_banks;
      memcpy(NesStateBlocks->vram, &NESmachine->rominfo->vram[0], NesStateBlocks->vramSize);
   } else {
      printf("NES VRAM not present\n");
   }
//--------------------------------------------------------------------------------
//SRAM BLOCK
//--------------------------------------------------------------------------------

   printf("NES STATE SAVE: SRAM BLOCK\n");

   if (NESmachine->rominfo->sram) {
      int sram_length;
      sram_length = SRAM_BANK_LENGTH * NESmachine->rominfo->sram_banks;
      if (NESmachine->rominfo->sram_banks > 8) {
         printf("Unsupported number of SRAM banks: %d\n", NESmachine->rominfo->sram_banks);
         return -1;
      }
      NesStateBlocks->sramSize = sram_length; 
      // TODO: this should not always be true!! 
      NesStateBlocks->sramEnabled = true;
      memcpy(NesStateBlocks->sram, &NESmachine->rominfo->sram[0], NesStateBlocks->sramSize);
   } else {
      printf("NES SRAM not present\n");
   }

//--------------------------------------------------------------------------------
//MAPPERS BLOCK
//--------------------------------------------------------------------------------

   // TODO: snss spec should be updated, using 4kB ROM pages.. 
   for (uint8_t i = 0; i < 4; i++) NesStateBlocks->prgPages[i] = (cpu.mem_page[(i + 4) * 2] - NESmachine->rominfo->rom) >> 13;

   if (NESmachine->rominfo->vrom_banks) {
      for (uint8_t i = 0; i < 8; i++) NesStateBlocks->chrPages[i] = (ppu_getpage(i) - NESmachine->rominfo->vrom + (i * 0x400)) >> 10;
   } else {
      // bleh! slight hack 
      for (uint8_t i = 0; i < 8; i++) NesStateBlocks->chrPages[i] = i;
   }

   if (NESmachine->mmc->intf->get_state) NESmachine->mmc->intf->get_state(&NesStateBlocks->mapperBlock);
 
//--------------------------------------------------------------------------------
//SOUND REGS BLOCK
//--------------------------------------------------------------------------------
   // rect 0 
   NesStateBlocks->soundRegisters[0x00] = apu.rectangle[0].regs[0];
   NesStateBlocks->soundRegisters[0x01] = apu.rectangle[0].regs[1];
   NesStateBlocks->soundRegisters[0x02] = apu.rectangle[0].regs[2];
   NesStateBlocks->soundRegisters[0x03] = apu.rectangle[0].regs[3];
   // rect 1 
   NesStateBlocks->soundRegisters[0x04] = apu.rectangle[1].regs[0];
   NesStateBlocks->soundRegisters[0x05] = apu.rectangle[1].regs[1];
   NesStateBlocks->soundRegisters[0x06] = apu.rectangle[1].regs[2];
   NesStateBlocks->soundRegisters[0x07] = apu.rectangle[1].regs[3];
   // triangle 
   NesStateBlocks->soundRegisters[0x08] = apu.triangle.regs[0];
   NesStateBlocks->soundRegisters[0x0A] = apu.triangle.regs[1];
   NesStateBlocks->soundRegisters[0x0B] = apu.triangle.regs[2];
   // noise 
   NesStateBlocks->soundRegisters[0X0C] = apu.noise.regs[0];
   NesStateBlocks->soundRegisters[0X0E] = apu.noise.regs[1];
   NesStateBlocks->soundRegisters[0x0F] = apu.noise.regs[2];
   // dmc 
   NesStateBlocks->soundRegisters[0x10] = apu.dmc.regs[0];
   NesStateBlocks->soundRegisters[0x11] = apu.dmc.regs[1];
   NesStateBlocks->soundRegisters[0x12] = apu.dmc.regs[2];
   NesStateBlocks->soundRegisters[0x13] = apu.dmc.regs[3];
   // control 
   NesStateBlocks->soundRegisters[0x15] = apu.enable_reg;

   //File fp = SD_MMC.open(filename, O_RDWR | O_CREAT | O_NONBLOCK);
   //fp.truncate(0); 
   //fp.write(NesStateBlocks,sizeof(NES_STATE_BLOCKS));
   //fp.close();
   free(NesStateBlocks); 
   return 1;
 
}

int NES_state_load(char* filename) {
   NesStateBlocks = (NES_STATE_BLOCKS*)heap_caps_malloc(sizeof(NES_STATE_BLOCKS), MALLOC_CAP_SPIRAM);

   File fp = SD_MMC.open(filename);
   if (!fp) {
      return -1; //no save file found...
   }

   for (uint32_t tmp=0;tmp<sizeof(NES_STATE_BLOCKS);tmp++) {
      ((unsigned char*)NesStateBlocks)[tmp]= (unsigned char)fp.read();
   }
   fp.close();

//--------------------------------------------------------------------------------
//BASE BLOCK
//--------------------------------------------------------------------------------

   NESmachine->fiq_occurred = NesStateBlocks->fiq_occurred;
   NESmachine->fiq_state = NesStateBlocks->fiq_state;
   NESmachine->fiq_cycles = NesStateBlocks->fiq_cycles;
   NESmachine->scanline = NesStateBlocks->scanline;
  
   cpu.jammed = NesStateBlocks->jammed;
   cpu.int_pending = NesStateBlocks->int_pending;
   cpu.int_latency = NesStateBlocks->int_latency;
   cpu.total_cycles = NesStateBlocks->total_cycles;
   cpu.burn_cycles = NesStateBlocks->burn_cycles;

   cpu.a_reg = NesStateBlocks->regA;
   cpu.x_reg = NesStateBlocks->regX;
   cpu.y_reg = NesStateBlocks->regY;
   cpu.p_reg = NesStateBlocks->regFlags;
   cpu.s_reg = NesStateBlocks->regStack;
   cpu.pc_reg = NesStateBlocks->regPc;

   NESmachine->ppu->ctrl0 = NesStateBlocks->reg2000;
   NESmachine->ppu->ctrl1 = NesStateBlocks->reg2001;

   memcpy(&cpu.mem_page[0][0],NesStateBlocks->cpuRam, 0x800);

   memcpy(&NESmachine->ppu->oam[0], NesStateBlocks->spriteRam, 0x100);
   memcpy(&NESmachine->ppu->nametab[0], NesStateBlocks->ppuRam, 0x1000);
   memcpy(&NESmachine->ppu->palette[0], NesStateBlocks->palette, 0x20);

   NESmachine->ppu->vaddr = NesStateBlocks->vramAddress;
   NESmachine->ppu->oam_addr = NesStateBlocks->spriteRamAddress;
   NESmachine->ppu->tile_xofs = NesStateBlocks->tileXOffset;

   NESmachine->ppu->stat = NesStateBlocks->PPU_stat;
   NESmachine->ppu->vaddr_latch = NesStateBlocks->PPU_vaddr_latch;
   NESmachine->ppu->vaddr_inc = NesStateBlocks->PPU_vaddr_inc;
   NESmachine->ppu->tile_nametab = NesStateBlocks->PPU_tile_nametab;
   NESmachine->ppu->flipflop = NesStateBlocks->PPU_flipflop;

   NESmachine->ppu->obj_height = NesStateBlocks->PPU_obj_height;
   NESmachine->ppu->obj_base = NesStateBlocks->PPU_obj_base;
   NESmachine->ppu->bg_base = NesStateBlocks->PPU_bg_base;
   NESmachine->ppu->bg_on = NesStateBlocks->PPU_bg_on;
   NESmachine->ppu->obj_on = NesStateBlocks->PPU_obj_on;
   NESmachine->ppu->obj_mask = NesStateBlocks->PPU_obj_mask;
   NESmachine->ppu->bg_mask = NesStateBlocks->PPU_bg_mask;
   NESmachine->ppu->latch = NesStateBlocks->PPU_latch;
   NESmachine->ppu->vdata_latch = NesStateBlocks->PPU_vdata_latch;
   NESmachine->ppu->strobe = NesStateBlocks->PPU_strobe;
   NESmachine->ppu->strikeflag = NesStateBlocks->PPU_strikeflag;
   NESmachine->ppu->strike_cycle = NesStateBlocks->PPU_strike_cycle;
   NESmachine->ppu->vram_accessible = NesStateBlocks->PPU_vram_accessible;
   NESmachine->ppu->vram_present = NesStateBlocks->PPU_vram_present;
   NESmachine->ppu->drawsprites = NesStateBlocks->PPU_drawsprites;
  
   // Mask off priority color bits 
   for (uint8_t i = 0; i < 32; i++) NESmachine->ppu->palette[i] = NesStateBlocks->palette[i]; 
 
//--------------------------------------------------------------------------------
//VRAM BLOCK
//--------------------------------------------------------------------------------

   if (NESmachine->rominfo->vram) {
      memcpy(&NESmachine->rominfo->vram[0], NesStateBlocks->vram, NesStateBlocks->vramSize);
   } else {
      printf("NES VRAM not present\n");
   }
//--------------------------------------------------------------------------------
//SRAM BLOCK
//--------------------------------------------------------------------------------

   if (NESmachine->rominfo->sram) {
      //printf("sramSize: %d\n",NesStateBlocks->sramSize);
      memcpy(&NESmachine->rominfo->sram[0], NesStateBlocks->sram, NesStateBlocks->sramSize);     
   } else {
      printf("NES SRAM not present\n");
   }

//--------------------------------------------------------------------------------
//SOUND REGS BLOCK
//--------------------------------------------------------------------------------

   int i;
   for (i = 0; i < 0x16; i++) {
      if (i != 0x14) // do NOT trigger OAM DMA! 
         apu_write(0x4000 + i, NesStateBlocks->soundRegisters[i]);
   }
   
//--------------------------------------------------------------------------------
//MAPPERS BLOCK
//--------------------------------------------------------------------------------

   for (uint8_t i = 0; i < 4; i++) mmc_bankrom(8, 0x8000 + (i * 0x2000), NesStateBlocks->prgPages[i]);
   
   if (NESmachine->rominfo->vrom_banks) {
      for (uint8_t i = 0; i < 8; i++) mmc_bankvrom(1, i * 0x400, NesStateBlocks->chrPages[i]);
   } else {
      for (uint8_t i = 0; i < 8; i++) ppu_setpage(1, i, NESmachine->rominfo->vram);
   }
   if (NESmachine->mmc->intf->set_state)  NESmachine->mmc->intf->set_state(&NesStateBlocks->mapperBlock);

   free(NesStateBlocks);  
   return 1;
}
