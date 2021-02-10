#ifndef _H_unwind
#define _H_unwind

#include <bfd.h>
#include <errno.h>
#include <gelf.h>
#include "libunwind.h"

#include "/usr/include/libdwarf/dwarf.h"  // TODO wow... Just wow
#include "procutils.h"

#ifdef D_UWDBG
#  define DBGLOG(...)                          \
    do {                                       \
      fprintf(stderr, "%s: ", __FUNCTION__);   \
      fprintf(stderr, __VA_ARGS__);            \
    } while(0)
#else
#  define DBGLOG(...) do{}while(0);
#endif

// TODO: remove this immediately
struct table_entry {
    int32_t start_ip_offset;
    int32_t fde_offset;
  };

struct __attribute__((packed))  eh_frame_hdr {
  unsigned char version;
  unsigned char eh_frame_ptr_enc;
  unsigned char fde_count_enc;
  unsigned char table_enc;
  uint64_t enc[2];
  char data[];
};

struct Dwarf {
  uint8_t* ptr;
  const uint8_t* end;
};

// TODO what else is needed for inline functions?
struct FunLoc {
  uint64_t ip;        // Relative to file, not VMA
  uint64_t map_start; // Start address of mapped region
  uint64_t map_end;   // End
  uint64_t map_off;   // Offset into file
  char* funname;      // name of the function (mangled, possibly)
  char* srcpath;      // name of the source file, if known
  char* sopath;       // name of the file where the symbol is interned (e.g., .so)
  uint32_t line;      // line number in file
  uint32_t disc;      // discriminator
};

struct FunLocLookup {
  bfd_vma pc;
  struct FunLoc* loc;
  struct bfd_symbol** symtab;
  char done;
  bfd* bfd;
};

// References to the inner struct of the union will fail without C99 anon structs
// Register order follows https://github.com/torvalds/linux/blob/master/arch/x86/include/uapi/asm/perf_regs.h
struct UnwindState {
  pid_t pid;
  unw_addr_space_t uas;
  char* stack;  // stack dump, probably from perf sample
  size_t stack_sz;
  union {
    uint64_t regs[3];
    struct {
      uint64_t ebp;
      uint64_t esp;
      uint64_t eip;
    };
  };
  Map* map;
};

/******************************************************************************\
|*                               Symbol Lookup                                *|
\******************************************************************************/
// Forward decl
typedef void bfd_cleanup;
#include "elf-bfd.h" // add binutils-gdb to include path
enum DMGL {
  DMGL_NO_OPTS = 0,            // For readability...
  DMGL_PARAMS = (1 << 0),      // Include function args
  DMGL_ANSI = (1 << 1),        // Include const, volatile, etc
  DMGL_JAVA = (1 << 2),        // Demangle as Java rather than C++.
  DMGL_VERBOSE = (1 << 3),     // Include implementation details.
  DMGL_TYPES = (1 << 4),       // Also try to demangle type encodings.
  DMGL_RET_POSTFIX = (1 << 5), // Print function return types
  DMGL_RET_DROP = (1 << 6),    // Suppress function return types
  DMGL_AUTO  = (1 << 8),
  DMGL_GNU_V3 = (1 << 14),
  DMGL_GNAT = (1 << 15),       // Ada?
  DMGL_DLANG = (1 << 16),      // DLANG?
  DMGL_RUST = (1 << 17),       // Rust wraps GNU_V3 style mangling.
  DMGL_STYLE_MASK = (DMGL_AUTO|DMGL_GNU_V3|DMGL_JAVA|DMGL_GNAT|DMGL_DLANG|DMGL_RUST)
};

