#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"
#include "random.h"

#ifndef DEBUG_SYMTAB
#define DEBUG_SYMTAB 0
#endif

#ifndef DEBUG_RELOC
#define DEBUG_RELOC 0
#endif

static int loadseg(pde_t *, uint64, struct inode *, uint, uint);

// map ELF permissions to PTE permission bits.
int flags2perm(int flags)
{
  int perm = 0;
  if (flags & 0x1)
    perm = PTE_X;
  if (flags & 0x2)
    perm |= PTE_W;
  return perm;
}

int getSymbolTable(struct elfhdr *elf, struct inode *ip, uint64 symboladdrs[ELF_MAX_SYMB])
{
  struct elfsym symbol;
  struct elfshdr sh;
  uint currentsymbol = 0;
  int i, off;
  DEBUG_PRINT(DEBUG_SYMTAB,"getSymbolTable: start, shoff=%ld, shnum=%d, shentsize=%d\n",
         elf->shoff, elf->shnum, elf->shentsize);
  for (i = 0, off = elf->shoff; i < elf->shnum; i++, off += elf->shentsize)
  {
    if (readi(ip, 0, (uint64)&sh, off, elf->shentsize) != elf->shentsize)
    {
      printf("exec: section readi error on section %d\n", i);
      return -1;
    }
    DEBUG_PRINT(DEBUG_SYMTAB,"  Section %d: type=0x%x, offset=%ld, size=%ld, entsize=%ld\n",
           i, sh.type, sh.offset, sh.size, sh.entsize);
    int not_dynsym = (sh.type ^ SHT_DYNSYM);
    if (!not_dynsym)
    {
      DEBUG_PRINT(DEBUG_SYMTAB,"  Found DYNSYM section at index %d\n", i);

      // found section header for dynamic symbol table
      // read through each dynamic symbol
      for (int sectoff = 0; sectoff < sh.size; sectoff += sh.entsize)
      {
        int size = readi(ip, 0, (uint64)&symbol, sh.offset + sectoff, sh.entsize);
        DEBUG_PRINT(DEBUG_SYMTAB,"    Symbol %d: st_name=%d, st_info=0x%x, st_other=0x%x, "
               "st_shndx=%d, st_value=0x%lx, st_size=%ld\n",
               currentsymbol, symbol.st_name, symbol.st_info, symbol.st_other,
               symbol.st_shndx, symbol.st_value, symbol.st_size);
        // debug("sym a:",symbol.)
        symboladdrs[currentsymbol++] = symbol.st_value;
        if (size != sizeof(struct elfsym))
        {
          printf("    Symbol read error: expected %ld bytes, got %d\n",
                 sizeof(struct elfsym), size);
          return -1;
        }
        if (currentsymbol == ELF_MAX_SYMB)
        {
          panic("Exec: elf file symbol exceeds max symbols");
        }
      }
      DEBUG_PRINT(DEBUG_SYMTAB,"  Finished reading DYNSYM: %d symbols total\n", currentsymbol);
    }
  }
  return currentsymbol;
}

int applyRelocation(uint64 load_offset, pagetable_t pagetable, struct elfhdr *elf, struct inode *ip, uint64 symboladdrs[ELF_MAX_SYMB])
{
  int i, off;
  struct elfshdr sh;
  struct elfrela relocation;
  uint64 instr;
  for (i = 0, off = elf->shoff; i < elf->shnum; i++, off += elf->shentsize)
  {
    if (readi(ip, 0, (uint64)&sh, off, elf->shentsize) != elf->shentsize)
    {
      printf("exec: section readi error on section %d\n", i);
      return -1;
    }
    int not_rela = (sh.type ^ SHT_RELA);
    if (!not_rela)
    {
      // found section header for relocations
      // read through each relocation
      // before entering loop
      DEBUG_PRINT(DEBUG_RELOC,"applyRelocation: start, load_offset=%ld\n", load_offset);
      for (int sectoff = 0, relocnum = 1; sectoff < sh.size; sectoff += sh.entsize, relocnum++)
      {
        int size = readi(ip, 0, (uint64)&relocation, sh.offset + sectoff, sh.entsize);
        switch (ELF64_R_TYPE(relocation.info))
        {
        case R_RISCV_RELATIVE:
          DEBUG_PRINT(DEBUG_RELOC,"Relocation: R_RISCV_RELATIVE at offset %ld\n", relocation.offset);
          if (copyin(pagetable, (char *)&instr, (uint64)relocation.offset + load_offset, 8) != 0)
            panic("exec: copyin1 relocation");
          DEBUG_PRINT(DEBUG_RELOC,"  Old value: %ld\n", instr);
          instr = load_offset;
          DEBUG_PRINT(DEBUG_RELOC,"  New value: %ld\n", instr);
          if (copyout(pagetable, (uint64)relocation.offset + load_offset, (char *)&instr, 8) != 0)
            panic("exec: copyout1 relocation");
          break;

        case R_RISCV_JUMP_SLOT:
        {
          int sym_index = ELF64_R_SYM(relocation.info);
          DEBUG_PRINT(DEBUG_RELOC,"Relocation: R_RISCV_JUMP_SLOT at offset %ld, sym %d\n",
                 relocation.offset, sym_index);
          instr = 0;
          if (copyin(pagetable, (char *)&instr, (uint64)relocation.offset + load_offset, 8) != 0)
            panic("exec: copyin2 relocation");
          DEBUG_PRINT(DEBUG_RELOC,"  Old value: %ld\n", instr);
          instr = symboladdrs[sym_index] + load_offset;
          DEBUG_PRINT(DEBUG_RELOC,"  New value: %ld (symbol base %ld)\n", instr, symboladdrs[sym_index]);
          if (copyout(pagetable, (uint64)relocation.offset + load_offset, (char *)&instr, 8) != 0)
            panic("exec: copyout2 relocation");
          break;
        }

        case R_RISCV_64:
        {
          int sym_index = ELF64_R_SYM(relocation.info);
          DEBUG_PRINT(DEBUG_RELOC,"Relocation: R_RISCV_64 at offset %ld, sym %d, addend %d\n",
                 relocation.offset, sym_index, (int)relocation.addend);
          instr = 0;
          if (copyin(pagetable, (char *)&instr, (uint64)relocation.offset + load_offset, 8) != 0)
            panic("exec: copyin3 relocation");
          DEBUG_PRINT(DEBUG_RELOC,"  Old value: %ld\n", instr);
          instr = symboladdrs[sym_index] + relocation.addend + load_offset;
          DEBUG_PRINT(DEBUG_RELOC,"  New value: %ld (symbol base %ld)\n", instr, symboladdrs[sym_index]);
          if (copyout(pagetable, (uint64)relocation.offset + load_offset, (char *)&instr, 8) != 0)
            panic("exec: copyout3 relocation");
          break;
        }

        default:
          DEBUG_PRINT(DEBUG_RELOC,"exec: relocation type %d not handled (offset %ld)\n",
                 ELF64_R_TYPE(relocation.info), relocation.offset);
          panic("exec: relocation type not handled");
          break;
        }

        if (size != sizeof(struct elfrela))
          return -1;
      }
    }
  }
  DEBUG_PRINT(DEBUG_RELOC,"applyRelocation: done\n");

  return 0;
}

