#pragma once

#include "libdw.h"
#include "libdwfl.h"
#include "libebl.h"

typedef struct Dwfl_Process Dwfl_Process;
#define _(Str) dgettext("elfutils", Str)

#define DWFL_ERRORS                                                            \
  DWFL_ERROR(NOERROR, N_("no error"))                                          \
  DWFL_ERROR(UNKNOWN_ERROR, N_("unknown error"))                               \
  DWFL_ERROR(NOMEM, N_("out of memory"))                                       \
  DWFL_ERROR(ERRNO, N_("See errno"))                                           \
  DWFL_ERROR(LIBELF, N_("See elf_errno"))                                      \
  DWFL_ERROR(LIBDW, N_("See dwarf_errno"))                                     \
  DWFL_ERROR(LIBEBL, N_("See ebl_errno (XXX missing)"))                        \
  DWFL_ERROR(ZLIB, N_("gzip decompression failed"))                            \
  DWFL_ERROR(BZLIB, N_("bzip2 decompression failed"))                          \
  DWFL_ERROR(LZMA, N_("LZMA decompression failed"))                            \
  DWFL_ERROR(UNKNOWN_MACHINE, N_("no support library found for machine"))      \
  DWFL_ERROR(NOREL, N_("Callbacks missing for ET_REL file"))                   \
  DWFL_ERROR(BADRELTYPE, N_("Unsupported relocation type"))                    \
  DWFL_ERROR(BADRELOFF, N_("r_offset is bogus"))                               \
  DWFL_ERROR(BADSTROFF, N_("offset out of range"))                             \
  DWFL_ERROR(RELUNDEF, N_("relocation refers to undefined symbol"))            \
  DWFL_ERROR(CB, N_("Callback returned failure"))                              \
  DWFL_ERROR(NO_DWARF, N_("No DWARF information found"))                       \
  DWFL_ERROR(NO_SYMTAB, N_("No symbol table found"))                           \
  DWFL_ERROR(NO_PHDR, N_("No ELF program headers"))                            \
  DWFL_ERROR(OVERLAP, N_("address range overlaps an existing module"))         \
  DWFL_ERROR(ADDR_OUTOFRANGE, N_("address out of range"))                      \
  DWFL_ERROR(NO_MATCH, N_("no matching address range"))                        \
  DWFL_ERROR(TRUNCATED, N_("image truncated"))                                 \
  DWFL_ERROR(ALREADY_ELF, N_("ELF file opened"))                               \
  DWFL_ERROR(BADELF, N_("not a valid ELF file"))                               \
  DWFL_ERROR(WEIRD_TYPE, N_("cannot handle DWARF type description"))           \
  DWFL_ERROR(WRONG_ID_ELF, N_("ELF file does not match build ID"))             \
  DWFL_ERROR(BAD_PRELINK, N_("corrupt .gnu.prelink_undo section data"))        \
  DWFL_ERROR(LIBEBL_BAD, N_("Internal error due to ebl"))                      \
  DWFL_ERROR(CORE_MISSING, N_("Missing data in core file"))                    \
  DWFL_ERROR(INVALID_REGISTER, N_("Invalid register"))                         \
  DWFL_ERROR(PROCESS_MEMORY_READ, N_("Error reading process memory"))          \
  DWFL_ERROR(PROCESS_NO_ARCH, N_("Couldn't find architecture of any ELF"))     \
  DWFL_ERROR(PARSE_PROC, N_("Error parsing /proc filesystem"))                 \
  DWFL_ERROR(INVALID_DWARF, N_("Invalid DWARF"))                               \
  DWFL_ERROR(UNSUPPORTED_DWARF, N_("Unsupported DWARF"))                       \
  DWFL_ERROR(NEXT_THREAD_FAIL, N_("Unable to find more threads"))              \
  DWFL_ERROR(ATTACH_STATE_CONFLICT, N_("Dwfl already has attached state"))     \
  DWFL_ERROR(NO_ATTACH_STATE, N_("Dwfl has no attached state"))                \
  DWFL_ERROR(NO_UNWIND, N_("Unwinding not supported for this architecture"))   \
  DWFL_ERROR(INVALID_ARGUMENT, N_("Invalid argument"))                         \
  DWFL_ERROR(NO_CORE_FILE, N_("Not an ET_CORE ELF file"))

