#ifndef _H_unwind
#define _H_unwind

#include <bfd.h>
#include <gelf.h>
#include <libunwind.h>

#include "/usr/include/libdwarf/dwarf.h"  // TODO wow... Just wow
#include "procutils.h"

// TODO: remove this immediately
struct TTableEntry {
  uint32_t startIpOffset;
  uint32_t fdeOffset;
};

struct EhHdr {
  uint8_t version;
  uint8_t ehFramePtrEnc;
  uint8_t fdeCountEnc;
  uint8_t tableEnc;

  uint64_t enc[2u];

  uint8_t data[];
} __attribute__((packed));

struct Dwarf {
  uint8_t* ptr;
  const uint8_t* end;
};

// TODO what else is needed for inline functions?
struct FunLoc {
  uint64_t ip;    // Relative to file, not VMA
  char* name;     // name of the function (mangled, possibly)
  char* source;   // name of the source file, if known
  char* file;     // name of the file where the symbol is interned (e.g., .so)
  uint32_t line;  // line number in file
};

struct FunLocLookup {
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
};

/******************************************************************************\
|*                               Symbol Lookup                                *|
\******************************************************************************/
static void slurp_symtab(struct FunLocLookup* flu) {
  bfd* abfd = flu->bfd;
  long storage;
  long symcount;
  bfd_boolean dynamic = 0;
  if (!(bfd_get_file_flags(abfd) & HAS_SYMS)) return;
  storage = bfd_get_symtab_upper_bound(abfd);
  if (!storage) {
    storage = bfd_get_dynamic_symtab_upper_bound(abfd);
    dynamic = 1;
  }
  if (storage < 0) {
    printf("Storage is 0\n");
    return;
  }
  flu->symtab = calloc(1, storage);
  if (dynamic)
    symcount = bfd_canonicalize_dynamic_symtab(abfd, flu->symtab);
  else
    symcount = bfd_canonicalize_symtab(abfd, flu->symtab);
  if (symcount < 0) {
    printf("Storage is 0\n");
    return;
  }
}

static void translate_addresses(bfd* abfd, asection* section, bfd_vma pc, uint64_t* addr, size_t naddr) {
  int read_stdin = (naddr == 0);
  static char found = 0;
  for (;;) {
    if (naddr <= 0) break;
    --naddr;
    pc = *addr++;
    if (bfd_get_flavour(abfd) == bfd_target_elf_flavour) {
      const struct elf_backend_data* bed = get_elf_backend_data(abfd);
      bfd_vma sign = (bfd_vma)1 << (bed->s->arch_size - 1);
      pc &= (sign << 1) - 1;
      if (bed->sign_extend_vma) pc = (pc ^ sign) - sign;
    }
    if (1) {
      printf("0x");
      bfd_printf_vma(abfd, pc);
      if (pretty_print)
        printf(": ");
      else
        printf("\n");
    }
    found = FALSE;
    if (section)
      find_offset_in_section(abfd, section);
    else
      bfd_map_over_sections(abfd, find_address_in_section, NULL);
    if (!found) {
      if (1) printf("??\n");
      printf("??:0\n");
    } else {
      while (1) {
        if (with_functions) {
          const char* name;
          char* alloc = NULL;
          name = functionname;
          if (name == NULL || *name == '\0')
            name = "??";
          else if (do_demangle) {
            alloc = bfd_demangle(abfd, name, DMGL_ANSI | DMGL_PARAMS);
            if (alloc != NULL) name = alloc;
          }
          printf("%s", name);
          if (pretty_print)
            printf(_(" at "));
          else
            printf("\n");
          if (alloc != NULL) free(alloc);
        }
        if (base_names && filename != NULL) {
          char* h;
          h = strrchr(filename, '/');
          if (h != NULL) filename = h + 1;
        }
        printf("%s:", filename ? filename : "??");
        if (line != 0) {
          if (discriminator != 0)
            printf("%u (discriminator %u)\n", line, discriminator);
          else
            printf("%u\n", line);
        } else
          printf("?\n");
        if (!unwind_inlines)
          found = FALSE;
        else
          found = bfd_find_inliner_info(abfd, &filename, &functionname, &line);
        if (!found) break;
        if (pretty_print) printf(_(" (inlined by) "));
      }
    }
  }
}

