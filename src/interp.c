#include "interp_util.h"
#include "rvemu.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static void func_empty(state_t *state, inst_t *inst) {}

#define FUNC(ty)                                                               \
    uint64_t addr = state->gp_regs[inst->rs1] + (int64_t)inst->imm;            \
    uint64_t val = *(ty *)TO_HOST(addr);                                       \
    state->gp_regs[inst->rd] = val;                                            \
    if (inst->rd == 1 && val == 0) {                                           \
        fprintf(                                                               \
            stderr,                                                            \
            "DEBUG: ra loaded with 0 from addr %lx (pc=%lx)\n",                \
            addr,                                                              \
            state->pc                                                          \
        );                                                                     \
    }

/*
 *  lb: load byte, 从内存地址(rs1+imm)加载8位有符号数到rd，符号扩展到XLEN
 *  lb a1, 4(a0) <=> a1 = (i8)mem[a0 + 4]
 */
static void func_lb(state_t *state, inst_t *inst) { FUNC(int8_t); }

/*
 *  lh: load high xxxx, 从内存地址(rs1+imm)加载16位有符号数到rd，符号扩展到XLEN
 *  lh a1, 4(a0) <=> a1 = (i16)mem[a0 + 4]
 */
static void func_lh(state_t *state, inst_t *inst) { FUNC(int16_t); }

/*
 *  lb: load word, 从内存地址(rs1+imm)加载8位有符号数到rd，符号扩展到XLEN
 *  lb a1, 4(a0) <=> a1 = (int32_t)mem[a0 + 4]
 */
static void func_lw(state_t *state, inst_t *inst) { FUNC(int32_t); }

// 加载64位数据到rd
static void func_ld(state_t *state, inst_t *inst) { FUNC(int64_t); }

// 加载8位无符号数到rd，零扩展到XLEN
static void func_lbu(state_t *state, inst_t *inst) { FUNC(uint8_t); }

// 加载16位无符号数到rd，零扩展到XLEN
static void func_lhu(state_t *state, inst_t *inst) { FUNC(uint16_t); }

// 加载32位无符号数到rd，零扩展到64位
static void func_lwu(state_t *state, inst_t *inst) { FUNC(uint32_t); }

#undef FUNC

#define FUNC(expr)                                                             \
    uint64_t rs1 = state->gp_regs[inst->rs1];                                  \
    int64_t imm = (int64_t)inst->imm;                                          \
    uint64_t res = (expr);                                                     \
    state->gp_regs[inst->rd] = res;                                            \
    if (inst->rd == 1 && res == 0) {                                           \
        fprintf(stderr, "DEBUG: ra set to 0 by ALU at pc=%lx\n", state->pc);   \
    }

// 将寄存器 rs1 的值与立即数 imm 相加，结果写入 rd。
static void func_addi(state_t *state, inst_t *inst) { FUNC(rs1 + imm); }

// 将 rs1 的值左移 imm 位（逻辑左移），结果写入 rd。
static void func_slli(state_t *state, inst_t *inst) {
    // rs1在RV64中只有64位, 因此imm只取最低6位,就能表示0-63
    FUNC(rs1 << (imm & 0x3f));
}

// 如果 rs1 < imm（有符号比较），则 rd = 1，否则 rd = 0。
static void func_slti(state_t *state, inst_t *inst) {
    FUNC((int64_t)rs1 < (int64_t)imm);
}

// 如果 rs1 < imm（无符号比较），则 rd = 1，否则 rd = 0。
static void func_sltiu(state_t *state, inst_t *inst) {
    FUNC((uint64_t)rs1 < (uint64_t)imm);
}

// 将 rs1 与 imm 做按位异或，结果写入 rd
static void func_xori(state_t *state, inst_t *inst) { FUNC(rs1 ^ imm); }

// 将 rs1 逻辑右移 imm 位，结果写入 rd。
static void func_srli(state_t *state, inst_t *inst) {
    // rs1在RV64中只有64位, 因此imm只取最低6位,就能表示0-63
    FUNC(rs1 >> (imm & 0x3f));
}

// 将 rs1 算术右移 imm 位（保留符号位），结果写入 rd。
static void func_srai(state_t *state, inst_t *inst) {
    FUNC((int64_t)rs1 >> (imm & 0x3f)); // ?
}

// 将 rs1 与 imm 做按位或，结果写入 rd。
static void func_ori(state_t *state, inst_t *inst) {
    FUNC(rs1 | (uint64_t)imm);
}

// 将 rs1 与 imm 做按位与，结果写入 rd。
static void func_andi(state_t *state, inst_t *inst) {
    FUNC(rs1 & (uint64_t)imm);
}

// 将 rs1 左移 imm 位，结果截断为 32 位后再扩展为 64 位，写入 rd。
static void func_addiw(state_t *state, inst_t *inst) {
    FUNC((int64_t)(int32_t)(rs1 + imm)); // 为什么要先截断再转成64位?
}

// 将 rs1 与 imm 相加，结果截断为 32 位后再符号扩展为 64 位，写入 rd。
static void func_slliw(state_t *state, inst_t *inst) {
    // w: word(4字节, 32位),以w结尾的表示操作32位数据
    // 32位只需要5位就能表示0-31
    FUNC((int64_t)(int32_t)(rs1 << (imm & 0x1f)));
}

// 将 rs1 逻辑右移 imm 位，结果截断为 32 位后再符号扩展为 64 位，写入 rd。
static void func_srliw(state_t *state, inst_t *inst) {
    // w: word(4字节, 32位),以w结尾的表示操作32位数据
    // 32位只需要5位就能表示0-31
    FUNC((int64_t)(int32_t)((uint32_t)rs1 >> (imm & 0x1f)));
}

// 将 rs1 算术右移 imm 位，结果截断为 32 位后再扩展为 64 位，写入 rd。
static void func_sraiw(state_t *state, inst_t *inst) {
    FUNC((int64_t)((int32_t)rs1 >> (imm & 0x1f))); // 为什么这里只取最后5位?
}

#undef FUNC

// 将当前 PC（程序计数器）与立即数相加，结果写入 rd 寄存器。
static void func_auipc(state_t *state, inst_t *inst) {
    uint64_t val = state->pc + (int64_t)inst->imm;
    state->gp_regs[inst->rd] = val;
}