#define DWFL_ERROR(name, text) DWFL_E_##name,
typedef enum { DWFL_ERRORS DWFL_E_NUM } Dwfl_Error;
#undef DWFL_ERROR

#define OTHER_ERROR(name) ((unsigned int)DWFL_E_##name << 16)
#define DWFL_E(name, errno) (OTHER_ERROR(name) | (errno))

struct Dwfl_User_Core {
  char *executable_for_core; /* --executable if --core was specified.  */
  Elf *core;                 /* non-NULL if we need to free it.  */
  int fd;                    /* close if >= 0.  */
};

struct Dwfl {
  const Dwfl_Callbacks *callbacks;

  Dwfl_Module *modulelist; /* List in order used by full traversals.  */

  Dwfl_Process *process;
  Dwfl_Error attacherr; /* Previous error attaching process.  */

  GElf_Addr offline_next_address;

  GElf_Addr segment_align; /* Smallest granularity of segments.  */

  /* Binary search table in three parallel malloc'd arrays.  */
  size_t lookup_elts;          /* Elements in use.  */
  size_t lookup_alloc;         /* Elements allococated.  */
  GElf_Addr *lookup_addr;      /* Start address of segment.  */
  Dwfl_Module **lookup_module; /* Module associated with segment, or null.  */
  int *lookup_segndx;          /* User segment index, or -1.  */

  /* Cache from last dwfl_report_segment call.  */
  const void *lookup_tail_ident;
  GElf_Off lookup_tail_vaddr;
  GElf_Off lookup_tail_offset;
  int lookup_tail_ndx;

  struct Dwfl_User_Core *user_core;
};

#define OFFLINE_REDZONE 0x10000

struct dwfl_file {
  char *name;
  int fd;
  bool valid;     /* The build ID note has been matched.  */
  bool relocated; /* Partial relocation of all sections done.  */

  Elf *elf;

  /* This is the lowest p_vaddr in this ELF file, aligned to p_align.
     For a file without phdrs, this is zero.  */
  GElf_Addr vaddr;

  /* This is an address chosen for synchronization between the main file
     and the debug file.  See dwfl_module_getdwarf.c for how it's chosen.  */
  GElf_Addr address_sync;
};

struct Dwfl_Module {
  Dwfl *dwfl;
  struct Dwfl_Module *next; /* Link on Dwfl.modulelist.  */

  void *userdata;

  char *name; /* Iterator name for this module.  */
  GElf_Addr low_addr, high_addr;

  struct dwfl_file main, debug, aux_sym;
  GElf_Addr main_bias;
  Ebl *ebl;
  GElf_Half e_type;  /* GElf_Ehdr.e_type cache.  */
  Dwfl_Error elferr; /* Previous failure to open main file.  */

  struct dwfl_relocation *reloc_info; /* Relocatable sections.  */

  struct dwfl_file *symfile; /* Either main or debug.  */
  Elf_Data *symdata;         /* Data in the ELF symbol table section.  */
  Elf_Data *aux_symdata;     /* Data in the auxiliary ELF symbol table.  */
  size_t syments;            /* sh_size / sh_entsize of that section.  */
  size_t aux_syments;        /* sh_size / sh_entsize of aux_sym section.  */
  int first_global;          /* Index of first global symbol of table.  */
  int aux_first_global;      /* Index of first global of aux_sym table.  */
  Elf_Data *symstrdata;      /* Data for its string table.  */
  Elf_Data *aux_symstrdata;  /* Data for aux_sym string table.  */
  Elf_Data *symxndxdata;     /* Data in the extended section index table. */
  Elf_Data *aux_symxndxdata; /* Data in the extended auxiliary table. */

  char *elfdir; /* The dir where we found the main Elf.  */

  Dwarf *dw;    /* libdw handle for its debugging info.  */
  Dwarf *alt;   /* Dwarf used for dwarf_setalt, or NULL.  */
  int alt_fd;   /* descriptor, only valid when alt != NULL.  */
  Elf *alt_elf; /* Elf for alt Dwarf.  */