static void slurp_symtab(struct FunLocLookup* flu) {
  bfd* abfd = flu->bfd;
  long storage;
  long symcount;
  bfd_boolean dynamic = 0;
  if (!(bfd_get_file_flags(abfd) & HAS_SYMS)) {
    printf("symtab has no syms\n");
    return;
  }
  storage = bfd_get_symtab_upper_bound(abfd);
  if (!storage) {
    storage = bfd_get_dynamic_symtab_upper_bound(abfd);
    dynamic = 1;
  }
  if (storage <= 0) {
    printf("Storage0 is 0\n");
    return;
  }
  flu->symtab = calloc(1, storage);
  if (dynamic) {
    DBGLOG("Dynamic symtab\n");
    symcount = bfd_canonicalize_dynamic_symtab(abfd, flu->symtab);
  } else {
    DBGLOG("Static symtab\n");
    symcount = bfd_canonicalize_symtab(abfd, flu->symtab);
  }
  if (symcount == 0 && !dynamic && (storage = bfd_get_dynamic_symtab_upper_bound(abfd)) > 0) {
    free(flu->symtab);
    flu->symtab = calloc(1, storage);
    symcount = bfd_canonicalize_dynamic_symtab(abfd, flu->symtab);
  }

  if (symcount <= 0) {
    printf("Storage1 is 0\n");
    free(flu->symtab);
    flu->symtab = NULL;
  }
}
static void find_address_in_section(bfd* abfd, asection* section, void* arg) {
  bfd_vma vma;
  struct FunLocLookup *flu = arg;
  const char** filename = (const char**)&flu->loc->srcpath;
  const char** functionname = (const char**)&flu->loc->funname;
  unsigned int*  line = &flu->loc->line;
  unsigned int*  discriminator = &flu->loc->disc;
  bfd_vma pc = flu->pc;

  if (flu->done) return;
//  DBGLOG("Section(%d): %s\n", section->index, section->name);

  if ((bfd_section_flags(section) & SEC_ALLOC) == 0) {
//    DBGLOG("section is not allocated\n");
    return;
  }

  vma = bfd_section_vma(section);
  if (pc < vma) {
//    DBGLOG("pc < vma\n");
    return;
  }

  if (pc >= vma + bfd_section_size(section)) {
//    DBGLOG("pc >= vma + bfd_section_size\n");
    return;
  }

  flu->done = bfd_find_nearest_line_discriminator(abfd, section, flu->symtab, pc - vma,
                                                  filename, functionname,
                                                  line, discriminator);
}

static void find_offset_in_section(bfd* abfd, asection* section, struct FunLocLookup* flu) {
  const char** filename = (const char**)&flu->loc->srcpath;
  const char** functionname = (const char**)&flu->loc->funname;
  unsigned int*  line = &flu->loc->line;
  unsigned int*  discriminator = &flu->loc->disc;
  bfd_vma pc = flu->pc;
  bfd_size_type size;

  if ((bfd_section_flags(section) & SEC_ALLOC) == 0) return;

  size = bfd_section_size(section);
  if (pc >= size) return;

  flu->done = bfd_find_nearest_line_discriminator(abfd, section, flu->symtab, pc,
                                                  filename, functionname,
                                                  line, discriminator);
}

static void translate_addresses(struct FunLocLookup* flu, asection* section, uint64_t addr) {
  bfd* abfd = flu->bfd;
  const char** filename = (const char**)&flu->loc->srcpath;
  unsigned int*  line = &flu->loc->line;
//  unsigned int*  discriminator = &flu->loc->disc;
  flu->pc = addr;

  DBGLOG("Translating address: 0x%lx\n", addr);
  if (bfd_get_flavour(abfd) == bfd_target_elf_flavour) {
    // As per binutils elf-bfd.h
    const struct elf_backend_data* bed = (const struct elf_backend_data*)abfd->xvec;
     bfd_vma sign = (bfd_vma)1 << (bed->s->arch_size - 1);
     flu->pc &= (sign << 1) - 1;
     if (bed->sign_extend_vma) flu->pc = (flu->pc ^ sign) - sign;
  }
  flu->done = FALSE;
  if (section) {
    DBGLOG("Got a section.\n");
    find_offset_in_section(abfd, section, flu);
  } else {
    DBGLOG("Did not get a section.\n");
    bfd_map_over_sections(abfd, find_address_in_section, flu);
  }
  if (!flu->done) {
    DBGLOG("Did not finish\n");
    flu->loc->funname = strdup("??");
    return;
  } else {
    if (!flu->loc->funname) {
      flu->loc->funname = strdup("??");
    } else if (!*flu->loc->funname) {
      free(flu->loc->funname); // Is this OK?
      flu->loc->funname = strdup("??");
    }
    char* buf = bfd_demangle(abfd, flu->loc->funname, DMGL_ANSI | DMGL_PARAMS);
    if(buf) {
      free(flu->loc->funname);
      flu->loc->funname = buf;
    }

    flu->done = bfd_find_inliner_info(abfd, filename, (const char**)&flu->loc->funname, line);
  }
}

