/*
 * Copyright 2022 Nick Ripley
 * Copyright 2017 Andrei Pangin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Modified by Nick Ripley to extract components needed for call stack unwinding
 */

#ifdef __linux__

#  include "dwarf.h"
#  include "symbols.h"
#  include <elf.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <linux/limits.h>
#  include <set>
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>

#  include "elf_helpers.h"
#  include <dlfcn.h>
#  include <libelf.h>

#  define LG_WRN(...) printf(__VA_ARGS__)

class SymbolDesc {
private:
  const char *_addr;
  const char *_type;

public:
  SymbolDesc(const char *s) {
    _addr = s;
    _type = strchr(_addr, ' ') + 1;
  }

  const char *addr() { return (const char *)strtoul(_addr, NULL, 16); }
  char type() { return _type[0]; }
  const char *name() { return _type + 2; }
};

class MemoryMapDesc {
private:
  const char *_addr;
  const char *_end;
  const char *_perm;
  const char *_offs;
  const char *_dev;
  const char *_inode;
  const char *_file;

public:
  MemoryMapDesc(const char *s) {
    _addr = s;
    _end = strchr(_addr, '-') + 1;
    _perm = strchr(_end, ' ') + 1;
    _offs = strchr(_perm, ' ') + 1;
    _dev = strchr(_offs, ' ') + 1;
    _inode = strchr(_dev, ' ') + 1;
    _file = strchr(_inode, ' ');

    if (_file != NULL) {
      while (*_file == ' ')
        _file++;
    }
  }

  const char *file() { return _file; }
  bool isReadable() { return _perm[0] == 'r'; }
  bool isExecutable() { return _perm[2] == 'x'; }
  const char *addr() { return (const char *)strtoul(_addr, NULL, 16); }
  const char *end() { return (const char *)strtoul(_end, NULL, 16); }
  unsigned long offs() { return strtoul(_offs, NULL, 16); }
  unsigned long dev() {
    return strtoul(_dev, NULL, 16) << 8 | strtoul(_dev + 3, NULL, 16);
  }
  unsigned long inode() { return strtoul(_inode, NULL, 10); }
};

#  ifdef __LP64__
const unsigned char ELFCLASS_SUPPORTED = ELFCLASS64;
typedef Elf64_Ehdr ElfHeader;
typedef Elf64_Shdr ElfSection;
typedef Elf64_Phdr ElfProgramHeader;
typedef Elf64_Nhdr ElfNote;
typedef Elf64_Sym ElfSymbol;
typedef Elf64_Rel ElfRelocation;
typedef Elf64_Dyn ElfDyn;
#    define ELF_R_TYPE ELF64_R_TYPE
#    define ELF_R_SYM ELF64_R_SYM
#  else
const unsigned char ELFCLASS_SUPPORTED = ELFCLASS32;
typedef Elf32_Ehdr ElfHeader;
typedef Elf32_Shdr ElfSection;
typedef Elf32_Phdr ElfProgramHeader;
typedef Elf32_Nhdr ElfNote;
typedef Elf32_Sym ElfSymbol;
typedef Elf32_Rel ElfRelocation;
typedef Elf32_Dyn ElfDyn;
#    define ELF_R_TYPE ELF32_R_TYPE
#    define ELF_R_SYM ELF32_R_SYM
#  endif // __LP64__

#  if defined(__x86_64__)
#    define R_GLOB_DAT R_X86_64_GLOB_DAT
#  elif defined(__i386__)
#    define R_GLOB_DAT R_386_GLOB_DAT
#  elif defined(__arm__) || defined(__thumb__)
#    define R_GLOB_DAT R_ARM_GLOB_DAT
#  elif defined(__aarch64__)
#    define R_GLOB_DAT R_AARCH64_GLOB_DAT
#  elif defined(__PPC64__)
#    define R_GLOB_DAT R_PPC64_GLOB_DAT
#  else
#    error "Compiling on unsupported arch"
#  endif