#define FUNC(typ)                                                              \
    uint64_t rs1 = state->gp_regs[inst->rs1];                                  \
    uint64_t rs2 = state->gp_regs[inst->rs2];                                  \
    uint64_t addr = rs1 + inst->imm;                                           \
    *(typ *)TO_HOST(addr) = (typ)rs2;                                          \
    if (addr == 0x201bc30) {                                                   \
        fprintf(                                                               \
            stderr,                                                            \
            "DEBUG: store to %lx val=%lx at pc=%lx\n",                         \
            addr,                                                              \
            rs2,                                                               \
            state->pc                                                          \
        );                                                                     \
    }

// 将 rs2 寄存器的最低 8 位（1 字节）存储到内存地址 rs1 + imm 处。
static void func_sb(state_t *state, inst_t *inst) { FUNC(uint8_t); }

// 将 rs2 寄存器的最低 16 位（2 字节）存储到内存地址 rs1 + imm 处。
static void func_sh(state_t *state, inst_t *inst) { FUNC(uint16_t); }

// ：将 rs2 寄存器的最低 32 位（4 字节）存储到内存地址 rs1 + imm 处。
static void func_sw(state_t *state, inst_t *inst) { FUNC(uint32_t); }

// 将 rs2 寄存器的 64 位（8 字节）存储到内存地址 rs1 + imm 处。
static void func_sd(state_t *state, inst_t *inst) { FUNC(uint64_t); }

#undef FUNC

#define FUNC(expr)                                                             \
    uint64_t rs1 = state->gp_regs[inst->rs1];                                  \
    uint64_t rs2 = state->gp_regs[inst->rs2];                                  \
    state->gp_regs[inst->rd] = (expr);

// 将 rs1 和 rs2 相加，结果写入 rd。
static void func_add(state_t *state, inst_t *inst) { FUNC(rs1 + rs2); }

// 将 rs1 逻辑左移 rs2 的低 6 位（0~63），结果写入 rd。
static void func_sll(state_t *state, inst_t *inst) {
    FUNC(rs1 << (rs2 & 0x3f));
}

// 如果 rs1 < rs2（有符号比较），rd = 1，否则 rd = 0
static void func_slt(state_t *state, inst_t *inst) {
    FUNC((int64_t)rs1 < (int64_t)rs2);
}

// 如果 rs1 < rs2（无符号比较），rd = 1，否则 rd = 0。
static void func_sltu(state_t *state, inst_t *inst) {
    FUNC((uint64_t)rs1 < (uint64_t)rs2);
}

static void func_xor(state_t *state, inst_t *inst) { FUNC(rs1 ^ rs2); }

// 将 rs1 逻辑右移 rs2 的低 6 位（0~63），结果写入 rd。
static void func_srl(state_t *state, inst_t *inst) {
    FUNC(rs1 >> (rs2 & 0x3f));
}

// 将 rs1 和 rs2 按位或，结果写入 rd。
static void func_or(state_t *state, inst_t *inst) { FUNC(rs1 | rs2); }

// 将 rs1 和 rs2 按位与，结果写入 rd。
static void func_and(state_t *state, inst_t *inst) { FUNC(rs1 & rs2); }

// 将 rs1 和 rs2 相乘，结果写入 rd（低 64 位）。
static void func_mul(state_t *state, inst_t *inst) { FUNC(rs1 * rs2); }

// Multiply High (signed × signed)
// 将 rs1 和 rs2 有符号相乘，结果的高 64 位写入 rd。
static void func_mulh(state_t *state, inst_t *inst) { FUNC(mulh(rs1, rs2)); }

// Multiply High Signed * Unsigned
// rs1 有符号，rs2 无符号相乘，高 64 位写入 rd。
static void func_mulhsu(state_t *state, inst_t *inst) {
    FUNC(mulhsu(rs1, rs2));
}

// Multiply High Unsigned
// rs1 和 rs2 无符号相乘，高 64 位写入 rd。
static void func_mulhu(state_t *state, inst_t *inst) { FUNC(mulhu(rs1, rs2)); }

// 将 rs1 和 rs2 相减，结果写入 rd。
static void func_sub(state_t *state, inst_t *inst) { FUNC(rs1 - rs2); }

// 将 rs1 算术右移 rs2 的低 6 位（0~63），结果写入 rd
static void func_sra(state_t *state, inst_t *inst) {
    FUNC((int64_t)rs1 >> (rs2 & 0x3f));
}

// rs1 除以 rs2 的无符号余数，写入 rd。rs2 为 0 时，rd = rs1。
static void func_remu(state_t *state, inst_t *inst) {
    FUNC(rs2 == 0 ? rs1 : rs1 % rs2);
}

// rs1 和 rs2 相加，结果截断为 32 位后符号扩展为 64 位，写入 rd。
static void func_addw(state_t *state, inst_t *inst) {
    FUNC((int64_t)(int32_t)(rs1 + rs2));
}

// rs1 左移 rs2 的低 5 位（0~31），结果截断为 32 位后符号扩展为 64 位，写入 rd。
static void func_sllw(state_t *state, inst_t *inst) {
    FUNC((int64_t)(int32_t)(rs1 << (rs2 & 0x1f)));
}

// rs1 逻辑右移 rs2 的低 5 位，结果截断为 32 位后符号扩展为 64 位，写入 rd。
static void func_srlw(state_t *state, inst_t *inst) {
    FUNC((int64_t)(int32_t)((uint32_t)rs1 >> (rs2 & 0x1f)));
}

// rs1 和 rs2 相乘，结果截断为 32 位后符号扩展为 64 位，写入 rd。
static void func_mulw(state_t *state, inst_t *inst) {
    FUNC((int64_t)(int32_t)(rs1 * rs2));
}

// s1 和 rs2 有符号除法，结果截断为 32 位后符号扩展为 64 位，写入 rd。rs2 为 0
// 时，rd = UINT64_MAX。
static void func_divw(state_t *state, inst_t *inst) {
    FUNC(
        rs2 == 0 ? UINT64_MAX
                 : (int32_t)((int64_t)(int32_t)rs1 / (int64_t)(int32_t)rs2)
    );
}

// rs1 和 rs2 无符号除法，结果截断为 32 位后符号扩展为 64 位，写入 rd。rs2 为 0
// 时，rd = UINT64_MAX。
static void func_divuw(state_t *state, inst_t *inst) {
    FUNC(rs2 == 0 ? UINT64_MAX : (int32_t)((uint32_t)rs1 / (uint32_t)rs2));
}