static int process_file(char* file, uint64_t addr, struct FunLoc* loc) {
  struct FunLocLookup* flu = &(struct FunLocLookup){0};
  flu->loc = loc;
  char** matching;
  if(!file || !*file) return -1;
  DBGLOG("Processing file %s:%lx\n", file, addr);

  flu->bfd = bfd_openr(file, NULL);
  if (flu->bfd == NULL) {
    printf("Couldn't open file %s\n", file);
    return -1;
  }

  // Decompression stuff
  flu->bfd->flags |= BFD_DECOMPRESS;
  if (bfd_check_format(flu->bfd, bfd_archive)) {
    printf("Failed to check the format of archive.\n");
    return -1;
  }

  if (!bfd_check_format_matches(flu->bfd, bfd_object, &matching)) {
    printf("File %s is not an object.\n", bfd_get_filename(flu->bfd));
    // if (bfd_get_error() == bfd_error_file_ambiguously_recognized) {
    //   list_matching_formats(matching);
    //   free(matching);
    // }
    return -1;
  }

  slurp_symtab(flu);
  translate_addresses(flu, NULL, addr);
  if (flu->symtab != NULL) {
    free(flu->symtab);
    flu->symtab = NULL;
  }
  bfd_close(flu->bfd);

  // If we're here, we succeeded.  Finish populating loc
  loc->ip = addr;
  loc->sopath = file;
  return 0;
}


/******************************************************************************\
|*                               DWARF and ELF                                *|
\******************************************************************************/
extern int UNW_OBJ(dwarf_search_unwind_table)(unw_addr_space_t, unw_word_t, unw_dyn_info_t*, unw_proc_info_t*, int, void*);
extern int UNW_OBJ(dwarf_find_debug_frame)(int, unw_dyn_info_t*, unw_word_t, unw_word_t, const char*, unw_word_t, unw_word_t);
#define dwarf_search_unwind_table UNW_OBJ(dwarf_search_unwind_table)
#define dwarf_find_debug_frame UNW_OBJ(dwarf_find_debug_frame)
#define unwcase(x)   \
  case x:            \
    printf(#x "\n"); \
    break;

// TODO borrowed from perf
#define dw_read(ptr, type, end)  ({             \
    type* __p = (type*)ptr;                     \
    type __v;                                   \
    if ((__p + 1) > (type*)end) return -EINVAL; \
    __v = *__p++;                               \
    ptr = (__typeof__(ptr))__p;                 \
    __v;                                        \
  })

#define DW_EH_PE_FORMAT_MASK 0x0f  // format of the encoded value
#define DW_EH_PE_APPL_MASK 0x70    // how the value is to be applied
#define DW_EH_PE_omit 0xff
#define DW_EH_PE_ptr 0x00  // pointer-sized unsigned value
#define DW_EH_PE_absptr 0x00  // absolute value
#define DW_EH_PE_pcrel 0x10   // rel. to addr. of encoded value

static int __dw_read_encoded_value(uint8_t** p, uint8_t* end, uint64_t* val, uint8_t encoding) {
  uint8_t* cur = *p;
  *val = 0;

  switch (encoding) {
    case DW_EH_PE_omit:
      *val = 0;
      goto out;
    case DW_EH_PE_ptr:
      *val = dw_read(cur, unsigned long, end);
      goto out;
    default:
      break;
  }

  switch (encoding & DW_EH_PE_APPL_MASK) {
    case DW_EH_PE_absptr:
      break;
    case DW_EH_PE_pcrel:
      *val = (unsigned long)cur;
      break;
    default:
      return -EINVAL;
  }

  if ((encoding & 0x07) == 0x00) encoding |= DW_EH_PE_udata4;

  switch (encoding & DW_EH_PE_FORMAT_MASK) {
    case DW_EH_PE_sdata4:
      *val += dw_read(cur, int32_t, end);
      break;
    case DW_EH_PE_udata4:
      *val += dw_read(cur, uint32_t, end);
      break;
    case DW_EH_PE_sdata8:
      *val += dw_read(cur, int64_t, end);
      break;
    case DW_EH_PE_udata8:
      *val += dw_read(cur, uint64_t, end);
      break;
    default:
      return -EINVAL;
  }

out:
  *p = cur;
  return 0;
}