// GNU dynamic linker relocates pointers in the dynamic section, while musl
// doesn't. A tricky case is when we attach to a musl container from a glibc
// host.
#  ifdef __musl__
#    define DYN_PTR(ptr) (_base + (ptr))
#  else
#    define DYN_PTR(ptr)                                                       \
      ((char *)(ptr) >= _base ? (char *)(ptr) : _base + (ptr))
#  endif // __musl__

class ElfParser {
public:
  CodeCache *_cc;
  const char *_base;
  const char *_file_name;
  ElfHeader *_header;
  const char *_sections;

  ElfParser(CodeCache *cc, const char *base, const void *addr,
            const char *file_name = NULL) {
    _cc = cc;
    _base = base;
    _file_name = file_name;
    _header = (ElfHeader *)addr;
    _sections = (const char *)addr + _header->e_shoff;
  }

  bool validHeader() {
    unsigned char *ident = _header->e_ident;
    return ident[0] == 0x7f && ident[1] == 'E' && ident[2] == 'L' &&
        ident[3] == 'F' && ident[4] == ELFCLASS_SUPPORTED &&
        ident[5] == ELFDATA2LSB && ident[6] == EV_CURRENT &&
        _header->e_shstrndx != SHN_UNDEF;
  }

  ElfSection *section(int index) {
    return (ElfSection *)(_sections + index * _header->e_shentsize);
  }

  const char *at(ElfSection *section) {
    return (const char *)_header + section->sh_offset;
  }

  const char *at(ElfProgramHeader *pheader) {
    return _header->e_type == ET_EXEC
        ? (const char *)pheader->p_vaddr
        : (const char *)_header + pheader->p_vaddr;
  }

  ElfSection *findSection(uint32_t type, const char *name);
  ElfProgramHeader *findProgramHeader(uint32_t type);

  void parseDynamicSection();
  void parseDwarfInfo();
  void parseDwarfInfoRemote(const char *eh_frame_data, const char *base_remote,
                            Offset_t adjust_eh_frame);
  void loadSymbols(bool use_debug);
  bool loadSymbolsUsingBuildId();
  bool loadSymbolsUsingDebugLink();
  void loadSymbolTable(ElfSection *symtab);
  void addRelocationSymbols(ElfSection *reltab, const char *plt);

public:
  static const char *get_self_vdso(void);
  static void parseProgramHeaders(CodeCache *cc, const char *base);
  static bool parseProgramHeadersRemote(Elf *elf, CodeCache *cc,
                                        const char *base,
                                        const char *mmap_addr);
  static bool parseFile(CodeCache *cc, const char *base, const char *file_name,
                        bool use_debug);
  static void parseMem(CodeCache *cc, const char *base);
  static void parseMemRemote(CodeCache *cc, const char *base, const char *addr);
};

ElfSection *ElfParser::findSection(uint32_t type, const char *name) {
  const char *strtab = at(section(_header->e_shstrndx));

  for (int i = 0; i < _header->e_shnum; i++) {
    ElfSection *section = this->section(i);
    if (section->sh_type == type && section->sh_name != 0) {
      if (strcmp(strtab + section->sh_name, name) == 0) {
        return section;
      }
    }
  }

  return NULL;
}

ElfProgramHeader *ElfParser::findProgramHeader(uint32_t type) {
  const char *pheaders = (const char *)_header + _header->e_phoff;

  for (int i = 0; i < _header->e_phnum; i++) {
    ElfProgramHeader *pheader =
        (ElfProgramHeader *)(pheaders + i * _header->e_phentsize);
    if (pheader->p_type == type) {
      return pheader;
    }
  }

  return NULL;
}

bool ElfParser::parseFile(CodeCache *cc, const char *base,
                          const char *file_name, bool use_debug) {
  int fd = open(file_name, O_RDONLY);
  if (fd == -1) {
    return false;
  }

  size_t length = (size_t)lseek64(fd, 0, SEEK_END);
  void *addr = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);

  if (addr == MAP_FAILED) {
    LG_WRN("Could not parse symbols from %s: %s", file_name, strerror(errno));
  } else {
    ElfParser elf(cc, base, addr, file_name);
    if (elf.validHeader()) {
      elf.loadSymbols(use_debug);
    }
    munmap(addr, length);
  }
  return true;
}