// rs1 和 rs2 有符号余数，结果截断为 32 位后符号扩展为 64 位，写入 rd。rs2 为 0
// 时，rd = rs1（截断为 32 位）。
static void func_remw(state_t *state, inst_t *inst) {
    FUNC(
        rs2 == 0
            ? (int64_t)(int32_t)rs1
            : (int64_t)(int32_t)((int64_t)(int32_t)rs1 % (int64_t)(int32_t)rs2)
    );
}

// rs1 和 rs2 无符号余数，结果截断为 32 位后符号扩展为 64 位，写入 rd。rs2
// 为 0 时，rd = rs1（截断为 32 位）。
static void func_remuw(state_t *state, inst_t *inst) {
    FUNC(
        rs2 == 0 ? (int64_t)(int32_t)(uint32_t)rs1
                 : (int64_t)(int32_t)((uint32_t)rs1 % (uint32_t)rs2)
    );
}

// rs1 和 rs2 相减，结果截断为 32 位后符号扩展为 64 位，写入 rd。
static void func_subw(state_t *state, inst_t *inst) {
    FUNC((int64_t)(int32_t)(rs1 - rs2));
}

// rs1 算术右移 rs2 的低 5 位，结果截断为 32 位后符号扩展为 64 位，写入 rd。
static void func_sraw(state_t *state, inst_t *inst) {
    FUNC((int64_t)(int32_t)((int32_t)rs1 >> (rs2 & 0x1f)));
}

#undef FUNC

// 对 rs1 和 rs2 做有符号除法，结果写入 rd。
// - rs2 为 0，rd = UINT64_MAX（RISC-V 规定除以 0 时结果为 -1，即所有位为 1）。
// - rs1 为最小负数且 rs2 为 -1（即溢出情况），rd = INT64_MIN（RISC-V 规定）。
// - 其他情况，rd = (int64_t)rs1 / (int64_t)rs2
static void func_div(state_t *state, inst_t *inst) {
    uint64_t rs1 = state->gp_regs[inst->rs1];
    uint64_t rs2 = state->gp_regs[inst->rs2];
    uint64_t rd = 0;
    if (rs2 == 0) {
        rd = UINT64_MAX;
    } else if (rs1 == INT64_MIN && rs2 == UINT64_MAX) {
        // UINT64_MAX解释为有符号数是-1
        rd = INT64_MIN;
    } else {
        rd = (int64_t)rs1 / (int64_t)rs2;
    }
    state->gp_regs[inst->rd] = rd;
}

// 对 rs1 和 rs2 做无符号除法，结果写入 rd。
// - 如果 rs2 为 0，rd = UINT64_MAX（RISC-V 规定除以 0 时结果为最大无符号数）。
// - 其他情况，rd = rs1 / rs2（无符号除法）。
static void func_divu(state_t *state, inst_t *inst) {
    uint64_t rs1 = state->gp_regs[inst->rs1];
    uint64_t rs2 = state->gp_regs[inst->rs2];
    uint64_t rd = 0;
    if (rs2 == 0) {
        rd = UINT64_MAX;
    } else {
        rd = rs1 / rs2;
    }
    state->gp_regs[inst->rd] = rd;
}

// 对 rs1 和 rs2 做有符号取余，结果写入 rd。
// - 如果 rs2 为 0，rd = rs1（RISC-V 规定余数为被除数）。
// - 如果 rs1 为最小负数且 rs2 为 -1，rd = 0（RISC-V 规定）。
// - 其他情况，rd = (int64_t)rs1 % (int64_t)rs2。
static void func_rem(state_t *state, inst_t *inst) {
    uint64_t rs1 = state->gp_regs[inst->rs1];
    uint64_t rs2 = state->gp_regs[inst->rs2];
    uint64_t rd = 0;
    if (rs2 == 0) {
        rd = rs1;
    } else if (rs1 == INT64_MIN && rs2 == UINT64_MAX) {
        rd = 0;
    } else {
        rd = (int64_t)rs1 % (int64_t)rs2;
    }
    state->gp_regs[inst->rd] = rd;
}

// 将立即数 imm（通常是高 20 位）写入 rd 寄存器的高位，低位补零。
// - 在你的实现中，直接将 imm 写入 rd，模拟器可能已做了符号扩展处理。
static void func_lui(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] = (int64_t)inst->imm;
}

#define FUNC(expr)                                                             \
    uint64_t rs1 = state->gp_regs[inst->rs1];                                  \
    uint64_t rs2 = state->gp_regs[inst->rs2];                                  \
    uint64_t target_addr = state->pc + (int64_t)inst->imm;                     \
    if (expr) {                                                                \
        state->reenter_pc = state->pc = target_addr;                           \
        state->exit_reason = direct_branch;                                    \
        inst->continue_exec = true;                                            \
    }

// 如果 rs1 等于 rs2，则跳转到目标地址（ pc + imm ）。
// 用于条件跳转，判断两个寄存器是否相等。
static void func_beq(state_t *state, inst_t *inst) {
    FUNC((uint64_t)rs1 == (uint64_t)rs2);
}

// 如果 rs1 不等于 rs2，则跳转到目标地址。
// 用于条件跳转，判断两个寄存器是否不相等。
static void func_bne(state_t *state, inst_t *inst) {
    FUNC((uint64_t)rs1 != (uint64_t)rs2);
}

// 如果 rs1 小于 rs2（有符号比较），则跳转到目标地址。
// 用于条件跳转，判断 rs1 是否小于 rs2（有符号）。
static void func_blt(state_t *state, inst_t *inst) {
    FUNC((int64_t)rs1 < (int64_t)rs2);
}

// 如果 rs1 大于等于 rs2（有符号比较），则跳转到目标地址。
// 用于条件跳转，判断 rs1 是否大于等于 rs2（有符号）。
static void func_bge(state_t *state, inst_t *inst) {
    FUNC((int64_t)rs1 >= (int64_t)rs2);
}

// 如果 rs1 小于 rs2（无符号比较），则跳转到目标地址。
// 用于条件跳转，判断 rs1 是否小于 rs2（无符号）。
static void func_bltu(state_t *state, inst_t *inst) {
    FUNC((uint64_t)rs1 < (uint64_t)rs2);
}

