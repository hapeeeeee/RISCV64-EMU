#ifndef RVEMU_H
#define RVEMU_H

#include <assert.h>
#include <bits/stdint-uintn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "elfdef.h"
#include "regs.h"
#include "types.h"

#define fatalf(fmt, ...)                                                       \
    (fprintf(                                                                  \
         stderr, "fatal: %s:%d " fmt "\n", __FILE__, __LINE__, __VA_ARGS__     \
     ),                                                                        \
     exit(1))
#define fatal(msg) fatalf("%s", msg)
#define unreachable() (fatal("unreachable"), __builtin_unreachable())

#define ROUNDDOWN(x, k) ((x) & -(k))
#define ROUNDUP(x, k) (((x) + (k) - 1) & -(k))
#define MIN(x, y) ((y) > (x) ? (x) : (y))
#define MAX(x, y) ((y) < (x) ? (x) : (y))

// 被模拟程序的地址可由2种addr表示：
// 自己希望加载的地址(GUEST_ADDR)和实际在rvemu进程中被存放的地址(HOST_ADDR)
// TO_HOST：GUEST_ADDR转HOST_ADDR
// TO_GUEST：HOST_ADDR转GUEST_ADDR
#define GUEST_MEMORY_OFFSET 0x088800000000ULL
#define TO_HOST(addr) (addr + GUEST_MEMORY_OFFSET)
#define TO_GUEST(addr) (addr - GUEST_MEMORY_OFFSET)

#define FORCE_INLINE inline __attribute__((always_inline))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