void ElfParser::parseMemRemote(CodeCache *cc, const char *base,
                               const char *addr) {
  ElfParser elf(cc, base, addr);
  if (elf.validHeader()) {
    elf.loadSymbols(false);
  }
}

void ElfParser::parseMem(CodeCache *cc, const char *base) {
  ElfParser elf(cc, base, base);
  if (elf.validHeader()) {
    elf.loadSymbols(false);
  }
}

// remote opens the elf file
bool ElfParser::parseProgramHeadersRemote(Elf *elf, CodeCache *cc,
                                          const char *base,
                                          const char *mmap_addr) {
  // todo check if I can use base
  ElfParser elf_remote(cc, base, mmap_addr);
  if (elf_remote.validHeader()) {
    cc->setTextBase(mmap_addr);
    elf_remote.parseDynamicSection();
    elf_remote.parseDwarfInfo();
    return true;
  } else {
    printf("invalid header \n");
  }
  return false;
}

void ElfParser::parseProgramHeaders(CodeCache *cc, const char *base) {
  ElfParser elf(cc, base, base);

  if (elf.validHeader()) {
    printf("Setting text base = %p \n", base);
    cc->setTextBase(base);
    elf.parseDynamicSection();
    elf.parseDwarfInfo();
  }
}

void ElfParser::parseDynamicSection() {
  ElfProgramHeader *dynamic = findProgramHeader(PT_DYNAMIC);
  if (dynamic != NULL) {
    void **got_start = NULL;
    size_t pltrelsz = 0;
    char *rel = NULL;
    size_t relsz = 0;
    size_t relent = 0;
    size_t relcount = 0;

    const char *dyn_start = at(dynamic);
    const char *dyn_end = dyn_start + dynamic->p_memsz;
    for (ElfDyn *dyn = (ElfDyn *)dyn_start; dyn < (ElfDyn *)dyn_end; dyn++) {
      switch (dyn->d_tag) {
      case DT_PLTGOT:
        got_start = (void **)DYN_PTR(dyn->d_un.d_ptr) + 3;
        break;
      case DT_PLTRELSZ:
        pltrelsz = dyn->d_un.d_val;
        break;
      case DT_RELA:
      case DT_REL:
        rel = (char *)DYN_PTR(dyn->d_un.d_ptr);
        break;
      case DT_RELASZ:
      case DT_RELSZ:
        relsz = dyn->d_un.d_val;
        break;
      case DT_RELAENT:
      case DT_RELENT:
        relent = dyn->d_un.d_val;
        break;
      case DT_RELACOUNT:
      case DT_RELCOUNT:
        relcount = dyn->d_un.d_val;
        break;
      }
    }
    printf("relent = %d \n", relent);
    if (relent != 0) {
      if (pltrelsz != 0 && got_start != NULL) {
        // The number of entries in .got.plt section matches the number of
        // entries in .rela.plt
        printf("GOT start == %p \n", got_start);
        _cc->setGlobalOffsetTable(got_start, got_start + pltrelsz / relent,
                                  false);
      } else if (rel != NULL && relsz != 0) {
        // RELRO technique: .got.plt has been merged into .got and made
        // read-only. Find .got end from the highest relocation address.
        void **min_addr = (void **)-1;
        void **max_addr = (void **)0;
        for (size_t offs = relcount * relent; offs < relsz; offs += relent) {
          ElfRelocation *r = (ElfRelocation *)(rel + offs);
          if (ELF_R_TYPE(r->r_info) == R_GLOB_DAT) {
            void **addr = (void **)(_base + r->r_offset);
            if (addr < min_addr)
              min_addr = addr;
            if (addr > max_addr)
              max_addr = addr;
          }
        }

        if (got_start == NULL) {
          got_start = (void **)min_addr;
        }

        if (max_addr >= got_start) {
          _cc->setGlobalOffsetTable(got_start, max_addr + 1, false);
        }
      }
    }
  } else {
    printf("No dynamic section \n");
  }
}

