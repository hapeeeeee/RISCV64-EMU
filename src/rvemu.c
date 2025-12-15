#include "rvemu.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    assert(argc > 1);

    machine_t machine = { 0 };
    machine_load_program(&machine, argv[1]);

    machine_setup(&machine, argc, argv);

    while (true) {

        enum exit_reason_t exit_reason = machine_step(&machine);
        assert(exit_reason == ecall);

        // handle syscall
        // RISCV syscall发生时, a7寄存器记录syscall编号
        // a0 - a6寄存器记录syscall所需参数
        // printf("machine_get_gp_reg\n");
        uint64_t syscall_id = machine_get_gp_reg(&machine, a7);
        // printf("handle syscall: %lu\n", syscall_id);
        uint64_t syscall_ret = do_syscall(&machine, syscall_id);
        machine_set_gp_reg(&machine, a0, syscall_ret);
    }

    return 0;
}