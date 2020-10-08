#ifndef _H_unwind
#define _H_unwind

#define UNW_LOCAL_ONLY
#include <libunwind.h>
#include <gelf.h>
#include "/usr/include/libdwarf/dwarf.h" // TODO wow... Just wow

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

struct AddressSpaceSegment {
  uint8_t type;
  char*   name;
  uint64_t start;
  uint64_t end;
  uint64_t off;
  char     readable;
  char     writable;
  char     executable;
  char     private;
};

struct AddressMap {
  uint8_t*            cache;
  struct AddressSpaceSegment segment;
  char*               filename;
};

struct Ctxt {
  struct StackCtxt*       stack;
  struct AddressSpaceSegment*    addrspace;
  char**           files;
  Map**            maps;
  unw_addr_space_t uas;

};

struct Dwarf {
  uint8_t*       ptr;
  const uint8_t* end;
};

struct UnwindState {
  pid_t    pid;
  char*    stack;     // stack dump, probably from perf sample
  size_t   stack_sz;
  union {
    uint64_t regs[3];
    struct {
      uint64_t ebp;
      uint64_t esp;
      uint64_t eip;
    };
  };
};

// TODO wow, just wow.
char dwarf_readEhFrameValueInt(struct Dwarf* dwarf, uint64_t* out, size_t sz) {
  uint64_t* pptr = (uint64_t*)dwarf->ptr;
  uint64_t* pend = (uint64_t*)dwarf->end;

  if((pptr + 1u) <= pend) {
    *out += *pptr++;
    *dwarf->ptr += sz;
    return 1;
  }

  return 0;
}

char dwarf_readEhFrameValue(struct Dwarf* dwarf, uint64_t* val, uint8_t enc) {
  printf(".");
  val = 0;

  switch(enc) {
  case DW_EH_PE_omit:
    return 1;
  case DW_EH_PE_absptr:
    return dwarf_readEhFrameValueInt(dwarf, val, 8);
  }

  switch(enc & 0x70) {
  case DW_EH_PE_absptr:
    break;
  case DW_EH_PE_pcrel:
    *val = (uint64_t)dwarf->ptr;
    break;
  default:
    return 0;
  }

  switch(enc & 0x0f) {
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

// TODO clean these up
// Based mostly on perf's util/unwind-libunwind-local.c
int unw_fpi(unw_addr_space_t as,
            unw_word_t ip, unw_proc_info_t *pip,
            int need_unwind_info, void *arg) {

  struct UnwindState* us = arg;
  Map* map = procfs_MapMatch(us->pid, ip);

  // Get the ELF info.  We'll cache this later
  int fd = open(map->path, O_RDONLY);
  if(-1 == fd)
    return -UNW_EINVALIDIP;
  Elf* elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
  GElf_Ehdr ehdr;
  GElf_Shdr shdr;
  uint64_t offset = 0, table_data = 0, fde_count = 0;
  if(!elf)
    return -UNW_EINVALIDIP;

  if(!gelf_getehdr(elf, &ehdr) || elf_rawdata(elf_getscn(elf, ehdr.e_shstrndx), NULL))
    return -UNW_ESTOPUNWIND;

  Elf_Scn* sec;
  while((sec = elf_nextscn(elf, sec))) {
    gelf_getshdr(sec, &shdr);
    if(!strcmp(".eh_frame_hdr", elf_strptr(elf, ehdr.e_shstrndx, shdr.sh_name))) {
      offset = shdr.sh_offset;
      break;
    }
  }

  elf_end(elf);

  if(offset) {
    struct EhHdr ehhdr = {0};
    if(sizeof(struct EhHdr) == pread(fd, &ehhdr, sizeof(struct EhHdr), offset)) {
      uint64_t fptr;
      struct Dwarf dwarf = {.ptr = (uint8_t*)&ehhdr.enc[0], .end = ehhdr.data};
      if(dwarf_readEhFrameValue(&dwarf, &fptr, ehhdr.ehFramePtrEnc) &&
         dwarf_readEhFrameValue(&dwarf, &fde_count, ehhdr.fdeCountEnc)) {
        table_data = (dwarf.ptr - (uint8_t*)&ehhdr) + offset;
      }
    }
  }

  // Attempt to unwind
  struct unw_dyn_info di = {
    .format   = UNW_INFO_FORMAT_REMOTE_TABLE,
    .start_ip = map->start,
    .end_ip   = map->end,
    .u = {.rti = {
      .segbase    = map->start + offset     - map->off, // ELF section offset
      .table_data = map->start + table_data - map->off,
      .table_len = fde_count*sizeof(struct TTableEntry)/sizeof(unw_word_t)}}
  };

  if(!UNW_OBJ(dwarf_search_unwind_table)(as, ip, &di, pip, NULL))
    return UNW_ESUCCESS;

  return -UNW_ESTOPUNWIND;
}

void unw_pui(unw_addr_space_t as,
             unw_proc_info_t *pip, void *arg) {}
int unw_gdila(unw_addr_space_t as,
              unw_word_t *dilap, void *arg) {return -UNW_ENOINFO;} // punt
int unw_am(unw_addr_space_t as,
           unw_word_t addr, unw_word_t *valp,
           int write, void *arg) {
  // libunwind will use this function to read a word of memory
  struct UnwindState* us = arg;
  if(write)
    return -UNW_EINVAL;  // not supported

  // Start and end of stack addresses
  const uint64_t p_start = us->esp;
  const uint64_t p_end   = us->esp + us->stack_sz;

  if(p_start <= addr && addr + sizeof(unw_word_t) < p_end) {
    *valp = *(unw_word_t*)(&us->stack[addr - p_start]);
    return UNW_ESUCCESS;
  }

  pid_t pid = *(pid_t*)arg;
  Map* map = procfs_MapMatch(pid, addr);
  if(map) {
    procfs_MapRead(map, (void*)*valp, addr - map->start + map->off, 2); // read 1 word
    return UNW_ESUCCESS;
  }

  return -UNW_EINVAL;
}

int unw_ar(unw_addr_space_t as,
           unw_regnum_t regnum, unw_word_t *valp,
           int write, void *arg) {
  struct UnwindState* us = arg;
  if(write)
    return -UNW_EREADONLYREG;

  switch(regnum) {
  case UNW_X86_64_RBP: *valp = us->regs[0]; break;
  case UNW_X86_64_RSP: *valp = us->regs[1]; break;
  case UNW_X86_64_RIP: *valp = us->regs[2]; break;
  default:
    return -UNW_EBADREG;
  }

}
int unw_af(unw_addr_space_t as,
           unw_regnum_t regnum, unw_fpreg_t *fpvalp,
           int write, void *arg) {return -UNW_EINVAL;} // Not relevant for us, we don't care about the FP registers
int unw_res(unw_addr_space_t as,
            unw_cursor_t *cp, void *arg) {return -UNW_EINVAL;} // Not needed.  We don't unw_resume()
int unw_gpn(unw_addr_space_t as,
            unw_word_t addr, char *bufp,
            size_t buf_len, unw_word_t *offp,
            void *arg) {return -UNW_EINVAL;}

unw_accessors_t unwAccessors = {
  .find_proc_info         = unw_fpi,
  .put_unwind_info        = unw_pui,
  .get_dyn_info_list_addr = unw_gdila,
  .access_mem             = unw_am,
  .access_reg             = unw_ar,
  .access_fpreg           = unw_af,
  .resume                 = unw_res,
  .get_proc_name          = unw_gpn
};

#endif
