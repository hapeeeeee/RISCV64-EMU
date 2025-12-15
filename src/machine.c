#include "rvemu.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum exit_reason_t machine_step(machine_t *machine) {
    while (true) {
        machine->state.exit_reason = none;
        exec_block_interp(&machine->state);
        assert(machine->state.exit_reason != none);
        if (machine->state.exit_reason == direct_branch ||
            machine->state.exit_reason == indirect_branch) {

            machine->state.pc = machine->state.reenter_pc;
            continue; // for JIT
        }
        break;
    }

    machine->state.pc = machine->state.reenter_pc;
    assert(machine->state.exit_reason == ecall);
    return ecall;
}

void machine_load_program(machine_t *m, char *prog) {
    int fd = open(prog, O_RDONLY);
    if (fd == -1) {
        fatal(strerror(errno));
    }

    mmu_load_elf(&(m->mmu), fd);
    close(fd);

    m->state.pc = (uint64_t)m->mmu.entry;
}

void machine_setup(machine_t *machine, int argc, char *argv[]) {
    size_t stack_size = 32 * 1024 * 1024; // 32MB的栈空间
    // addr是被模拟程序除自身ELF后的开始地址,是最大栈顶地址
    uint64_t addr = mmu_alloc(&machine->mmu, stack_size);
    // printf("setup alloc addr 1 %lu-%lu\n", addr, machine->mmu.guest_alloc);
    // 栈在内存中是低地址是栈顶, 因此addr +
    // stack_size等于将栈顶指针指向栈底,置空栈
    machine->state.gp_regs[sp] = addr + stack_size;

    machine->state.gp_regs[sp] -= 8; // auxv
    machine->state.gp_regs[sp] -= 8; // envp
    machine->state.gp_regs[sp] -= 8; // argv end

    // 从后往前将argv中的字符串指针压入栈
    size_t argvs_index = argc - 1;
    for (int i = argvs_index; i > 0; i--) {
        // strlen只返回字符串长度, 但字符串末尾有一个`\0`
        // 需要在申请内存时 多申请一个字节
        size_t len = strlen(argv[i]);
        addr = mmu_alloc(&machine->mmu, len + 1);
        // printf("setup alloc addr 2 %lu-%lu\n", addr,
        // machine->mmu.guest_alloc); 将字符串指针写入栈底之后
        mmu_write(addr, (uint8_t *)argv[i], len);
        machine->state.gp_regs[sp] -= 8; // 字符串指针在被模拟程序的栈空间位置
        mmu_write(
            machine->state.gp_regs[sp], (uint8_t *)&addr, sizeof(uint64_t)
        );
    }

    machine->state.gp_regs[sp] -= 8; // argc
    mmu_write(machine->state.gp_regs[sp], (uint8_t *)&argc, sizeof(uint64_t));
}