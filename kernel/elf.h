// Format of an ELF executable file

/* Standard ELF base typedefs for 32-bit */
typedef uint16 Elf64_Half;
typedef uint32 Elf64_Word;
typedef int32  Elf64_Sword;
typedef uint64 Elf64_Xword;
typedef int64  Elf64_Sxword;
typedef uint64 Elf64_Addr;
typedef uint64 Elf64_Off;
#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

/**************************************FILE HEADER**************************************/
struct elfhdr {
    uint32    magic;         /* Must equal ELF_MAGIC ("\x7FELF") */
    unsigned char elf[12];   /* Remaining ELF identification bytes:
                                class, data encoding, version, OS/ABI, ABI version */
    Elf64_Half  type;        /* Object file type (ET_EXEC, ET_DYN, etc.) */
    Elf64_Half  machine;     /* Target architecture (EM_X86_64, EM_RISCV, etc.) */
    Elf64_Word  version;     /* ELF version (usually 1) */
    Elf64_Addr  entry;       /* Entry point virtual address */
    Elf64_Off   phoff;       /* Offset of program header table in file */
    Elf64_Off   shoff;       /* Offset of section header table in file */
    Elf64_Word  flags;       /* Processor-specific flags */
    Elf64_Half  ehsize;      /* ELF header size in bytes */
    Elf64_Half  phentsize;   /* Size of one program header entry */
    Elf64_Half  phnum;       /* Number of entries in the program header table */
    Elf64_Half  shentsize;   /* Size of one section header entry */
    Elf64_Half  shnum;       /* Number of entries in the section header table */
    Elf64_Half  shstrndx;    /* Index of section header string table */
};
/* Object file types (e_type) */
#define ET_NONE   0x0000  /* No file type */
#define ET_REL    0x0001  /* Relocatable file */
#define ET_EXEC   0x0002  /* Executable file */
#define ET_DYN    0x0003  /* Shared object file */
#define ET_CORE   0x0004  /* Core file */
/* Machine types (e_machine) */
#define EM_NONE        0   /* No machine */
#define EM_M32         1   /* AT&T WE 32100 */
#define EM_SPARC       2   /* SPARC */
#define EM_386         3   /* Intel 80386 */
#define EM_68K         4   /* Motorola 68000 */
#define EM_88K         5   /* Motorola 88000 */
#define EM_860         7   /* Intel 80860 */
#define EM_MIPS        8   /* MIPS I Architecture */
#define EM_MIPS_RS3_LE 10  /* MIPS RS3000 Little-endian */
/* Object file version (e_version) */
#define EV_NONE        0   /* Invalid version */
#define EV_CURRENT     1   /* Current version (always 1 for valid ELF) */


/**************************************SECTION HEADER**************************************/
struct elfshdr {
    Elf64_Word  name;       /* Section name (index into section header string table) */
    Elf64_Word  type;       /* Section type (SHT_*) */
    Elf64_Xword flags;      /* Section flags (SHF_*) */
    Elf64_Addr  addr;       /* Virtual address in memory, for loaded sections */
    Elf64_Off   offset;     /* Offset of section in file */
    Elf64_Xword size;       /* Size of section in bytes */
    Elf64_Word  link;       /* Section header table index link (depends on section type) */
    Elf64_Word  info;       /* Extra information (depends on section type) */
    Elf64_Xword addralign;  /* Alignment requirement */
    Elf64_Xword entsize;    /* Entry size if section holds a table (e.g., symbol table) */
};
/* Section types (sh_type) */
#define SHT_NULL              0      /* Inactive section */
#define SHT_PROGBITS          1      /* Program data (code, data, read-only data) */
#define SHT_SYMTAB            2      /* Symbol table */
#define SHT_STRTAB            3      /* String table */
#define SHT_RELA              4      /* Relocation entries with addends */
#define SHT_HASH              5      /* Symbol hash table */
#define SHT_DYNAMIC           6      /* Dynamic linking info */
#define SHT_NOTE              7      /* Notes */
#define SHT_NOBITS            8      /* Uninitialized data (bss) */
#define SHT_REL               9      /* Relocation entries without addends */
#define SHT_SHLIB             10     /* Reserved */
#define SHT_DYNSYM            11     /* Dynamic linker symbol table */
/* Section flags (sh_flags) */
#define SHF_WRITE             0x001  /* Writable section */
#define SHF_ALLOC             0x002  /* Occupies memory during execution */
#define SHF_EXECINSTR         0x004  /* Executable instructions */
#define SHF_MERGE             0x010  /* Might be merged */
#define SHF_STRINGS           0x020  /* Contains null-terminated strings */
#define SHF_INFO_LINK         0x040  /* sh_info contains link */
#define SHF_LINK_ORDER        0x080  /* Preserve order after combining */
#define SHF_OS_NONCONFORMING  0x100  /* OS-specific semantics */
#define SHF_GROUP             0x200  /* Section is member of a group */
#define SHF_TLS               0x400  /* Section holds thread-local data */

