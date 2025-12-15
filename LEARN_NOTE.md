// addi a1, a0, 2 <==> a1 = a0 + 2
// riscv指令是u32或u16(压缩指令)
//
// while (true) {
//      raw = fetch(pc);
//      inst = decode(raw);
//      exec(inst);
//      pc += rvc ? 2 : 4;
// }