// 如果 rs1 大于等于 rs2（无符号比较），则跳转到目标地址。
// 用于条件跳转，判断 rs1 是否大于等于 rs2（无符号）。
static void func_bgeu(state_t *state, inst_t *inst) {
    FUNC((uint64_t)rs1 >= (uint64_t)rs2);
}

#undef FUNC

// 将下一条指令的地址（，取决于是否压缩指令）写入 rd寄存器（用于返回）。
// 跳转到 rs1 + imm的地址，并将最低位清零（保证跳转地址是偶数）。
// 用于实现函数调用、间接跳转等。
static void func_jalr(state_t *state, inst_t *inst) {
    uint64_t rs1 = state->gp_regs[inst->rs1];
    state->gp_regs[inst->rd] = state->pc + (inst->rvc ? 2 : 4);
    state->exit_reason = indirect_branch;
    state->reenter_pc = (rs1 + (int64_t)inst->imm) & ~(uint64_t)1;
    if (state->reenter_pc == 0) {
        uint8_t *p = (uint8_t *)TO_HOST(state->pc);
        fprintf(
            stderr,
            "DEBUG: jalr to 0 at pc=%lx. rs1_reg=%d rs1_val=%lx, imm=%d, "
            "sp=%lx\n",
            state->pc,
            inst->rs1,
            rs1,
            (int32_t)inst->imm,
            state->gp_regs[2]
        );
        fprintf(
            stderr, "Inst bytes: %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3]
        );
    }
}

// 将下一条指令的地址（ pc + 4 或 pc + 2 ）写入 rd 寄存器（用于返回）。
// 跳转到 pc + imm 的目标地址。
// 用于实现直接跳转和函数调用。
static void func_jal(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] = state->pc + (inst->rvc ? 2 : 4);
    state->reenter_pc = state->pc = state->pc + (int64_t)inst->imm;
    state->exit_reason = direct_branch;
}

// 用于用户程序向操作系统或模拟器发起系统调用请求。
// 设置 exit_reason 为 ecall ，并将 reenter_pc 设置为下一条指令地址pc + 4
// 表示系统调用后返回的位置。
static void func_ecall(state_t *state, inst_t *inst) {
    state->exit_reason = ecall;
    state->reenter_pc = state->pc + 4;
}

#define FUNC()                                                                 \
    switch (inst->csr) {                                                       \
    case fflags:                                                               \
    case frm:                                                                  \
    case fcsr:                                                                 \
        break;                                                                 \
    default:                                                                   \
        fatal("unsupported csr");                                              \
    }                                                                          \
    state->gp_regs[inst->rd] = 0;

// 功能 ：将 CSR 的值读到 rd，同时用 rs1 的值写入 CSR。
// 用途 ：原子地交换寄存器和 CSR 的值。
static void func_csrrw(state_t *state, inst_t *inst) { FUNC(); }

// 功能 ：将 CSR 的值读到 rd，同时用 rs1 的值按位或到 CSR（设置指定的位）。
// 用途 ：用于设置 CSR 的某些位（比如打开某些功能）
static void func_csrrs(state_t *state, inst_t *inst) { FUNC(); }

// 功能 ：将 CSR 的值读到 rd，同时用 rs1 的值按位清零到 CSR（清除指定的位）。
// 用途 ：用于清除 CSR 的某些位（比如关闭某些功能）。
static void func_csrrc(state_t *state, inst_t *inst) { FUNC(); }

// 功能:与 CSRRW 类似，但写入 CSR 的值是一个立即数（imm），而不是寄存器 rs1。
// 用途 ：更高效地用常量配置 CSR。
static void func_csrrwi(state_t *state, inst_t *inst) { FUNC(); }

// - 功能 ：与 CSRRS 类似，但设置的位是立即数（imm）。
// - 用途 ：更高效地用常量设置 CSR 的某些位。
static void func_csrrsi(state_t *state, inst_t *inst) { FUNC(); }

// - 功能 ：与 CSRRC 类似，但清除的位是立即数（imm）。
// - 用途 ：更高效地用常量清除 CSR 的某些位。
static void func_csrrci(state_t *state, inst_t *inst) { FUNC(); }

#undef FUNC

// 从内存地址 rs1 + imm 处读取 32 位（单精度 float）数据，写入浮点寄存器 rd 。
// 你的实现中还做了高位填充（ | ((uint64_t)-1 << 32) ），可能是为了模拟 NaN
// 或特殊值，但标准行为是只写低 32 位。
static void func_flw(state_t *state, inst_t *inst) {
    uint64_t addr = state->gp_regs[inst->rs1] + (int64_t)inst->imm;

    // ((uint64_t)-1 << 32): 这是一个 64 位数，高 32 位全为 1，低 32 位为 0。
    state->fp_regs[inst->rd].v =
        *(uint32_t *)TO_HOST(addr) | ((uint64_t)-1 << 32);
}

// 从内存地址 rs1 + imm 处读取 64 位（双精度 double）数据，写入浮点寄存器rd
static void func_fld(state_t *state, inst_t *inst) {
    uint64_t addr = state->gp_regs[inst->rs1] + (int64_t)inst->imm;
    state->fp_regs[inst->rd].v = *(uint64_t *)TO_HOST(addr);
}

#define FUNC(typ)                                                              \
    uint64_t rs1 = state->gp_regs[inst->rs1];                                  \
    uint64_t rs2 = state->fp_regs[inst->rs2].v;                                \
    *(typ *)TO_HOST(rs1 + inst->imm) = (typ)rs2;

// 将浮点寄存器 rs2 的 32 位（单精度 float）数据，存储到内存地址 rs1 + imm 处。
static void func_fsw(state_t *state, inst_t *inst) { FUNC(uint32_t); }
// 将浮点寄存器 rs2 的 64 位（双精度 double）数据，存储到内存地址 rs1 + imm 处。
static void func_fsd(state_t *state, inst_t *inst) { FUNC(uint64_t); }

#undef FUNC

#define FUNC(expr)                                                             \
    f32 rs1 = state->fp_regs[inst->rs1].f;                                     \
    f32 rs2 = state->fp_regs[inst->rs2].f;                                     \
    f32 rs3 = state->fp_regs[inst->rs3].f;                                     \
    state->fp_regs[inst->rd].f = (f32)(expr);

// 执行浮点乘加运算： rd = rs1 * rs2 + rs3 （单精度 float）。
static void func_fmadd_s(state_t *state, inst_t *inst) {
    FUNC(rs1 * rs2 + rs3);
}