static int process_file(const char* file_name, const char* section_name, struct FunLocLookup* flu) {
  asection* section;
  char** matching;
  flu->bfd = bfd_openr(file_name, NULL);
  if (flu->bfd == NULL) {
    printf("Couldn't open file %s\n", file_name);
    return -1;
  }

  // Decompression stuff
  flu->bfd->flags |= BFD_DECOMPRESS;
  if (bfd_check_format(flu->bfd, bfd_archive)) {
    printf("Asked to process archive.\n");
    return -1;
  }

  if (!bfd_check_format_matches(flu->bfd, bfd_object, &matching)) {
    printf("File %s is not an object.\n", bfd_get_filename(flu->bfd));
    if (bfd_get_error() == bfd_error_file_ambiguously_recognized) {
      list_matching_formats(matching);
      free(matching);
    }
    return -1;
  }

  if (section_name != NULL) {
    section = bfd_get_section_by_name(flu->bfd, section_name);
    if (!section) {
      printf("Couldn't get section\n");
      return -1;
    }
  } else {
    section = NULL;
  }
  slurp_symtab(flu);
  translate_addresses(flu->bfd, section);
  if (flu->symtab != NULL) {
    free(flu->symtab);
    flu->symtab = NULL;
  }
  bfd_close(flu->bfd);
  return 0;
}