void ElfParser::parseDwarfInfoRemote(const char *eh_frame_data,
                                     const char *base_remote,
                                     Offset_t adjust_eh_frame) {
  printf("Create dwarf with base:%p - eh_frame_hdr:%p\n", _base, eh_frame_data);
  DwarfParser dwarf(_cc->name(), base_remote, eh_frame_data, adjust_eh_frame);
  _cc->setDwarfTable(dwarf.table(), dwarf.count());
  printf("Created a number of dwarf entries = %d \n", dwarf.count());
}

void ElfParser::parseDwarfInfo() {
  if (!DWARF_SUPPORTED)
    return;

  ElfProgramHeader *eh_frame_hdr = findProgramHeader(PT_GNU_EH_FRAME);

  if (eh_frame_hdr != NULL) {
    printf("Create dwarf with %lx - at:%lx \n", _base, at(eh_frame_hdr));
    DwarfParser dwarf(_cc->name(), _base, at(eh_frame_hdr));
    _cc->setDwarfTable(dwarf.table(), dwarf.count());
    printf("Created a number of dwarf entries = %d \n", dwarf.count());
  }
}

void ElfParser::loadSymbols(bool use_debug) {
  // Look for debug symbols in the original .so
  ElfSection *section = findSection(SHT_SYMTAB, ".symtab");
  if (section != NULL) {
    loadSymbolTable(section);
    goto loaded;
  }

  // Try to load symbols from an external debuginfo library
  if (use_debug) {
    if (loadSymbolsUsingBuildId() || loadSymbolsUsingDebugLink()) {
      goto loaded;
    }
  }

  // If everything else fails, load only exported symbols
  section = findSection(SHT_DYNSYM, ".dynsym");
  if (section != NULL) {
    loadSymbolTable(section);
  }

loaded:
  if (use_debug) {
    // Synthesize names for PLT stubs
    ElfSection *plt = findSection(SHT_PROGBITS, ".plt");
    ElfSection *reltab = findSection(SHT_RELA, ".rela.plt");
    if (reltab == NULL) {
      reltab = findSection(SHT_REL, ".rel.plt");
    }
    if (plt != NULL && reltab != NULL) {
      addRelocationSymbols(reltab, _base + plt->sh_offset + PLT_HEADER_SIZE);
    }
  }
}

// Load symbols from /usr/lib/debug/.build-id/ab/cdef1234.debug, where
// abcdef1234 is Build ID
bool ElfParser::loadSymbolsUsingBuildId() {
  ElfSection *section = findSection(SHT_NOTE, ".note.gnu.build-id");
  if (section == NULL || section->sh_size <= 16) {
    return false;
  }

  ElfNote *note = (ElfNote *)at(section);
  if (note->n_namesz != 4 || note->n_descsz < 2 || note->n_descsz > 64) {
    return false;
  }

  const char *build_id = (const char *)note + sizeof(*note) + 4;
  int build_id_len = note->n_descsz;

  char path[PATH_MAX];
  char *p =
      path + sprintf(path, "/usr/lib/debug/.build-id/%02hhx/", build_id[0]);
  for (int i = 1; i < build_id_len; i++) {
    p += sprintf(p, "%02hhx", build_id[i]);
  }
  strcpy(p, ".debug");

  return parseFile(_cc, _base, path, false);
}