// 执行浮点乘减运算： rd = rs1 * rs2 - rs3 （单精度 float）。
static void func_fmsub_s(state_t *state, inst_t *inst) {
    FUNC(rs1 * rs2 - rs3);
}

// 执行负乘减运算： rd = -((rs1 * rs2) - rs3) （单精度 float）。
static void func_fnmsub_s(state_t *state, inst_t *inst) {
    FUNC(-(rs1 * rs2) + rs3);
}

// 执行负乘加运算： rd = -((rs1 * rs2) + rs3) （单精度 float）。
static void func_fnmadd_s(state_t *state, inst_t *inst) {
    FUNC(-(rs1 * rs2) - rs3);
}

#undef FUNC

#define FUNC(expr)                                                             \
    f64 rs1 = state->fp_regs[inst->rs1].d;                                     \
    f64 rs2 = state->fp_regs[inst->rs2].d;                                     \
    f64 rs3 = state->fp_regs[inst->rs3].d;                                     \
    state->fp_regs[inst->rd].d = (expr);

// 执行浮点乘加运算： rd = rs1 * rs2 + rs3 （双精度 double）。
static void func_fmadd_d(state_t *state, inst_t *inst) {
    FUNC(rs1 * rs2 + rs3);
}

// 执行浮点乘减运算： rd = rs1 * rs2 - rs3（双精度 double）。
static void func_fmsub_d(state_t *state, inst_t *inst) {
    FUNC(rs1 * rs2 - rs3);
}

// 执行负乘减运算： rd = -((rs1 * rs2) - rs3) （双精度 double）。
static void func_fnmsub_d(state_t *state, inst_t *inst) {
    FUNC(-(rs1 * rs2) + rs3);
}

// 执行负乘加运算： rd = -((rs1 * rs2) + rs3) （双精度 double）。
static void func_fnmadd_d(state_t *state, inst_t *inst) {
    FUNC(-(rs1 * rs2) - rs3);
}

#undef FUNC

#define FUNC(expr)                                                             \
    f32 rs1 = state->fp_regs[inst->rs1].f;                                     \
    __attribute__((unused)) f32 rs2 = state->fp_regs[inst->rs2].f;             \
    state->fp_regs[inst->rd].f = (f32)(expr);

static void func_fadd_s(state_t *state, inst_t *inst) {
    // 此处用x86的加法模拟了RV64的加法, 大部分情况下是可以工作的
    // 但在一些特殊情况下 是不对的.
    // 可用softfloat库中的fadd(rs1, rs2)
    FUNC(rs1 + rs2);
}

static void func_fsub_s(state_t *state, inst_t *inst) { FUNC(rs1 - rs2); }

static void func_fmul_s(state_t *state, inst_t *inst) { FUNC(rs1 * rs2); }

static void func_fdiv_s(state_t *state, inst_t *inst) { FUNC(rs1 / rs2); }

static void func_fsqrt_s(state_t *state, inst_t *inst) { FUNC(sqrtf(rs1)); }

static void func_fmin_s(state_t *state, inst_t *inst) {
    FUNC(rs1 < rs2 ? rs1 : rs2);
}
static void func_fmax_s(state_t *state, inst_t *inst) {
    FUNC(rs1 > rs2 ? rs1 : rs2);
}

#undef FUNC

#define FUNC(expr)                                                             \
    f64 rs1 = state->fp_regs[inst->rs1].d;                                     \
    __attribute__((unused)) f64 rs2 = state->fp_regs[inst->rs2].d;             \
    state->fp_regs[inst->rd].d = (expr);

static void func_fadd_d(state_t *state, inst_t *inst) { FUNC(rs1 + rs2); }

static void func_fsub_d(state_t *state, inst_t *inst) { FUNC(rs1 - rs2); }

static void func_fmul_d(state_t *state, inst_t *inst) { FUNC(rs1 * rs2); }

static void func_fdiv_d(state_t *state, inst_t *inst) { FUNC(rs1 / rs2); }

static void func_fsqrt_d(state_t *state, inst_t *inst) { FUNC(sqrt(rs1)); }

static void func_fmin_d(state_t *state, inst_t *inst) {
    FUNC(rs1 < rs2 ? rs1 : rs2);
}

static void func_fmax_d(state_t *state, inst_t *inst) {
    FUNC(rs1 > rs2 ? rs1 : rs2);
}

#undef FUNC

#define FUNC(n, x)                                                             \
    uint32_t rs1 = state->fp_regs[inst->rs1].w;                                \
    uint32_t rs2 = state->fp_regs[inst->rs2].w;                                \
    state->fp_regs[inst->rd].v =                                               \
        (uint64_t)fsgnj32(rs1, rs2, n, x) | ((uint64_t)-1 << 32);

// 作用: 将 rs2 的符号位注入到 rs1, 其余位保持 rs1 原值。
// 即rd = rs1, 但符号位取自 rs2 。
static void func_fsgnj_s(state_t *state, inst_t *inst) { FUNC(false, false); }

// 作用: 将 rs2 的符号位取反后注入到 rs1, 其余位保持 rs1 原值。
// 即: rd = rs1, 但符号位取自 ~rs2 。
static void func_fsgnjn_s(state_t *state, inst_t *inst) { FUNC(true, false); }

// 作用: 将 rs1 和 rs2 的符号位异或后注入到 rs1, 其余位保持 rs1 原值。
// 即: rd = rs1, 但符号位为 rs1.sign ^ rs2.sign
static void func_fsgnjx_s(state_t *state, inst_t *inst) { FUNC(false, true); }

#undef FUNC

#define FUNC(n, x)                                                             \
    uint64_t rs1 = state->fp_regs[inst->rs1].v;                                \
    uint64_t rs2 = state->fp_regs[inst->rs2].v;                                \
    state->fp_regs[inst->rd].v = fsgnj64(rs1, rs2, n, x);

static void func_fsgnj_d(state_t *state, inst_t *inst) { FUNC(false, false); }
static void func_fsgnjn_d(state_t *state, inst_t *inst) { FUNC(true, false); }
static void func_fsgnjx_d(state_t *state, inst_t *inst) { FUNC(false, true); }

#undef FUNC