#define dw_read_encoded_value(ptr, end, enc) ({                             \
  uint64_t __v;                                                             \
  if (__dw_read_encoded_value(&(ptr), (end), &__v, (enc))) return -EINVAL;  \
  __v;                                                                      \
})


// TODO clean these up
// Based mostly on perf's util/unwind-libunwind-local.c
int unw_fpi(unw_addr_space_t as, unw_word_t ip, unw_proc_info_t* pip, int need_unwind_info, void* arg) {
  struct UnwindState* us = arg;
  uint64_t offset = 0, table_data = 0, fde_count = 0;
  int fd;
  Map* map;
  Elf* elf;
  GElf_Ehdr* ehdr = &(GElf_Ehdr){0};
  GElf_Shdr* shdr = &(GElf_Shdr){0};
  Elf_Scn* sec = NULL;

  if(!(map = procfs_MapMatch(us->pid, ip))) {
    return -UNW_EINVALIDIP;  // probably [vdso] or something
  }

  DBGLOG("ip: 0x%lx, rel: 0x%lx in %s\n", ip, ip - (map->start - map->off), map->path);

  // This is horrible, but right now we cache the current map.  The reason for
  // this is because downstream analysis may require us to read segments which
  // were never mapped in the target process, so inspecting procfs will fail.
  // Instead, now that we know the map currently being accessed, we pass it
  // downstream.  The main problem is actually if the map might change during
  // processing, but whatever
  us->map = map;

  // Get the ELF info.  We'll cache this later
  if (!( fd = open(map->path, O_RDONLY))) {
    printf("Couldn't open.  Path is %s\n", map->path);
    return -UNW_EINVALIDIP;
  }
  if (!(elf = elf_begin(fd, ELF_C_READ_MMAP, NULL))) {
    int err = elf_errno();
    printf("<ERR> %s (%s)\n", elf_errmsg(err), map->path);
    printf("Not a valid ELF header?\n");
    close(fd);
    return -UNW_EINVALIDIP;
  }

  if(!gelf_getehdr(elf, ehdr)) {
    int err = elf_errno();
    printf("<ERR> %s (%s)\n", elf_errmsg(err), map->path);
    printf("Not a valid ELF header?\n");
    close(fd);
    return -UNW_EINVAL;
  }

  if(!elf_rawdata(elf_getscn(elf, ehdr->e_shstrndx), NULL)) {
    int err = elf_errno();
    printf("<ERR> %s (%s)\n", elf_errmsg(err), map->path);
    printf("ELF is corrupted?\n");
    close(fd);
    return -UNW_EINVAL;
  }

  // Iterate through the ELF sections until the .eh_frame_hdr is found
  while ((sec = elf_nextscn(elf, sec))) {
    gelf_getshdr(sec, shdr);
    if (!strcmp(".eh_frame_hdr", elf_strptr(elf, ehdr->e_shstrndx, shdr->sh_name))) {
      offset = shdr->sh_offset;
      DBGLOG("--> Offset: %lx\n", offset);
      break;
    }
  }

  // Read the ELF segment corresponding to .eh_frame_hdr (GNU_EH_FRAME)
  // in order to find the unwind table address
  if (offset) {
    struct eh_frame_hdr efh;
    uint8_t* enc = (uint8_t*)&efh.enc;
    uint8_t* end = (uint8_t*)&efh.data;
    if (sizeof(efh) == pread(fd, &efh, sizeof(efh), offset)) {
      dw_read_encoded_value(enc, end, efh.eh_frame_ptr_enc);
      fde_count = dw_read_encoded_value(enc, end, efh.fde_count_enc);
      table_data = (enc - (uint8_t*)&efh) + offset;
      DBGLOG("--> Table Data: %lx (%ld)\n", table_data, fde_count);
    }
  }

  // Attempt to unwind
  DBGLOG("map.start: %ld, offset: %ld, map->off: %ld\n", map->start, offset, map->off);
  struct unw_dyn_info di = {.format = UNW_INFO_FORMAT_REMOTE_TABLE,
                            .start_ip = map->start,
                            .end_ip = map->end,
                            .u = {.rti = { .segbase =    (map->start - map->off) + offset,
                                           .table_data = (map->start - map->off) + table_data,
                                           .table_len = fde_count * sizeof(struct table_entry) / sizeof(unw_word_t)}}
  };


  switch(-dwarf_search_unwind_table(as, ip, &di, pip, need_unwind_info, arg)) {
  case UNW_ESUCCESS:
    // Done with the map, the file, etc
    DBGLOG("Succeeded with eh_frame dwarf_search_unwind_table: 0x%lx\n", pip->start_ip);
    us->map = NULL;
    elf_end(elf);
    close(fd);
    return UNW_ESUCCESS;
  unwcase(UNW_EUNSPEC)
  unwcase(UNW_ENOMEM)
  unwcase(UNW_EINVAL)
  unwcase(UNW_ENOINFO)
  unwcase(UNW_EBADVERSION)
  unwcase(UNW_EBADREG)
  unwcase(UNW_EREADONLYREG)
  unwcase(UNW_EINVALIDIP)
  unwcase(UNW_EBADFRAME)
  unwcase(UNW_ESTOPUNWIND)
  }

  // Now try to unwind with the debug frame
  // TODO need to find the equivalent of what perf calls symsrc_filename, by
  //      checking build-id and poking around the filesystem and stuff
  //      For now, only check self-same file as IP
  //      When that happens, also need  to update the strategy for unw_am()
  int ret = -1;
  char is_exec = 0;
  if(strcmp(".so", &map->path[strlen(map->path)-3])) {
    DBGLOG("I think %s does not end in .so\n", map->path);
    is_exec = 1;
  }
  if (dwarf_find_debug_frame(0, &di, ip, map->start - map->off, map->path, map->start, map->end)) {
    ret = dwarf_search_unwind_table(as, ip, &di, pip, need_unwind_info, arg);
    DBGLOG("Found debug frame, checking return:\n");
    switch(ret) {
    unwcase(UNW_ESUCCESS)
    unwcase(UNW_EUNSPEC)
    unwcase(UNW_ENOMEM)
    unwcase(UNW_EINVAL)
    unwcase(UNW_ENOINFO)
    unwcase(UNW_EBADVERSION)
    unwcase(UNW_EBADREG)
    unwcase(UNW_EREADONLYREG)
    unwcase(UNW_EINVALIDIP)
    unwcase(UNW_EBADFRAME)
    unwcase(UNW_ESTOPUNWIND)
    }
    us->map = NULL;
    elf_end(elf);
    close(fd);
    return ret;
  }

  DBGLOG("Failure and no debug frame...\n");
  us->map = NULL;
  elf_end(elf);
  close(fd);
  return -UNW_ESTOPUNWIND;
}