//
// the implementation of the exec() system call
//
uint64 symboladdrs[ELF_MAX_SYMB] = {0};
int kexec(char *path, char **argv)
{
  debug("exec: trying to run %s\n", path);

  char *s, *last;
  int i = 0, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op();

  // Open the executable file.
  if ((ip = namei(path)) == 0)
  {
    end_op();
    return -1;
  }
  ilock(ip);

  // Read the ELF header.
  if (readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;

  // Is this really an ELF file?
  if (elf.magic != ELF_MAGIC)
    goto bad;

  if ((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  int aslr_eligible = 0;
  int aslr_offset = 0;
  // uint64 symboladdrs[ELF_MAX_SYMB]={0};

  if (elf.type == ET_DYN)
  {
    aslr_eligible = 1;
    if (getSymbolTable(&elf, ip, symboladdrs) <= 0)
      goto bad;
    aslr_offset = rand_range(0,1000)<<12;
    debug("aslr eligible. offset=%d\n", aslr_offset);
    elf.entry += aslr_offset;
    sz = aslr_offset;
  }

  // sz = uvmalloc(pagetable, 0, aslr_offset, 0);
  // Load program into memory.
  for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph))
  {
    if (readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if (ph.type != ELF_PROG_LOAD)
      continue;
    if (ph.memsz < ph.filesz)
      goto bad;
    if (ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if (ph.vaddr % PGSIZE != 0)
      goto bad;
    uint64 sz1;

    ph.vaddr += aslr_offset;

    debug("size: %ld | va: %ld | memsz: %ld  | paddr: %ld\n", sz, ph.vaddr, ph.memsz, ph.paddr);
    if ((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0)
    {
      printf("EXEC: uvmalloc\n");
      goto bad;
    }
    sz = sz1;
    if (loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
    {
      printf("EXEC: load seg\n");
      goto bad;
    }
  }
  if (aslr_eligible)
    if (applyRelocation(aslr_offset, pagetable, &elf, ip, symboladdrs) < 0)
    {
      printf("exec: applyRelocation failed");
      goto bad;
    }

  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // Allocate some pages at the next page boundary.
  // Make the first inaccessible as a stack guard.
  // Use the rest as the user stack.
  sz = PGROUNDUP(sz, PGSIZE);
  uint64 sz1;
  if ((sz1 = uvmalloc(pagetable, sz, sz + (USERSTACK + 1) * PGSIZE, PTE_W)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz - (USERSTACK + 1) * PGSIZE);
  sp = sz;
  stackbase = sp - USERSTACK * PGSIZE;

  // Copy argument strings into new stack, remember their
  // addresses in ustack[].
  for (argc = 0; argv[argc]; argc++)
  {
    if (argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if (sp < stackbase)
      goto bad;
    if (copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // push a copy of ustack[], the array of argv[] pointers.
  sp -= (argc + 1) * sizeof(uint64);
  sp -= sp % 16;
  if (sp < stackbase)
    goto bad;
  if (copyout(pagetable, sp, (char *)ustack, (argc + 1) * sizeof(uint64)) < 0)
    goto bad;

  // a0 and a1 contain arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  for (last = s = path; *s; s++)
    if (*s == '/')
      last = s + 1;
  safestrcpy(p->name, last, sizeof(p->name));

  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry; // initial program counter = ulib.c:start()
  p->trapframe->sp = sp;         // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);
  return argc; // this ends up in a0, the first argument to main(argc, argv)

bad:
  if (pagetable)
    proc_freepagetable(pagetable, sz);
  if (ip)
  {
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// Load an ELF program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  for (i = 0; i < sz; i += PGSIZE)
  {
    pa = walkaddr(pagetable, va + i);
    if (pa == 0)
      panic("loadseg: address should exist");
    if (sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if (readi(ip, 0, (uint64)pa, offset + i, n) != n)
      return -1;
  }

  return 0;
}