// Symbol table entry
struct elfsym {
  Elf64_Word      st_name;   /* Index into the string table for symbol name */
  unsigned char   st_info;   /* Type and binding attributes (see ELF64_ST_BIND/ELF64_ST_TYPE) */
  unsigned char   st_other;  /* Reserved (usually 0) */
  Elf64_Half      st_shndx;  /* Section index: SHN_* (e.g., SHN_UNDEF, SHN_ABS) */
  Elf64_Addr      st_value;  /* Symbol value (address or absolute value) */
  Elf64_Xword     st_size;   /* Size of the symbol (0 for functions or unknown size) */
};
#define ELF_MAX_SYMB 1000
/* Macros to extract info from st_info */
#define ELF64_ST_BIND(i)    ((i) >> 4)       /* Symbol binding (local, global, weak) */
#define ELF64_ST_TYPE(i)    ((i) & 0xf)      /* Symbol type (function, object, etc.) */
#define ELF64_ST_INFO(b,t)  (((b)<<4) + ((t)&0xf))

/* Symbol bindings */
#define STB_LOCAL   0   /* Local symbol, not visible outside object */
#define STB_GLOBAL  1   /* Global symbol, visible to all objects */
#define STB_WEAK    2   /* Weak symbol, overridden by strong symbols */
#define STB_LOOS    10  /* OS-specific */
#define STB_HIOS    12  /* OS-specific */

/* Symbol types */
#define STT_NOTYPE  0    /* No type specified */
#define STT_OBJECT  1    /* Data object (variable, array, etc.) */
#define STT_FUNC    2    /* Function */
#define STT_SECTION 3    /* Symbol associated with a section */
#define STT_FILE    4    /* Source file associated with symbol */
#define STT_COMMON  5    /* Common data (uninitialized) */
#define STT_TLS     6    /* Thread-local storage */
#define STT_LOOS    10   /* OS-specific */
#define STT_HIOS    12   /* OS-specific */

/* Special section indices (st_shndx) */
#define SHN_UNDEF   0     /* Undefined symbol */
#define SHN_ABS     0xfff1/* Absolute value (not affected by relocations) */
#define SHN_COMMON  0xfff2/* Common block (uninitialized) */

/* Relocation entry without addend (used in .rel.* sections) */
struct elfrel{
    Elf64_Addr offset;  /* Location in memory / section to apply relocation */
    Elf64_Xword info;    /* Encodes symbol index and relocation type */
};

/* Relocation entry with explicit addend (used in .rela.* sections) */
struct elfrela{
    Elf64_Addr offset;  /* Location in memory / section to apply relocation */
    Elf64_Xword info;    /* Encodes symbol index and relocation type */
    Elf64_Sxword  addend;  /* Addend to apply during relocation */
};

/* Macros to extract info from r_info */
#define ELF64_R_SYM(i)   ((uint32)((i) >> 32))   /* Symbol index */
#define ELF64_R_TYPE(i)  ((uint32)(i))           /* Reloc type */
#define ELF64_R_INFO(s,t) ((((uint64)(s)) << 32) | ((t) & 0xFFFFFFFF))

/* RISC-V ELF relocation types */
#define R_RISCV_NONE        0
#define R_RISCV_32          1
#define R_RISCV_64          2
#define R_RISCV_RELATIVE    3
#define R_RISCV_COPY        4
#define R_RISCV_JUMP_SLOT   5
#define R_RISCV_TLS_DTPMOD32 6
#define R_RISCV_TLS_DTPMOD64 7
#define R_RISCV_TLS_DTPREL32 8
#define R_RISCV_TLS_DTPREL64 9
#define R_RISCV_TLS_TPREL32 10
#define R_RISCV_TLS_TPREL64 11
#define R_RISCV_RELAX       23
#define R_RISCV_32_PCREL    51

// Program section header
struct proghdr {
  uint32 type;
  uint32 flags;
  uint64 off;
  uint64 vaddr;
  uint64 paddr;
  uint64 filesz;
  uint64 memsz;
  uint64 align;
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