enum inst_type_t {
    inst_lb,        // 从内存地址(rs1+imm)加载8位有符号数到rd，符号扩展到XLEN
    inst_lh,        // 加载16位有符号数到rd，符号扩展到XLEN
    inst_lw,        // 加载32位有符号数到rd，符号扩展到XLEN
    inst_ld,        // 加载64位数据到rd
    inst_lbu,       // 加载8位无符号数到rd，零扩展到XLEN
    inst_lhu,       // 加载16位无符号数到rd，零扩展到XLEN
    inst_lwu,       // 加载32位无符号数到rd，零扩展到64位
    inst_fence,     // 内存屏障，约束内存读写的可见顺序
    inst_fence_i,   // 指令缓存同步，确保后续能看到已修改的指令
    inst_addi,      // rd = rs1 + imm
    inst_slli,      // rd = rs1 << shamt，逻辑左移
    inst_slti,      // rd = (rs1 < imm)? 1 : 0（有符号比较）
    inst_sltiu,     // rd = (rs1 < imm)? 1 : 0（无符号比较）
    inst_xori,      // rd = rs1 ^ imm
    inst_srli,      // rd = rs1 >> shamt，逻辑右移
    inst_srai,      // rd = rs1 >> shamt，算术右移（保留符号）
    inst_ori,       // rd = rs1 | imm
    inst_andi,      // rd = rs1 & imm
    inst_auipc,     // rd = pc + (imm << 12)
    inst_addiw,     // rd = signext32(rs1 + imm)
    inst_slliw,     // rd = signext32((uint32)rs1 << shamt)
    inst_srliw,     // rd = signext32((uint32)rs1 >> shamt)
    inst_sraiw,     // rd = signext32((int32)rs1 >> shamt)，算术右移
    inst_sb,        // 将rs2低8位写入内存[rs1+imm]
    inst_sh,        // 将rs2低16位写入内存[rs1+imm]
    inst_sw,        // 将rs2低32位写入内存[rs1+imm]
    inst_sd,        // 将rs2的64位写入内存[rs1+imm]
    inst_add,       // rd = rs1 + rs2
    inst_sll,       // rd = rs1 << (rs2低5/6位)，逻辑左移
    inst_slt,       // rd = (rs1 < rs2)? 1 : 0（有符号比较）
    inst_sltu,      // rd = (rs1 < rs2)? 1 : 0（无符号比较）
    inst_xor,       // rd = rs1 ^ rs2
    inst_srl,       // rd = rs1 >> (rs2低5/6位)，逻辑右移
    inst_or,        // rd = rs1 | rs2
    inst_and,       // rd = rs1 & rs2
    inst_mul,       // rd = (rs1 * rs2) 的低XLEN位
    inst_mulh,      // rd = (有符号×有符号)乘积的高XLEN位
    inst_mulhsu,    // rd = (有符号×无符号)乘积的高XLEN位
    inst_mulhu,     // rd = (无符号×无符号)乘积的高XLEN位
    inst_div,       // rd = rs1 / rs2（有符号）
    inst_divu,      // rd = rs1 / rs2（无符号）
    inst_rem,       // rd = rs1 % rs2（有符号）
    inst_remu,      // rd = rs1 % rs2（无符号）
    inst_sub,       // rd = rs1 - rs2
    inst_sra,       // rd = rs1 >> (rs2低5/6位)，算术右移
    inst_lui,       // rd = imm << 12
    inst_addw,      // rd = signext32((int32)rs1 + (int32)rs2)
    inst_sllw,      // rd = signext32((uint32)rs1 << (rs2低5位))
    inst_srlw,      // rd = signext32((uint32)rs1 >> (rs2低5位))
    inst_mulw,      // rd = signext32((int32)rs1 * (int32)rs2)
    inst_divw,      // rd = signext32((int32)rs1 / (int32)rs2)
    inst_divuw,     // rd = signext32((uint32)rs1 / (uint32)rs2)
    inst_remw,      // rd = signext32((int32)rs1 % (int32)rs2)
    inst_remuw,     // rd = signext32((uint32)rs1 % (uint32)rs2)
    inst_subw,      // rd = signext32((int32)rs1 - (int32)rs2)
    inst_sraw,      // rd = signext32((int32)rs1 >> (rs2低5位))，算术右移
    inst_beq,       // 若rs1==rs2，则pc += imm（相对跳转）
    inst_bne,       // 若rs1!=rs2，则pc += imm
    inst_blt,       // 若rs1<rs2（有符号），则pc += imm
    inst_bge,       // 若rs1>=rs2（有符号），则pc += imm
    inst_bltu,      // 若rs1<rs2（无符号），则pc += imm
    inst_bgeu,      // 若rs1>=rs2（无符号），则pc += imm
    inst_jalr,      // rd=pc+4；pc=(rs1+imm)&~1（间接跳转并链接）
    inst_jal,       // rd=pc+4；pc=pc+imm（直接跳转并链接）
    inst_ecall,     // 环境调用，陷入到系统以执行syscall
    inst_csrrc,     // rd=CSR；CSR &= ~rs1（原子清位）
    inst_csrrci,    // rd=CSR；CSR &= ~zimm（立即数清位）
    inst_csrrs,     // rd=CSR；CSR |= rs1（原子置位）
    inst_csrrsi,    // rd=CSR；CSR |= zimm（立即数置位）
    inst_csrrw,     // rd=CSR；CSR = rs1（原子读写）
    inst_csrrwi,    // rd=CSR；CSR = zimm（立即数读写）
    inst_flw,       // 从内存加载32位到浮点寄存器（单精度）
    inst_fsw,       // 将浮点寄存器32位写入内存（单精度）
    inst_fmadd_s,   // 单精度FMA：(rs1*rs2)+rs3
    inst_fmsub_s,   // 单精度FMA：(rs1*rs2)-rs3
    inst_fnmsub_s,  // 单精度FMA：-(rs1*rs2)-rs3
    inst_fnmadd_s,  // 单精度FMA：-(rs1*rs2)+rs3
    inst_fadd_s,    // 单精度浮点加法
    inst_fsub_s,    // 单精度浮点减法
    inst_fmul_s,    // 单精度浮点乘法
    inst_fdiv_s,    // 单精度浮点除法
    inst_fsqrt_s,   // 单精度平方根
    inst_fsgnj_s,   // 单精度符号注入：rd=带rs2符号的rs1
    inst_fsgnjn_s,  // 单精度符号取反注入：rd=带(~rs2符号)的rs1
    inst_fsgnjx_s,  // 单精度符号异或：rd符号=rs1与rs2符号异或
    inst_fmin_s,    // 单精度最小值
    inst_fmax_s,    // 单精度最大值
    inst_fcvt_w_s,  // 单精度转32位有符号整数
    inst_fcvt_wu_s, // 单精度转32位无符号整数
    inst_fmv_x_w,   // 将单精度位模式移动到整数寄存器（不做数值转换）
    inst_feq_s,     // 单精度比较：相等返回1，否则0
    inst_flt_s,     // 单精度比较：小于返回1，否则0
    inst_fle_s,     // 单精度比较：小于等于返回1，否则0
    inst_fclass_s,  // 单精度分类，返回类别位掩码
    inst_fcvt_s_w,  // 32位有符号整数转单精度
    inst_fcvt_s_wu, // 32位无符号整数转单精度
    inst_fmv_w_x,   // 将整数位模式移动到单精度寄存器（不做数值转换）
    inst_fcvt_l_s,  // 单精度转64位有符号整数
    inst_fcvt_lu_s, // 单精度转64位无符号整数
    inst_fcvt_s_l,  // 64位有符号整数转单精度
    inst_fcvt_s_lu, // 64位无符号整数转单精度
    inst_fld,       // 从内存加载64位到浮点寄存器（双精度）
    inst_fsd,       // 将浮点寄存器64位写入内存（双精度）
    inst_fmadd_d,   // 双精度FMA：(rs1*rs2)+rs3
    inst_fmsub_d,   // 双精度FMA：(rs1*rs2)-rs3
    inst_fnmsub_d,  // 双精度FMA：-(rs1*rs2)-rs3
    inst_fnmadd_d,  // 双精度FMA：-(rs1*rs2)+rs3
    inst_fadd_d,    // 双精度浮点加法
    inst_fsub_d,    // 双精度浮点减法
    inst_fmul_d,    // 双精度浮点乘法
    inst_fdiv_d,    // 双精度浮点除法
    inst_fsqrt_d,   // 双精度平方根
    inst_fsgnj_d,   // 双精度符号注入
    inst_fsgnjn_d,  // 双精度符号取反注入
    inst_fsgnjx_d,  // 双精度符号异或注入
    inst_fmin_d,    // 双精度最小值
    inst_fmax_d,    // 双精度最大值
    inst_fcvt_s_d,  // 双精度转单精度
    inst_fcvt_d_s,  // 单精度转双精度
    inst_feq_d,     // 双精度比较：相等返回1，否则0
    inst_flt_d,     // 双精度比较：小于返回1，否则0
    inst_fle_d,     // 双精度比较：小于等于返回1，否则0
    inst_fclass_d,  // 双精度分类，返回类别位掩码
    inst_fcvt_w_d,  // 双精度转32位有符号整数
    inst_fcvt_wu_d, // 双精度转32位无符号整数
    inst_fcvt_d_w,  // 32位有符号整数转双精度
    inst_fcvt_d_wu, // 32位无符号整数转双精度
    inst_fcvt_l_d,  // 双精度转64位有符号整数
    inst_fcvt_lu_d, // 双精度转64位无符号整数
    inst_fmv_x_d,   // 将双精度位模式移动到整数寄存器（不做数值转换）
    inst_fcvt_d_l,  // 64位有符号整数转双精度
    inst_fcvt_d_lu, // 64位无符号整数转双精度
    inst_fmv_d_x,   // 将整数位模式移动到双精度寄存器（不做数值转换）
    num_insns,      // 指令数量计数器（非指令）