void unw_pui(unw_addr_space_t as, unw_proc_info_t* pip, void* arg) { (void)as; (void)pip; (void)arg; DBGLOG("HERE.");}

int unw_gdila(unw_addr_space_t as, unw_word_t* dilap, void* arg) {(void)as; (void)dilap; (void)arg; return -UNW_ENOINFO; }  // punt

int unw_am(unw_addr_space_t as, unw_word_t _addr, unw_word_t* valp, int write, void* arg) {
(void)as;
  struct UnwindState* us = arg;
  unw_word_t addr = _addr;
  if (write || !us->stack) {
    *valp = 0;
    return -UNW_EINVAL;  // not supported
  }

  // Start and end of stack addresses
  const uint64_t sp_start = us->esp;
  const uint64_t sp_end = sp_start + us->stack_sz;

  // Check overflow, like perf
  if (addr + sizeof(unw_word_t) < addr)
    return -EINVAL;
  if (sp_start <= addr && addr + sizeof(unw_word_t) < sp_end) {
    *valp = *(unw_word_t*)(&us->stack[addr - sp_start]);
    return UNW_ESUCCESS;
  }

  Map* map;
  if(us->map) map = us->map;
  else {
    map = procfs_MapMatch(us->pid, us->eip);
//    DBGLOG("No global map cache.  Got %s\n", map->path);
  }

  // Now try to read, given the map.  This assumes that the address is in the
  // scope of the instrumented process.
  if (map) {
    if (addr < (map->start - map->off)) {
      // I don't really understand how it's possible that an IP isn't adjusted,
      // to the filespace, but I suppose this is true if libunwind ever pushes
      // a direct file address back into unw_am.  We can fix it.
      DBGLOG("[AM: EADJUST] addr = %lx, map_start = %lx, map_off = %lx, segbase = %lx!\n", addr, map->start,  map->off, map->start - map->off);
      addr += (map->start - map->off);
    }

    if (-1 == procfs_MapRead(map, valp, sizeof(*valp), addr - (map->start - map->off))) {
      DBGLOG("Reading failed!\n");
      *valp = 0; // Reset whatever valp is
      return -UNW_EINVALIDIP;
    }
    DBGLOG("mem[%016lx] -> %lx (%50s)\n", addr, *valp, map->path);
    return UNW_ESUCCESS;
  }

  // We land here if we didn't have a map.
  return -UNW_EINVAL;
}

