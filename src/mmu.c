#include "rvemu.h"

void mmu_load_elf(mmu_t *mmu, int fd) {
    uint8_t buf[sizeof(elf64_ehdr_t)];

    FILE *file = fdopen(fd, "rb");

    if (fread(buf, 1, sizeof(elf64_ehdr_t), file) != sizeof(elf64_ehdr_t)) {
        fatal("file too small");
    }

    elf64_ehdr_t *ehdr = (elf64_ehdr_t *)buf;

    if (*(uint32_t *)ehdr != *(uint32_t *)ELFMAG) {
        fatal("bad elfmag");
    }

    if (ehdr->e_machine != EM_RISCV || ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        fatal("only RISCV64 elf support");
    }

    mmu->entry = ehdr->e_entry;
}