  Dwfl_Error symerr; /* Previous failure to load symbols.  */
  Dwfl_Error dwerr;  /* Previous failure to load DWARF.  */

  /* Known CU's in this module.  */
  struct dwfl_cu *first_cu, **cu;

  void *lazy_cu_root; /* Table indexed by Dwarf_Off of CU.  */

  struct dwfl_arange *aranges; /* Mapping of addresses in module to CUs.  */

  void *build_id_bits;      /* malloc'd copy of build ID bits.  */
  GElf_Addr build_id_vaddr; /* Address where they reside, 0 if unknown.  */
  int build_id_len;         /* -1 for prior failure, 0 if unset.  */

  unsigned int ncu;
  unsigned int lazycu; /* Possible users, deleted when none left.  */
  unsigned int naranges;

  Dwarf_CFI *dwarf_cfi; /* Cached DWARF CFI for this module.  */
  Dwarf_CFI *eh_cfi;    /* Cached EH CFI for this module.  */

  int segment;        /* Index of first segment table entry.  */
  bool gc;            /* Mark/sweep flag.  */
  bool is_executable; /* Use Dwfl::executable_for_core?  */
};

/* This holds information common for all the threads/tasks/TIDs of one process
   for backtraces.  */

struct Dwfl_Process {
  struct Dwfl *dwfl;
  pid_t pid;
  const Dwfl_Thread_Callbacks *callbacks;
  void *callbacks_arg;
  struct ebl *ebl;
  bool ebl_close : 1;
};

/* See its typedef in libdwfl.h.  */

struct Dwfl_Thread {
  Dwfl_Process *process;
  pid_t tid;
  /* The current frame being unwound.  Initially it is the bottom frame.
     Later the processed frames get freed and this pointer is updated.  */
  Dwfl_Frame *unwound;
  void *callbacks_arg;
};

/* See its typedef in libdwfl.h.  */
struct Dwfl_Frame {
  Dwfl_Thread *thread;
  /* Previous (outer) frame.  */
  Dwfl_Frame *unwound;
  bool signal_frame  : 1;
  bool initial_frame : 1;
  enum {
    /* This structure is still being initialized or there was an error
       initializing it.  */
    DWFL_FRAME_STATE_ERROR,
    /* PC field is valid.  */
    DWFL_FRAME_STATE_PC_SET,
    /* PC field is undefined, this means the next (inner) frame was the
       outermost frame.  */
    DWFL_FRAME_STATE_PC_UNDEFINED
  } pc_state;
  /* Either initialized from appropriate REGS element or on some archs
     initialized separately as the return address has no DWARF register.  */
  Dwarf_Addr pc;
  /* (1 << X) bitmask where 0 <= X < ebl_frame_nregs.  */
  uint64_t regs_set[3];
  /* REGS array size is ebl_frame_nregs.
     REGS_SET tells which of the REGS are valid.  */
  Dwarf_Addr regs[];
};

/* Information cached about each CU in Dwfl_Module.dw.  */
struct dwfl_cu {
  /* This caches libdw information about the CU.  It's also the
     address passed back to users, so we take advantage of the
     fact that it's placed first to cast back.  */
  Dwarf_Die die;

  Dwfl_Module *mod; /* Pointer back to containing module.  */

  struct dwfl_cu *next; /* CU immediately following in the file.  */

  struct Dwfl_Lines *lines;
};

struct Dwfl_Lines {
  struct dwfl_cu *cu;

  /* This is what the opaque Dwfl_Line * pointers we pass to users are.
     We need to recover pointers to our struct dwfl_cu and a record in
     libdw's Dwarf_Line table.  To minimize the memory used in addition
     to libdw's Dwarf_Lines buffer, we just point to our own index in
     this table, and have one pointer back to the CU.  The indices here
     match those in libdw's Dwarf_CU.lines->info table.  */
  struct Dwfl_Line {
    unsigned int idx; /* My index in the dwfl_cu.lines table.  */
  } idx[0];
};