    /*
     * 未添加的指令/扩展说明：
     *
     * - A（原子指令扩展）：
     *   lr.w/lr.d, sc.w/sc.d, amoadd.w/d, amoswap.w/d, amoxor.w/d,
     *   amoor.w/d, amomin.w/d, amomax.w/d, amominu.w/d, amomaxu.w/d 等。
     *   说明：当前未在枚举中列出，属于原子内存操作，通常用于并发同步。
     *
     * - 特权指令（Privileged）：
     *   ebreak, uret/sret/mret, wfi, sfence.vma 等。
     *   说明：本工程以用户态程序为主，这些指令涉及特权级控制与页表维护，未纳入枚举。
     *
     * - Zfh（半精度浮点）：
     *   flh/fsh、fcvt.h.*、fcvt.*.h、fmin.h/fmax.h 等半精度相关指令。
     *   说明：当前仅实现 F（单精度）与 D（双精度），未包含半精度扩展。
     *
     * - Zba/Zbb/Zbc/Zbs（位操作与地址计算扩展，俗称“B 扩展”子集）：
     *   如 sh1add/sh2add、andn、orn、xnor、clz/ctz、min/max 等。
     *   说明：这些为常用的性能优化/位运算指令，未在此枚举中出现。
     *
     * - C（压缩指令）：
     *   压缩形式未作为独立枚举项列出，decode 时通过 `inst_t.rvc` 标识压缩形式，
     *   与对应非压缩指令共享语义（如 c.add 映射到 add）。
     *
     * 若后续需要支持上述扩展，请在此枚举与解码/执行路径中补充对应指令并实现。
     */

};