int unw_ar(unw_addr_space_t as, unw_regnum_t regnum, unw_word_t* valp, int write, void* arg) {
(void)as;
  struct UnwindState* us = arg;
  if (write) return -UNW_EREADONLYREG;

  switch(regnum) {
    case UNW_X86_64_RBP:
      *valp = us->ebp;
      break;
    case UNW_X86_64_RSP:
      *valp = us->esp;
      break;
    case UNW_X86_64_RIP:
      *valp = us->eip;
      break;
    default:
      return -UNW_EBADREG;
  }

  return UNW_ESUCCESS;
}

int unw_af(unw_addr_space_t as, unw_regnum_t regnum, unw_fpreg_t* fpvalp, int write, void* arg) { (void)as; (void)regnum; (void)fpvalp; (void)write; (void)arg; return -UNW_EINVAL; }

int unw_res(unw_addr_space_t as, unw_cursor_t* cp, void* arg) { (void)as; (void)cp; (void)arg; return -UNW_EINVAL; }

int unw_gpn(unw_addr_space_t as, unw_word_t addr, char* bufp, size_t buf_len, unw_word_t* offp, void* arg) { (void)as; (void)addr; (void)bufp; (void)buf_len; (void)offp; (void)arg; DBGLOG(".");return -UNW_EINVAL; }

unw_accessors_t unwAccessors = {.find_proc_info = unw_fpi,
                                .put_unwind_info = unw_pui,
                                .get_dyn_info_list_addr = unw_gdila,
                                .access_mem = unw_am,
                                .access_reg = unw_ar,
                                .access_fpreg = unw_af,
                                .resume = unw_res,
                                .get_proc_name = unw_gpn};

char unwindstate_Init(struct UnwindState* us) {
  us->uas = unw_create_addr_space(&unwAccessors, 0);
  if (!us->uas) return 1;
  unw_set_caching_policy(us->uas, UNW_CACHE_GLOBAL);
  bfd_init();
  bfd_set_default_target("x86_64-pc-linux-gnu");
  return 0;
}


void funloclookup_Init(struct FunLocLookup* flu, char* file) {
  flu->bfd = bfd_openr(file, NULL);
  if (!flu->bfd) {
    printf("I could not open the file.  Bummer.\n");
  }

  if (bfd_check_format(flu->bfd, bfd_archive)) {
    printf("Hey, I got an archive.\n");
    flu->bfd = bfd_openr_next_archived_file(flu->bfd, NULL);
  } else if (bfd_check_format(flu->bfd, bfd_object)) {
    printf("Hey, I got a straight object.\n");
  } else {
    printf("Hey, I did not get an object nor an archive... WTF?\n");
    bfd_close(flu->bfd);
    return;
  }

  if (bfd_get_file_flags(flu->bfd) & HAS_SYMS) {
    //    uint32_t sz;
    //    uint32_t cnt = bfd_read_minisymbols(flu->bfd, 0, (void**)flu->symtab, &sz);
    //    if (cnt<0)
    //      cnt = bfd_read_minisymbols(flu->bfd, 1, (void**)flu->symtab, &sz);
    //
    //    if (cnt<0) {
    //      printf("Failed at reading the minisymbols\n");
    //    }
    slurp_symtab(flu);
  }
  bfd_close(flu->bfd);
}