// Look for debuginfo file specified in .gnu_debuglink section
bool ElfParser::loadSymbolsUsingDebugLink() {
  ElfSection *section = findSection(SHT_PROGBITS, ".gnu_debuglink");
  if (section == NULL || section->sh_size <= 4) {
    return false;
  }

  const char *basename = strrchr(_file_name, '/');
  if (basename == NULL) {
    return false;
  }

  char *dirname = strndup(_file_name, basename - _file_name);
  if (dirname == NULL) {
    return false;
  }

  const char *debuglink = at(section);
  char path[PATH_MAX];
  bool result = false;

  // 1. /path/to/libjvm.so.debug
  if (strcmp(debuglink, basename + 1) != 0 &&
      snprintf(path, PATH_MAX, "%s/%s", dirname, debuglink) < PATH_MAX) {
    result = parseFile(_cc, _base, path, false);
  }

  // 2. /path/to/.debug/libjvm.so.debug
  if (!result &&
      snprintf(path, PATH_MAX, "%s/.debug/%s", dirname, debuglink) < PATH_MAX) {
    result = parseFile(_cc, _base, path, false);
  }

  // 3. /usr/lib/debug/path/to/libjvm.so.debug
  if (!result &&
      snprintf(path, PATH_MAX, "/usr/lib/debug%s/%s", dirname, debuglink) <
          PATH_MAX) {
    result = parseFile(_cc, _base, path, false);
  }

  free(dirname);
  return result;
}

void ElfParser::loadSymbolTable(ElfSection *symtab) {
  ElfSection *strtab = section(symtab->sh_link);
  const char *strings = at(strtab);
  int cpt = 0;
  const char *symbols = at(symtab);
  const char *symbols_end = symbols + symtab->sh_size;
  for (; symbols < symbols_end; symbols += symtab->sh_entsize) {
    ElfSymbol *sym = (ElfSymbol *)symbols;
    if (sym->st_name != 0 && sym->st_value != 0) {
      // Skip special AArch64 mapping symbols: $x and $d
      if (sym->st_size != 0 || sym->st_info != 0 ||
          strings[sym->st_name] != '$') {
        //        printf("Loading sym %s at 0x%lx (base=0x%lx)\n", strings +
        //        sym->st_name,
        //               _base + sym->st_value, _base);
        _cc->add(_base + sym->st_value, (int)sym->st_size,
                 strings + sym->st_name);
        ++cpt;
      }
    }
  }
  printf("Considered %d symbols \n", cpt);
}

void ElfParser::addRelocationSymbols(ElfSection *reltab, const char *plt) {
  ElfSection *symtab = section(reltab->sh_link);
  const char *symbols = at(symtab);

  ElfSection *strtab = section(symtab->sh_link);
  const char *strings = at(strtab);

  const char *relocations = at(reltab);
  const char *relocations_end = relocations + reltab->sh_size;
  for (; relocations < relocations_end; relocations += reltab->sh_entsize) {
    ElfRelocation *r = (ElfRelocation *)relocations;
    ElfSymbol *sym =
        (ElfSymbol *)(symbols + ELF_R_SYM(r->r_info) * symtab->sh_entsize);

    char name[256];
    if (sym->st_name == 0) {
      strcpy(name, "@plt");
    } else {
      const char *sym_name = strings + sym->st_name;
      snprintf(name, sizeof(name), "%s%cplt", sym_name,
               sym_name[0] == '_' && sym_name[1] == 'Z' ? '.' : '@');
      name[sizeof(name) - 1] = 0;
    }

    _cc->add(plt, PLT_ENTRY_SIZE, name);
    plt += PLT_ENTRY_SIZE;
  }
}

Mutex Symbols::_parse_lock;
bool Symbols::_have_kernel_symbols = false;

void Symbols::parseKernelSymbols(CodeCache *cc) {
  // XXX(nick): omitted
}

const char *ElfParser::get_self_vdso(void) {
  FILE *f = fopen("/proc/self/maps", "r");
  const char *addr_vdso = nullptr;

  if (f == NULL) {
    return nullptr;
  }
  char *str = NULL;
  size_t str_size = 0;
  ssize_t len;

  while ((len = getline(&str, &str_size, f)) > 0) {
    str[len - 1] = 0;

    MemoryMapDesc map(str);
    if (!map.isReadable() || map.file() == NULL || map.file()[0] == 0) {
      continue;
    }
    const char *image_base = map.addr();
    if (map.isExecutable()) {
      if (strcmp(map.file(), "[vdso]") == 0) {
        addr_vdso = image_base; // found it
        break;
      }
    }
  }
  return addr_vdso;
}