// RISC-V
// 指令格式最多只会有一个目标寄存器（rd）、两个源寄存器（rs1、rs2）、和一个立即数（imm）
//
// 具体说明如下：
// - R 型指令：有 rd、rs1、rs2，没有立即数。
// - I 型指令：有 rd、rs1、imm，没有 rs2。
// - S 型指令：有 rs1、rs2、imm，没有 rd。
// - B 型指令：有 rs1、rs2、imm，没有 rd。
// - U/J 型指令：有 rd、imm，没有 rs1、rs2。
typedef struct {
    int8_t rd;  // destination register，
    int8_t rs1; // source register 1
    int8_t rs2; // source register 2
    int8_t rs3;
    int32_t imm; // 立即数
    int16_t csr; // 字段代表 Control and Status Register （控制与状态寄存器）。
    enum inst_type_t type;
    bool rvc; // RISC-V Compressed 压缩指令
    bool
        continue_exec; // 目前主要处理syscall,
                       // 当遇到continue_exec标识符时,跳出内层循环,到外部循环处理syscall
} inst_t;

// clang-format off
/*
 * mmu.c
 * 模拟器所视内存：
 * [ 模拟器ELF自身运行所占内存 | 被模拟程序的栈空间 | argc argv envp auxv | argv[] ]
 *                         ^ 此处为base, 恒定值                      ^ 此处为被模拟进程的栈底
 **/
// clang-format on
typedef struct {
    uint64_t entry;
    uint64_t
        host_alloc; // ELF 占用区结束位置(malloc动态内存之前)，会随着malloc变化,
                    // 默认对齐page size
    uint64_t
        guest_alloc; // 被模拟进程guest觉得的ELF占用区结束位置，会随着malloc变化,
                     // 不一定对齐page size
    uint64_t
        base; // 永远指向最初的ELF占用区结束位置（不包含堆内存）,在程序加载完毕后不再变动
} mmu_t;

void mmu_load_elf(mmu_t *, int);
uint64_t mmu_alloc(mmu_t *, int64_t);
inline void mmu_write(uint64_t guest_addr, uint8_t *data, size_t len) {
    memcpy((void *)TO_HOST(guest_addr), (void *)data, len);
}

/*
 * decode.c
 **/
void decode_inst(inst_t *, uint32_t);

enum exit_reason_t {
    none,
    direct_branch,
    indirect_branch,
    ecall, // riscv通过ecall触发syscall
};

enum csr_t {
    fflags = 0x001,
    frm = 0x002,
    fcsr = 0x003,
};

/*
 * state.c
 **/
typedef struct {
    enum exit_reason_t exit_reason;
    uint64_t reenter_pc;           // block切换时,下一个block的起始pc
    uint64_t gp_regs[num_gp_regs]; // 32个通用寄存器
    fp_reg_t fp_regs[num_fp_regs]; // 32个浮点寄存器
    uint64_t pc;
} state_t;

/*
 * interp.c
 **/
void exec_block_interp(state_t *state);

/*
 * machine.c
 **/
typedef struct {
    state_t state;
    mmu_t mmu;
} machine_t;

FORCE_INLINE uint64_t machine_get_gp_reg(machine_t *m, int32_t reg) {
    assert(reg >= 0 && reg < num_gp_regs);
    return m->state.gp_regs[reg];
}

FORCE_INLINE void machine_set_gp_reg(machine_t *m, int32_t reg, uint64_t data) {
    assert(reg >= 0 && reg < num_gp_regs);
    m->state.gp_regs[reg] = data;
}

void machine_load_program(machine_t *, char *);
void machine_setup(machine_t *, int, char **);
enum exit_reason_t machine_step(machine_t *);
#endif

/*
 * syscall.c
 */
uint64_t do_syscall(machine_t *, uint64_t);