// Float ConVert To Word from Single
// 将单精度浮点数（float）转换为有符号 32 位整数（int32），
// 再符号扩展为 64 位，写入整数寄存器 rd。
// 典型用途 ：浮点转整数，带舍入，溢出时按 RISC-V 规范处理。
static void func_fcvt_w_s(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] =
        (int64_t)(int32_t)llrintf(state->fp_regs[inst->rs1].f);
}

// Float Convert To Word Unsigned from Single
// 将单精度浮点数转换为无符号 32 位整数（uint32），
// 再扩展为 64 位，写入 rd。
// 典型用途 ：浮点转无符号整数，带舍入。
static void func_fcvt_wu_s(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] =
        (int64_t)(int32_t)(uint32_t)llrintf(state->fp_regs[inst->rs1].f);
}

// Float ConVert To Word from Double
// 将双精度浮点数（double）转换为有符号 32 位整数（int32），
// 再符号扩展为 64 位，写入 rd。
static void func_fcvt_w_d(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] =
        (int64_t)(int32_t)llrint(state->fp_regs[inst->rs1].d);
}

// Float ConVert To Word Unsigned from Double
// 将双精度浮点数（double）转换为无符号 32 位整数（int32），
// 再符号扩展为 64 位，写入 rd。
static void func_fcvt_wu_d(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] =
        (int64_t)(int32_t)(uint32_t)llrint(state->fp_regs[inst->rs1].d);
}

// Float ConVert To Single from Word
// 将有符号 32 位整数（int32）转换为单精度浮点数，写入浮点寄存器 rd。
static void func_fcvt_s_w(state_t *state, inst_t *inst) {
    state->fp_regs[inst->rd].f = (f32)(int32_t)state->gp_regs[inst->rs1];
}

// Float ConVert To Single from Word Unsigned
// 将无符号 32 位整数（int32）转换为单精度浮点数，写入浮点寄存器 rd。
static void func_fcvt_s_wu(state_t *state, inst_t *inst) {
    state->fp_regs[inst->rd].f = (f32)(uint32_t)state->gp_regs[inst->rs1];
}

// Float ConVert To Double from Word
// 将有符号 32 位整数（int32）转换为双精度浮点数，写入浮点寄存器 rd。
static void func_fcvt_d_w(state_t *state, inst_t *inst) {
    state->fp_regs[inst->rd].d = (f64)(int32_t)state->gp_regs[inst->rs1];
}

// Float ConVert To Double from Word Unsigned
// 将无符号 32 位整数（int32）转换为双精度浮点数，写入浮点寄存器 rd。
static void func_fcvt_d_wu(state_t *state, inst_t *inst) {
    state->fp_regs[inst->rd].d = (f64)(uint32_t)state->gp_regs[inst->rs1];
}

// Float Move to Integer from Single
// 将单精度浮点数的位模式（32 位）直接移动到整数寄存器 rd（符号扩展为 64 位）。
// X （整数寄存器）, S （Single，单精度浮点数）
static void func_fmv_x_w(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] = (int64_t)(int32_t)state->fp_regs[inst->rs1].w;
}

// Float Move to Word from 整数寄存器
// 将整数寄存器的低 32 位位模式直接移动到浮点寄存器 rd（单精度）。
static void func_fmv_w_x(state_t *state, inst_t *inst) {
    state->fp_regs[inst->rd].w = (uint32_t)state->gp_regs[inst->rs1];
}

// 将双精度浮点数的位模式（64 位）直接移动到整数寄存器 rd。
static void func_fmv_x_d(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] = state->fp_regs[inst->rs1].v;
}

// 将整数寄存器 rs 直接移动到双精度浮点数的位模式（64 位）。
static void func_fmv_d_x(state_t *state, inst_t *inst) {
    state->fp_regs[inst->rd].v = state->gp_regs[inst->rs1];
}

#define FUNC(expr)                                                             \
    f32 rs1 = state->fp_regs[inst->rs1].f;                                     \
    f32 rs2 = state->fp_regs[inst->rs2].f;                                     \
    state->gp_regs[inst->rd] = (expr);

static void func_feq_s(state_t *state, inst_t *inst) { FUNC(rs1 == rs2); }

static void func_flt_s(state_t *state, inst_t *inst) { FUNC(rs1 < rs2); }

static void func_fle_s(state_t *state, inst_t *inst) { FUNC(rs1 <= rs2); }

#undef FUNC

#define FUNC(expr)                                                             \
    f64 rs1 = state->fp_regs[inst->rs1].d;                                     \
    f64 rs2 = state->fp_regs[inst->rs2].d;                                     \
    state->gp_regs[inst->rd] = (expr);

static void func_feq_d(state_t *state, inst_t *inst) { FUNC(rs1 == rs2); }

static void func_flt_d(state_t *state, inst_t *inst) { FUNC(rs1 < rs2); }

static void func_fle_d(state_t *state, inst_t *inst) { FUNC(rs1 <= rs2); }

#undef FUNC

// 对单精度浮点数进行分类，返回一个位掩码，表示该数属于哪种类型
// （如正零、负零、正无穷、负无穷、NaN、次正规数等）。
static void func_fclass_s(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] = f32_classify(state->fp_regs[inst->rs1].f);
}

// 对双精度浮点数进行分类，返回一个位掩码，表示该数属于哪种类型。
static void func_fclass_d(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] = f64_classify(state->fp_regs[inst->rs1].d);
}

// 将单精度浮点数（float）转换为有符号 64 位整数（int64），写入整数寄存器 rd。
static void func_fcvt_l_s(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] = (int64_t)llrintf(state->fp_regs[inst->rs1].f);
}

// 将单精度浮点数转换为无符号 64 位整数（uint64），写入 rd。
static void func_fcvt_lu_s(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] = (uint64_t)llrintf(state->fp_regs[inst->rs1].f);
}

// 将双精度浮点数（double）转换为有符号 64 位整数（int64），写入 rd。
static void func_fcvt_l_d(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] = (int64_t)llrint(state->fp_regs[inst->rs1].d);
}

// 将双精度浮点数转换为无符号 64 位整数（uint64），写入 rd。
static void func_fcvt_lu_d(state_t *state, inst_t *inst) {
    state->gp_regs[inst->rd] = (uint64_t)llrint(state->fp_regs[inst->rs1].d);
}