void Symbols::parsePidLibraries(pid_t pid, CodeCacheArray *array,
                                bool kernel_symbols) {
  std::set<const void *> parsed_libraries;
  std::set<unsigned long> parsed_inodes;
  MutexLocker ml(_parse_lock);
  char proc_map_filename[1024] = {};
  snprintf(proc_map_filename, std::size(proc_map_filename), "%s/proc/%d/maps",
           "", pid);
  // todo plug the proc_map open functions (handles user switches)
  FILE *f = fopen(proc_map_filename, "r");
  if (f == NULL) {
    return;
  }

  // last readable is previous mmap
  const char *last_readable_base = NULL;
  const char *image_end = NULL;
  char *str = NULL;
  size_t str_size = 0;
  ssize_t len;
  // tell elf what version we are using
  elf_version(EV_CURRENT);

  while ((len = getline(&str, &str_size, f)) > 0) {
    str[len - 1] = 0;

    MemoryMapDesc map(str);
    if (!map.isReadable() || map.file() == NULL || map.file()[0] == 0) {
      continue;
    }

    const char *image_base = map.addr();
    if (image_base != image_end)
      last_readable_base = image_base;
    image_end = map.end();

    if (map.isExecutable()) {
      if (!parsed_libraries.insert(image_base).second) {
        continue; // the library was already parsed
      }

      int count = array->count();
      if (count >= MAX_NATIVE_LIBS) {
        break;
      }

      CodeCache *cc = new CodeCache(map.file(), count, image_base, image_end);
      unsigned long inode = map.inode();
      printf("+++++ Considering %s ++++ \n", map.file());
      if (inode != 0) {
        char proc_root_filename[1024] = {};
        // use /proc/<pid>/root to access the file (whole host)
        int n = snprintf(proc_root_filename, 1024, "%s/proc/%d/root%s", "", pid, map.file());
        if (n < 0) {
          printf("error encoding file %s \n", map.file());
          continue;
        }
        int fd = open(proc_root_filename, O_RDONLY);
        // remote unwinding
        if (-1 == fd) {
          printf("error opening file %s \n", map.file());
          continue;
        }
        size_t length = (size_t)lseek64(fd, 0, SEEK_END);
        // todo : remove the mmap
        Elf *elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
        if (elf == NULL) {
          LG_WRN("Invalid elf %s (efl:%p, addr_mmap:%p)\n", map.file(), elf);
          goto continue_loop;
        }
        Offset_t biais_offset;
        ElfAddress_t vaddr;
        ElfAddress_t text_base; // not used
        Offset_t elf_offset;
        // Compute how to convert a process address
        if (get_elf_offsets(elf, map.file(), vaddr, elf_offset, biais_offset,
                            text_base)) {
          printf("vaddr from get_elf_offset: %lx \n", vaddr);
          printf("biais offset get_elf_offset: %lx \n", biais_offset);
          printf("text base from get_elf_offset: %lx \n", text_base);
          printf("offset from get_elf_offset: %lx \n", elf_offset);
          printf("last readable: %lx \n", last_readable_base);
        }
        else {
          printf("Failed to read elf offsets \n");
        }

        // Do not parse the same executable twice, e.g. on Alpine Linux
        if (parsed_inodes.insert(map.dev() | inode << 16).second) {
          // Be careful: executable file is not always ELF, e.g. classes.jsa
          // todo: This should be something with the biais instead
          if ((image_base -= map.offs()) >= last_readable_base) {
            // process elf info
            EhFrameInfo eh_frame_info = {};
            if (!get_eh_frame_info(elf, eh_frame_info)) {
              printf("Failed to retrieve eh frame info\n");
            }
            const char *elf_base = eh_frame_info._eh_frame_hdr._data -
                eh_frame_info._eh_frame_hdr._offset;
            // this is used during unwinding to offset PC to dwarf instructions
            cc->setTextBase(image_base);
            printf("image base = %lx \n", image_base);

            if (eh_frame_info._eh_frame_hdr._data) {
              // todo: is this always valid ?
              ElfParser elf_remote(cc, image_base, elf_base);

              // (vaddr_eh_frame - vaddr_eh_frame_hdr) - (offset_sec_1 -
              // offset_sec_2)
              // If eh frame is not in the same segment
              Offset_t adjust_eh_frame =
                  (eh_frame_info._eh_frame._vaddr_sec -
                   eh_frame_info._eh_frame_hdr._vaddr_sec) -
                  (eh_frame_info._eh_frame._offset -
                   eh_frame_info._eh_frame_hdr._offset);
              printf("adjust eh_frame %lx \n", adjust_eh_frame);
              elf_remote.parseDwarfInfoRemote(
                  eh_frame_info._eh_frame_hdr._data,
                  eh_frame_info._eh_frame_hdr._data -
                      eh_frame_info._eh_frame_hdr._offset,
                  adjust_eh_frame);
            } else {
              printf("No EH Frame data - %s\n", map.file());
            }
          }
          ElfParser::parseFile(cc, image_base, map.file(), true);
        }

      continue_loop:
        close(fd);
        elf_end(elf); // no-op if null
      } else if (strcmp(map.file(), "[vdso]") == 0) {
        // find our self address for vdso
        const char *addr_vdso = ElfParser::get_self_vdso();
        ElfParser::parseMemRemote(cc, image_base, addr_vdso);
      }
      cc->sort();
      array->add(cc);
    }
  }

  free(str);
  fclose(f);
}