void funloc_bfdmapoversections_callback(bfd* bf, asection* sec, void* arg) {
(void)bf;
(void)sec;
(void)arg;
  //  struct FunLocLookup *lu = arg;
  //  struct FunLoc* loc = lu->loc;
  //
  //
  //  // If we're done, we're done...  TODO, can we short-circuit the caller?!
  //  if (lu->done)
  //    return;
  //
  //  // If the section isn't allocated, we're not in a VMA... Done!
  //  if (!(bfd_section_flags(sec) & SEC_ALLOC))
  //    return;
  //
  //  // If the IP isn't in this VMA, we're done.
  //  bfd_vma sec_vma = bfd_section_vma(sec);
  //  printf("%d %d\n", loc->ip < sec_vma , loc->ip >= sec_vma + bfd_section_size(sec));
  //  if (loc->ip < sec_vma || loc->ip >= sec_vma + bfd_section_size(sec))
  //    return;
  //
  //  // If we're here, then we're going to be done soon!  But we're not done yet
  //  const char* funcname = NULL;
  //  const char* filename = NULL;
  //  static unsigned int discriminator;
  ////  lu->done = bfd_find_nearest_line(bf, sec, &lu->symtab, loc->ip - sec_vma, (const char**)&loc->file, (const char**)&loc->name, &loc->line);
  //  lu->done = bfd_find_nearest_line_discriminator(bf, sec, &lu->symtab, loc->ip - sec_vma, &filename, &funcname, &loc->line, &discriminator);
}

void funloclookup_Set(struct FunLocLookup* flu, uint64_t ip, pid_t pid) {
  DBGLOG("Looking up function IP = %lx\n", ip);
  Map* map = procfs_MapMatch(pid, ip);
  if (!map) {
    printf("NOMAP\n");
    return;
  }
  flu->loc->ip = ip - map->start + map->off;
  flu->loc->srcpath = map->path;

  funloclookup_Init(flu, map->path);
  bfd_map_over_sections(flu->bfd, funloc_bfdmapoversections_callback, flu);
}

static int process_ip(pid_t pid, uint64_t addr, struct FunLoc* loc) {
  int ret = -1;
  DBGLOG("Processing IP = %lx\n", addr);
  Map* map = procfs_MapMatch(pid, addr);
  if (!map) {
    DBGLOG("Failed to find map\n");
    exit(-1);
    return -1;
  }
  loc->map_start = map->start;
  loc->map_end = map->end;
  loc->map_off = map->off;

  // I'm not quite sure what I did to make this necessary, but I sure need to
  // figure out why
  if(!strcmp(".so", &map->path[strlen(map->path)-3]))
    ret = process_file(map->path, addr - (map->start - map->off), loc);
  else
    ret = process_file(map->path, addr, loc);
  return ret;
}

int unwindstate_unwind(struct UnwindState* us, struct FunLoc* locs, int max_stack) {
  int n = 0, ret = -1;
  unw_cursor_t uc;
  uint64_t ips[max_stack];

  if((ret = unw_init_remote(&uc, us->uas, us))) {
    printf("Could not initialize unw remote context.\n");
    return 0;
  }

  // Get the instruction pointers.  The first one is in EIP, unw for rest
  ips[n++] = us->eip;

  while (0 < (ret = unw_step(&uc)) && n < max_stack) {
    unw_get_reg(&uc, UNW_REG_IP, &ips[n]);
    if(unw_is_signal_frame(&uc) <= 0) --ips[n];
    n++;
  }

  // Now get the information into the output container
  memset(locs, 0, n * sizeof(struct FunLoc));
  for (int i = 0; i < n; i++) {
    DBGLOG("Processing step %d, ip = %lx\n", i, ips[i]);
    process_ip(us->pid, ips[i], &locs[i]);
    DBGLOG("Found location: %s\n", locs[i].funname);
  }
  return n;
}

#endif
