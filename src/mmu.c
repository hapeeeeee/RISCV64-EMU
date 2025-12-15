#include "rvemu.h"
#include <assert.h>
#include <bits/stdint-uintn.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

static void
load_phdr(elf64_phdr_t *phdr, elf64_ehdr_t *ehdr, int64_t i, FILE *file) {
    if (fseek(file, ehdr->e_phoff + ehdr->e_phentsize * i, SEEK_SET) != 0) {
        fatal("file too small");
    }

    if (fread((void *)phdr, 1, ehdr->e_phentsize, file) != ehdr->e_phentsize) {
        fatal("file too small");
    }
}

static int flags_to_mmap_prot(uint32_t flags) {
    return (flags & PF_R ? PROT_READ : 0) | (flags & PF_W ? PROT_WRITE : 0) |
           (flags & PF_X ? PROT_EXEC : 0);
}

/*
 * ELF 段加载中的对齐说明
 *
 * 在 ELF 文件中，每个 Program Header (phdr) 描述一个“段”：
 *   - p_vaddr : 段在内存中的期望虚拟地址（不一定页对齐）
 *   - p_filesz: 段在 ELF 文件中实际存在的数据字节数
 *   - p_memsz : 段加载后在内存中应占的总空间（可能比 p_filesz 大，比如 .bss
 需要补零）
 *
 * 问题：
 *   ELF 文件中的 p_vaddr 可能不是页对齐的。
 *   但在加载时（例如使用 mmap 或手动分配内存）必须按页对齐分配。
 *
 * 例子：
 *   page_size = 0x1000
 *   p_vaddr    = 0x8048034
 *   p_filesz   = 0x1000
 *   p_memsz    = 0x1200
 *
 *   内存布局 (理想)
 *      0x8048034 ───────────────────────────────┐
 *                  ← 0x1000 bytes (文件内容) →   │
 *      0x8049034 ───────────────────────────────┘
 *  但 mmap 时只能从 0x8048000 开始。因此
    内存布局 (实际)
 *      0x8048000 ───────────────────────────────┐
                     ← 0x34(页内偏移) + 0x1000 →  │
        0x8049034 ───────────────────────────────┘
    映射长度 = p_filesz + (p_vaddr - aligned_vaddr)
 *
 *   这样才能保证映射区域完整包含从 p_vaddr 开始的段内容。
 *
 * 总结：
 *   - p_filesz / p_memsz 表示段本身的数据大小（不考虑页对齐）
 *   - 加上 (p_vaddr - aligned_vaddr) 是为了补齐前面因为页对齐而多出的部分
 *   - 否则映射区会漏掉段的开头部分
 *
 * 对应代码：
 *   uint64_t guest_in_host_vaddr = TO_HOST(phdr->p_vaddr);
 *   uint64_t aligned_vaddr = ROUNDDOWN(guest_in_host_vaddr, page_size);
 *   uint64_t filesz = phdr->p_filesz + (guest_in_host_vaddr - aligned_vaddr);
 *   uint64_t memsz  = phdr->p_memsz  + (guest_in_host_vaddr - aligned_vaddr);
 */

static void mmu_load_segment(mmu_t *mmu, elf64_phdr_t *phdr, int fd) {
    int page_size = getpagesize();
    uint64_t p_offset = phdr->p_offset;
    uint64_t guest_in_host_vaddr = TO_HOST(phdr->p_vaddr);
    uint64_t aligned_vaddr = ROUNDDOWN(guest_in_host_vaddr, page_size);
    uint64_t filesz = phdr->p_filesz + (guest_in_host_vaddr - aligned_vaddr);
    uint64_t memsz = phdr->p_memsz + (guest_in_host_vaddr - aligned_vaddr);

    int prot = flags_to_mmap_prot(phdr->p_flags);
    uint64_t addr = (uint64_t)mmap(
        (void *)aligned_vaddr,
        filesz, // 不满一页同样会分配一页，因此之后的分配需要ROUNDUP
        prot,
        MAP_PRIVATE | MAP_FIXED,
        fd,
        ROUNDDOWN(p_offset, page_size)
    );
    assert(addr == aligned_vaddr);

    uint64_t remaining_bss_size =
        ROUNDUP(memsz, page_size) - ROUNDUP(filesz, page_size);
    if (remaining_bss_size > 0) {
        uint64_t addr = (uint64_t)mmap(
            (void *)aligned_vaddr + ROUNDUP(filesz, page_size),
            remaining_bss_size,
            prot,
            MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE,
            -1,
            0
        );
        assert(addr == aligned_vaddr + ROUNDUP(filesz, page_size));
    }
    mmu->host_alloc =
        MAX(mmu->host_alloc, aligned_vaddr + ROUNDUP(memsz, page_size));
    mmu->base = mmu->guest_alloc = TO_GUEST(mmu->host_alloc);
    // printf("seg %lu-%lu\n", mmu->host_alloc, mmu->guest_alloc);
}

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

    elf64_phdr_t phdr;
    for (int64_t i = 0; i < ehdr->e_phnum; i++) {
        load_phdr(&phdr, ehdr, i, file);

        if (phdr.p_type == PT_LOAD) {
            mmu_load_segment(mmu, &phdr, fd);
        }
    }
}

uint64_t mmu_alloc(mmu_t *mmu, int64_t size) {
    uint64_t page_size = getpagesize();
    uint64_t base = mmu->guest_alloc;
    assert(base >= mmu->base);
    mmu->guest_alloc += size; // 可能会释放内存
    assert(mmu->guest_alloc >= mmu->base);

    if (size > 0 && mmu->guest_alloc > TO_GUEST(mmu->host_alloc)) {
        uint64_t alloc_size = ROUNDUP(size, page_size);
        if (mmap(
                (void *)mmu->host_alloc,
                alloc_size,
                PROT_READ | PROT_WRITE,
                MAP_ANONYMOUS | MAP_PRIVATE,
                -1,
                0
            ) == MAP_FAILED)
            fatal("mmap failed in mmu alloc");

        mmu->host_alloc += alloc_size;
    } else if (size < 0 && ROUNDUP(mmu->guest_alloc, page_size) <
                               TO_GUEST(mmu->host_alloc)) {
        uint64_t munmap_size =
            TO_GUEST(mmu->host_alloc) - ROUNDUP(mmu->guest_alloc, page_size);
        if (munmap((void *)mmu->host_alloc, munmap_size) == -1)
            fatal(strerror(errno)); // ??? 似乎有问题
        // if (munmap((void *)(mmu->host_alloc - munmap_size), munmap_size) ==
        // -1)
        //     fatal(strerror(errno));
        mmu->host_alloc -= munmap_size;
    }

    return base; // 返回堆内存的初始地址, 该值在加载完elf后恒定
}