/******************************************************************************\
|*                               DWARF and ELF                                *|
\******************************************************************************/
// Convenience
#define dwarf_search_unwind_table UNW_OBJ(dwarf_search_unwind_table)
#define unwcase(x)   \
  case x:            \
    printf(#x "\n"); \
    break;

// TODO wow, just wow.
char dwarf_readEhFrameValueInt(struct Dwarf* dwarf, uint64_t* out, size_t sz) {
  uint64_t* pptr = (uint64_t*)dwarf->ptr;
  uint64_t* pend = (uint64_t*)dwarf->end;

  if ((pptr + 1u) <= pend) {
    *out += *pptr++;
    *dwarf->ptr += sz;
    return 1;
  }

  return 0;
}

// TODO borrowed from perf
#define dw_read(ptr, type, end)                 \
  ({                                            \
    type* __p = (type*)ptr;                     \
    type __v;                                   \
    if ((__p + 1) > (type*)end) return -EINVAL; \
    __v = *__p++;                               \
    ptr = (__typeof__(ptr))__p;                 \
    __v;                                        \
  })

#define DW_EH_PE_FORMAT_MASK 0x0f  // format of the encoded value
#define DW_EH_PE_APPL_MASK 0x70    // how the value is to be applied

/* Pointer-encoding formats: */
#define DW_EH_PE_omit 0xff
#define DW_EH_PE_ptr 0x00  // pointer-sized unsigned value

/* Pointer-encoding application: */
#define DW_EH_PE_absptr 0x00  // absolute value
#define DW_EH_PE_pcrel 0x10   // rel. to addr. of encoded value

/*
 * The following are not documented by LSB v1.3, yet they are used by
 * GCC, presumably they aren't documented by LSB since they aren't
 * used on Linux:
 */
#define DW_EH_PE_funcrel 0x40  // start-of-procedure-relative
#define DW_EH_PE_aligned 0x50  // aligned pointer

static int dw_read_encoded_value(uint8_t** p, uint8_t* end, uint64_t* val, uint8_t encoding) {
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

char dwarf_readEhFrameValue(struct Dwarf* dwarf, uint64_t* val, uint8_t enc) {
  *val = 0;

  switch (enc) {
    case DW_EH_PE_omit:
      return 1;
    case DW_EH_PE_absptr:
      return dwarf_readEhFrameValueInt(dwarf, val, 8);
  }

  switch (enc & 0x70) {
    case DW_EH_PE_absptr:
      break;
    case DW_EH_PE_pcrel:
      *val = (uint64_t)dwarf->ptr;
      break;
    default:
      return 0;
  }

  switch (enc & 0x0f) {
    case DW_EH_PE_sdata4:
      return dwarf_readEhFrameValueInt(dwarf, val, 4);
    case DW_EH_PE_udata4:
      return dwarf_readEhFrameValueInt(dwarf, val, 4);
    case DW_EH_PE_sdata8:
      return dwarf_readEhFrameValueInt(dwarf, val, 8);
    case DW_EH_PE_udata8:
      return dwarf_readEhFrameValueInt(dwarf, val, 8);
  }
  return 0;
}

#define casegelf(x)  \
  case x:            \
    printf(#x "\n"); \
    break;

// TODO clean these up
// Based mostly on perf's util/unwind-libunwind-local.c
int unw_fpi(unw_addr_space_t as, unw_word_t ip, unw_proc_info_t* pip, int need_unwind_info, void* arg) {
  struct UnwindState* us = arg;
  Map* map = procfs_MapMatch(us->pid, ip);
  if (!map) {
    printf("No map.  IP is 0x%lx\n", ip);
    return -UNW_EINVALIDIP;  // probably [vdso] or something
  }

  // Get the ELF info.  We'll cache this later
  int fd = open(map->path, O_RDONLY);
  if (-1 == fd) {
    printf("Couldn't open.  Path is %s\n", map->path);
    return -UNW_EINVALIDIP;
  }
  Elf* elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
  GElf_Ehdr ehdr;
  GElf_Shdr shdr;
  uint64_t offset = 0, table_data = 0, fde_count = 0;
  if (!elf) {
    int err = elf_errno();
    printf("<ERR> %s (%s)\n", elf_errmsg(err), map->path);
    printf("Not a valid ELF header?\n");
    close(fd);
    exit(-1);
    return -UNW_EINVALIDIP;
  }

  gelf_getehdr(elf, &ehdr);
  elf_rawdata(elf_getscn(elf, ehdr.e_shstrndx), NULL);

  Elf_Scn* sec = NULL;
  while ((sec = elf_nextscn(elf, sec))) {
    gelf_getshdr(sec, &shdr);
    if (!strcmp(".eh_frame_hdr", elf_strptr(elf, ehdr.e_shstrndx, shdr.sh_name))) {
      offset = shdr.sh_offset;
      break;
    }
  }

  elf_end(elf);

  if (offset) {
    struct EhHdr ehhdr = {0};
    if (sizeof(struct EhHdr) == pread(fd, &ehhdr, sizeof(struct EhHdr), offset)) {
      uint64_t fptr;  // Unused
      uint8_t* enc = (uint8_t*)&ehhdr.enc;
      uint8_t* end = (uint8_t*)&ehhdr.data;

      // Skip the eh_frame_ptr
      if (dw_read_encoded_value(&enc, end, &fptr, ehhdr.ehFramePtrEnc)) return -UNW_EINVAL;
      if (dw_read_encoded_value(&enc, end, &fde_count, ehhdr.fdeCountEnc)) return -UNW_EINVAL;
      table_data = (enc - (uint8_t*)&ehhdr) + offset;
    }
  }

  // Done with the map
  close(fd);

  // Attempt to unwind
  struct unw_dyn_info di = {.format = UNW_INFO_FORMAT_REMOTE_TABLE,
                            .start_ip = map->start,
                            .end_ip = map->end,
                            .u = {.rti = { .segbase = map->start + offset - map->off,
                                           .table_data = map->start + table_data - map->off,
                                           .table_len = fde_count * sizeof(struct TTableEntry) / sizeof(unw_word_t)}}
  };

  switch(-dwarf_search_unwind_table(as, ip, &di, pip, need_unwind_info, arg)) {
  case UNW_ESUCCESS:
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

  printf("dwarf_search failed\n");
  return -UNW_ESTOPUNWIND;
}

void unw_pui(unw_addr_space_t as, unw_proc_info_t* pip, void* arg) {}

int unw_gdila(unw_addr_space_t as, unw_word_t* dilap, void* arg) { return -UNW_ENOINFO; }  // punt

int unw_am(unw_addr_space_t as, unw_word_t addr, unw_word_t* valp, int write, void* arg) {
  // libunwind will use this function to read a word of memory
  struct UnwindState* us = arg;
  if (write || !us->stack) return -UNW_EINVAL;  // not supported

  // Start and end of stack addresses
  const uint64_t sp_start = us->esp;
  const uint64_t sp_end = us->esp + us->stack_sz;

  if (sp_start <= addr && addr + sizeof(unw_word_t) < sp_end) {
    *valp = *(unw_word_t*)(&us->stack[addr - sp_start]);
    return UNW_ESUCCESS;
  }

  // We want to read a piece of memory which is inside of a shared object.
  // Since we build all of the offsets from procfs, we may actually want to read
  // from a non-executable page (or an executable page that hasn't been loaded
  // into the target process).
  // Accordingly, we generate a map by opening according to the current value of
  // the instruction pointer, then treat `addr` relative to that.
  Map* map = procfs_MapMatch(us->pid, us->eip);
  if (map) {
    if (-1 == procfs_MapRead(map, valp, addr - map->start + map->off, sizeof(*valp))) {
      printf("Something went wrong processing %s\n", map->path);
      return -UNW_EINVALIDIP;
    }
    return UNW_ESUCCESS;
  } else {
    PidMap* pm = mapcache_Get(us->pid);
    if (!pm)
      printf("Failed to get map...\n");
    else {
      printf("Got pidmap, but couldn't get map\n");
    }
  }

  return -UNW_EINVAL;
}

int unw_ar(unw_addr_space_t as, unw_regnum_t regnum, unw_word_t* valp, int write, void* arg) {
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

int unw_af(unw_addr_space_t as, unw_regnum_t regnum, unw_fpreg_t* fpvalp, int write, void* arg) { return -UNW_EINVAL; }

int unw_res(unw_addr_space_t as, unw_cursor_t* cp, void* arg) { return -UNW_EINVAL; }

int unw_gpn(unw_addr_space_t as, unw_word_t addr, char* bufp, size_t buf_len, unw_word_t* offp, void* arg) {
  return -UNW_EINVAL;
}

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
  return 0;
}


void funloclookup_Init(struct FunLocLookup* flu, char* file) {
  static char initialized = 0;
  bfd* bfd;
  if (!initialized) bfd_init(), initialized = 1;

  flu->bfd = bfd_openr(file, NULL);
  if (!flu->bfd) {
    printf("I could not open the file.  Bummer.\n");
  }

  if (bfd_check_format(flu->bfd, bfd_archive)) {
    printf("Hey, I got an archive.\n");
    bfd = bfd_openr_next_archived_file(flu->bfd, NULL);
  } else if (bfd_check_format(flu->bfd, bfd_object)) {
    printf("Hey, I got a straight object.\n");
    bfd = flu->bfd;
  } else {
    printf("Hey, I did not get an object nor an archive... WTF?\n");
    bfd_close(bfd);
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
  if (bfd != flu->bfd) bfd_close(bfd);
  bfd_close(flu->bfd);
}

void funloc_bfdmapoversections_callback(bfd* bf, asection* sec, void* arg) {
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
  Map* map = procfs_MapMatch(pid, ip);
  if (!map) { } // TODO now what?
  if (!map) printf("NOMAP\n");
  flu->loc->ip = ip - map->start + map->off;
  flu->loc->file = map->path;

  funloclookup_Init(flu, map->path);
  bfd_map_over_sections(flu->bfd, funloc_bfdmapoversections_callback, flu);
}

int unwindstate_unwind(struct UnwindState* us, uint64_t* ips, size_t max_stack) {
  int ret = 0, i = 0;
  unw_cursor_t uc;

  // Get the instruction pointers.  The first one is in EIP, unw for rest
  printf("IP Addresses:\n");
  printf(" 0x%lx\n", us->eip);
  ips[i++] = us->eip;

  unw_init_remote(&uc, us->uas, us);
  while (unw_step(&uc) > 0) {
    unw_word_t l_reg;
    unw_get_reg(&uc, UNW_REG_IP, &ips[i++]);
    printf("  0x%lx\n", ips[i - 1]);
  }

  // Now get the information
  struct FunLoc locs[i];
  memset(locs, 0, i * sizeof(struct FunLoc));
  for (int j = 0; j<i; j++) {
    //    struct FunLocLookup flu = {
    //      .loc = &locs[j],
    //      .symtab = &(struct bfd_symbol){0},
    //      .bfd    = &(bfd){0}
    //    };
    //    funloclookup_Set(&flu, ips[j], us->pid);
    //
    //    printf("Function name: %s\n", locs[i].name);
  }
  return i;
}

#endif