// 将有符号 64 位整数（int64）转换为单精度浮点数，写入浮点寄存器 rd。
static void func_fcvt_s_l(state_t *state, inst_t *inst) {
    state->fp_regs[inst->rd].f = (f32)(int64_t)state->gp_regs[inst->rs1];
}

// 将无符号 64 位整数（uint64）转换为单精度浮点数，写入浮点寄存器 rd。
static void func_fcvt_s_lu(state_t *state, inst_t *inst) {
    state->fp_regs[inst->rd].f = (f32)(uint64_t)state->gp_regs[inst->rs1];
}

// 有符号 64 位整数（int64）转换为双精度浮点数，写入浮点寄存器 rd。
static void func_fcvt_d_l(state_t *state, inst_t *inst) {
    state->fp_regs[inst->rd].d = (f64)(int64_t)state->gp_regs[inst->rs1];
}

// 将无符号 64 位整数（uint64）转换为双精度浮点数，写入浮点寄存器 rd。
static void func_fcvt_d_lu(state_t *state, inst_t *inst) {
    state->fp_regs[inst->rd].d = (f64)(uint64_t)state->gp_regs[inst->rs1];
}

// - 将双精度浮点数（double）转换为单精度浮点数（float），写入浮点寄存器 rd。
static void func_fcvt_s_d(state_t *state, inst_t *inst) {
    state->fp_regs[inst->rd].f = (f32)state->fp_regs[inst->rs1].d;
}

// 将单精度浮点数（float）转换为双精度浮点数（double），写入浮点寄存器 rd。
static void func_fcvt_d_s(state_t *state, inst_t *inst) {
    state->fp_regs[inst->rd].d = (f64)state->fp_regs[inst->rs1].f;
}

typedef void(func_t)(state_t *, inst_t *);

static func_t *funcs[] = {
    func_lb,       func_lh,        func_lw,        func_ld,
    func_lbu,      func_lhu,       func_lwu,
    func_empty, // fence
    func_empty, // fence_i
    func_addi,     func_slli,      func_slti,      func_sltiu,
    func_xori,     func_srli,      func_srai,      func_ori,
    func_andi,     func_auipc,     func_addiw,     func_slliw,
    func_srliw,    func_sraiw,     func_sb,        func_sh,
    func_sw,       func_sd,        func_add,       func_sll,
    func_slt,      func_sltu,      func_xor,       func_srl,
    func_or,       func_and,       func_mul,       func_mulh,
    func_mulhsu,   func_mulhu,     func_div,       func_divu,
    func_rem,      func_remu,      func_sub,       func_sra,
    func_lui,      func_addw,      func_sllw,      func_srlw,
    func_mulw,     func_divw,      func_divuw,     func_remw,
    func_remuw,    func_subw,      func_sraw,      func_beq,
    func_bne,      func_blt,       func_bge,       func_bltu,
    func_bgeu,     func_jalr,      func_jal,       func_ecall,
    func_csrrw,    func_csrrs,     func_csrrc,     func_csrrwi,
    func_csrrsi,   func_csrrci,    func_flw,       func_fsw,
    func_fmadd_s,  func_fmsub_s,   func_fnmsub_s,  func_fnmadd_s,
    func_fadd_s,   func_fsub_s,    func_fmul_s,    func_fdiv_s,
    func_fsqrt_s,  func_fsgnj_s,   func_fsgnjn_s,  func_fsgnjx_s,
    func_fmin_s,   func_fmax_s,    func_fcvt_w_s,  func_fcvt_wu_s,
    func_fmv_x_w,  func_feq_s,     func_flt_s,     func_fle_s,
    func_fclass_s, func_fcvt_s_w,  func_fcvt_s_wu, func_fmv_w_x,
    func_fcvt_l_s, func_fcvt_lu_s, func_fcvt_s_l,  func_fcvt_s_lu,
    func_fld,      func_fsd,       func_fmadd_d,   func_fmsub_d,
    func_fnmsub_d, func_fnmadd_d,  func_fadd_d,    func_fsub_d,
    func_fmul_d,   func_fdiv_d,    func_fsqrt_d,   func_fsgnj_d,
    func_fsgnjn_d, func_fsgnjx_d,  func_fmin_d,    func_fmax_d,
    func_fcvt_s_d, func_fcvt_d_s,  func_feq_d,     func_flt_d,
    func_fle_d,    func_fclass_d,  func_fcvt_w_d,  func_fcvt_wu_d,
    func_fcvt_d_w, func_fcvt_d_wu, func_fcvt_l_d,  func_fcvt_lu_d,
    func_fmv_x_d,  func_fcvt_d_l,  func_fcvt_d_lu, func_fmv_d_x,
};