void Symbols::parseLibraries(CodeCacheArray *array, bool kernel_symbols) {
  // we can't use static global sets due to undefined initialization order stuff
  // (see
  // https://stackoverflow.com/questions/27145617/segfault-when-adding-an-element-to-a-stdmap)
  // I'm not sure why this original code even worked?
  std::set<const void *> parsed_libraries;
  std::set<unsigned long> parsed_inodes;
  MutexLocker ml(_parse_lock);

  FILE *f = fopen("/proc/self/maps", "r");
  if (f == NULL) {
    return;
  }

  const char *last_readable_base = NULL;
  const char *image_end = NULL;
  char *str = NULL;
  size_t str_size = 0;
  ssize_t len;

  while ((len = getline(&str, &str_size, f)) > 0) {
    str[len - 1] = 0;

    MemoryMapDesc map(str);
    if (!map.isReadable() || map.file() == NULL || map.file()[0] == 0) {
      continue;
    }

    const char *image_base = map.addr();
    if (image_base != image_end)
      last_readable_base = image_base;
    image_end = map.end();

    if (map.isExecutable()) {
      if (!parsed_libraries.insert(image_base).second) {
        continue; // the library was already parsed
      }
      printf("Considering %s \n", map.file());

      int count = array->count();
      if (count >= MAX_NATIVE_LIBS) {
        break;
      }

      CodeCache *cc = new CodeCache(map.file(), count, image_base, image_end);

      unsigned long inode = map.inode();
      if (inode != 0) {
        // Do not parse the same executable twice, e.g. on Alpine Linux
        if (parsed_inodes.insert(map.dev() | inode << 16).second) {
          // Be careful: executable file is not always ELF, e.g. classes.jsa
          printf("image_base = %p, map.offs() = %p, last_readable_base = %p \n",
                 image_base, map.offs(), last_readable_base);
          // todo - read the biais from the vaddr field (open file?)

          if ((image_base -= map.offs()) >= last_readable_base) {
            ElfParser::parseProgramHeaders(cc, image_base);
          }
          ElfParser::parseFile(cc, image_base, map.file(), true);
        }
      } else if (strcmp(map.file(), "[vdso]") == 0) {
        ElfParser::parseMem(cc, image_base);
      }
      cc->sort();
      array->add(cc);
    }
  }
  free(str);
  fclose(f);
}

#endif // __linux__