const char *inst_type_name(enum inst_type_t type) {
    switch (type) {
    case inst_lb:
        return "lb";
    case inst_lh:
        return "lh";
    case inst_lw:
        return "lw";
    case inst_ld:
        return "ld";
    case inst_lbu:
        return "lbu";
    case inst_lhu:
        return "lhu";
    case inst_lwu:
        return "lwu";
    case inst_fence:
        return "fence";
    case inst_fence_i:
        return "fence_i";
    case inst_addi:
        return "addi";
    case inst_slli:
        return "slli";
    case inst_slti:
        return "slti";
    case inst_sltiu:
        return "sltiu";
    case inst_xori:
        return "xori";
    case inst_srli:
        return "srli";
    case inst_srai:
        return "srai";
    case inst_ori:
        return "ori";
    case inst_andi:
        return "andi";
    case inst_auipc:
        return "auipc";
    case inst_addiw:
        return "addiw";
    case inst_slliw:
        return "slliw";
    case inst_srliw:
        return "srliw";
    case inst_sraiw:
        return "sraiw";
    case inst_sb:
        return "sb";
    case inst_sh:
        return "sh";
    case inst_sw:
        return "sw";
    case inst_sd:
        return "sd";
    case inst_add:
        return "add";
    case inst_sll:
        return "sll";
    case inst_slt:
        return "slt";
    case inst_sltu:
        return "sltu";
    case inst_xor:
        return "xor";
    case inst_srl:
        return "srl";
    case inst_or:
        return "or";
    case inst_and:
        return "and";
    case inst_mul:
        return "mul";
    case inst_mulh:
        return "mulh";
    case inst_mulhsu:
        return "mulhsu";
    case inst_mulhu:
        return "mulhu";
    case inst_div:
        return "div";
    case inst_divu:
        return "divu";
    case inst_rem:
        return "rem";
    case inst_remu:
        return "remu";
    case inst_sub:
        return "sub";
    case inst_sra:
        return "sra";
    case inst_lui:
        return "lui";
    case inst_addw:
        return "addw";
    case inst_sllw:
        return "sllw";
    case inst_srlw:
        return "srlw";
    case inst_mulw:
        return "mulw";
    case inst_divw:
        return "divw";
    case inst_divuw:
        return "divuw";
    case inst_remw:
        return "remw";
    case inst_remuw:
        return "remuw";
    case inst_subw:
        return "subw";
    case inst_sraw:
        return "sraw";
    case inst_beq:
        return "beq";
    case inst_bne:
        return "bne";
    case inst_blt:
        return "blt";
    case inst_bge:
        return "bge";
    case inst_bltu:
        return "bltu";
    case inst_bgeu:
        return "bgeu";
    case inst_jalr:
        return "jalr";
    case inst_jal:
        return "jal";
    case inst_ecall:
        return "ecall";
    case inst_csrrc:
        return "csrrc";
    case inst_csrrci:
        return "csrrci";
    case inst_csrrs:
        return "csrrs";
    case inst_csrrsi:
        return "csrrsi";
    case inst_csrrw:
        return "csrrw";
    case inst_csrrwi:
        return "csrrwi";
    case inst_flw:
        return "flw";
    case inst_fsw:
        return "fsw";
    case inst_fmadd_s:
        return "fmadd_s";
    case inst_fmsub_s:
        return "fmsub_s";
    case inst_fnmsub_s:
        return "fnmsub_s";
    case inst_fnmadd_s:
        return "fnmadd_s";
    case inst_fadd_s:
        return "fadd_s";
    case inst_fsub_s:
        return "fsub_s";
    case inst_fmul_s:
        return "fmul_s";
    case inst_fdiv_s:
        return "fdiv_s";
    case inst_fsqrt_s:
        return "fsqrt_s";
    case inst_fsgnj_s:
        return "fsgnj_s";
    case inst_fsgnjn_s:
        return "fsgnjn_s";
    case inst_fsgnjx_s:
        return "fsgnjx_s";
    case inst_fmin_s:
        return "fmin_s";
    case inst_fmax_s:
        return "fmax_s";
    case inst_fcvt_w_s:
        return "fcvt_w_s";
    case inst_fcvt_wu_s:
        return "fcvt_wu_s";
    case inst_fmv_x_w:
        return "fmv_x_w";
    case inst_feq_s:
        return "feq_s";
    case inst_flt_s:
        return "flt_s";
    case inst_fle_s:
        return "fle_s";
    case inst_fclass_s:
        return "fclass_s";
    case inst_fcvt_s_w:
        return "fcvt_s_w";
    case inst_fcvt_s_wu:
        return "fcvt_s_wu";
    case inst_fmv_w_x:
        return "fmv_w_x";
    case inst_fcvt_l_s:
        return "fcvt_l_s";
    case inst_fcvt_lu_s:
        return "fcvt_lu_s";
    case inst_fcvt_s_l:
        return "fcvt_s_l";
    case inst_fcvt_s_lu:
        return "fcvt_s_lu";
    case inst_fld:
        return "fld";
    case inst_fsd:
        return "fsd";
    case inst_fmadd_d:
        return "fmadd_d";
    case inst_fmsub_d:
        return "fmsub_d";
    case inst_fnmsub_d:
        return "fnmsub_d";
    case inst_fnmadd_d:
        return "fnmadd_d";
    case inst_fadd_d:
        return "fadd_d";
    case inst_fsub_d:
        return "fsub_d";
    case inst_fmul_d:
        return "fmul_d";
    case inst_fdiv_d:
        return "fdiv_d";
    case inst_fsqrt_d:
        return "fsqrt_d";
    case inst_fsgnj_d:
        return "fsgnj_d";
    case inst_fsgnjn_d:
        return "fsgnjn_d";
    case inst_fsgnjx_d:
        return "fsgnjx_d";
    case inst_fmin_d:
        return "fmin_d";
    case inst_fmax_d:
        return "fmax_d";
    case inst_fcvt_s_d:
        return "fcvt_s_d";
    case inst_fcvt_d_s:
        return "fcvt_d_s";
    case inst_feq_d:
        return "feq_d";
    case inst_flt_d:
        return "flt_d";
    case inst_fle_d:
        return "fle_d";
    case inst_fclass_d:
        return "fclass_d";
    case inst_fcvt_w_d:
        return "fcvt_w_d";
    case inst_fcvt_wu_d:
        return "fcvt_wu_d";
    case inst_fcvt_d_w:
        return "fcvt_d_w";
    case inst_fcvt_d_wu:
        return "fcvt_d_wu";
    case inst_fcvt_l_d:
        return "fcvt_l_d";
    case inst_fcvt_lu_d:
        return "fcvt_lu_d";
    case inst_fmv_x_d:
        return "fmv_x_d";
    case inst_fcvt_d_l:
        return "fcvt_d_l";
    case inst_fcvt_d_lu:
        return "fcvt_d_lu";
    case inst_fmv_d_x:
        return "fmv_d_x";
    default:
        return "unknown";
    }
}

void inst_print(inst_t *inst) {
    printf("inst_t {\n");
    printf("  type: %s (%d)\n", inst_type_name(inst->type), inst->type);
    printf("  rd: %d\n", inst->rd);
    printf("  rs1: %d\n", inst->rs1);
    printf("  rs2: %d\n", inst->rs2);
    printf("  rs3: %d\n", inst->rs3);
    printf("  imm: %d\n", inst->imm);
    printf("  csr: %d\n", inst->csr);
    printf("  rvc: %s\n", inst->rvc ? "true" : "false");
    printf("  continue_exec: %s\n", inst->continue_exec ? "true" : "false");
    printf("}\n");
}

void exec_block_interp(state_t *state) {
    static inst_t inst = { 0 };
    while (true) {
        uint32_t raw_data = *(uint32_t *)TO_HOST(state->pc);
        decode_inst(&inst, raw_data);
        // printf("PC: %lx\n", state->pc);
        // inst_print(&inst);
        funcs[inst.type](state, &inst);
        state->gp_regs[zero] = 0;

        if (inst.continue_exec)
            break; // 处理syscall
        state->pc += inst.rvc ? 2 : 4;
    }
}