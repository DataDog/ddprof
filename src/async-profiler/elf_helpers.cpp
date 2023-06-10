#include "elf_helpers.h"

#include "build_id.hpp"
#include "logger.hpp"

#include <gelf.h>
#include <libelf.h>

#include <dwarf.h>
#include <elfutils/libdw.h>

#define LG_WRN(args...) printf(args)

const char *get_section_data(Elf *elf, const char *section_name,
                             Offset_t &elf_offset) {
  // Get the string table index for the section header strings
  size_t shstrndx;
  if (elf_getshdrstrndx(elf, &shstrndx) != 0) {
    fprintf(stderr,
            "Failed to get string table index for section header strings: %s\n",
            elf_errmsg(-1));
    return nullptr;
  }

  // Iterate over the sections and find the .eh_frame section
  Elf_Scn *scn = NULL;
  while ((scn = elf_nextscn(elf, scn)) != NULL) {
    // Get the section header for the current section
    GElf_Shdr shdr;
    if (gelf_getshdr(scn, &shdr) != &shdr) {
      fprintf(stderr, "Failed to get section header: %s\n", elf_errmsg(-1));
      return nullptr;
    }

    // Get the name of the current section
    char *name = elf_strptr(elf, shstrndx, shdr.sh_name);
    if (name == NULL) {
      fprintf(stderr, "Failed to get section name: %s\n", elf_errmsg(-1));
      return nullptr;
    }

    // Check if the section is the .eh_frame section
    if (strcmp(name, section_name) == 0) {
      printf("%s section found at offset 0x%lx, size %ld\n", section_name,
             shdr.sh_offset, shdr.sh_size);
      // Get the data for the .eh_frame section
      elf_offset = shdr.sh_offset;
      Elf_Data *data = elf_getdata(scn, NULL);
      if (data == NULL) {
        fprintf(stderr, "Unable to find section data: %s\n", section_name);
        return nullptr;
      } else {
        return reinterpret_cast<const char *>(data->d_buf);
      }
    }
  }

  fprintf(stderr, "Failed to find section: %s\n", section_name);
  return nullptr;
}

bool get_section_info(Elf *elf, const char *section_name,
                      SectionInfo &section_info) {
  // Get the string table index for the section header strings
  size_t shstrndx;
  if (elf_getshdrstrndx(elf, &shstrndx) != 0) {
    fprintf(stderr,
            "Failed to get string table index for section header strings: %s\n",
            elf_errmsg(-1));
    return false;
  }

  // Iterate over the sections and find the .eh_frame section
  Elf_Scn *scn = NULL;
  bool found = false;
  GElf_Shdr shdr;

  while ((scn = elf_nextscn(elf, scn)) != NULL) {
    // Get the section header for the current section
    if (gelf_getshdr(scn, &shdr) != &shdr) {
      fprintf(stderr, "Failed to get section header: %s\n", elf_errmsg(-1));
      return false;
    }

    // Get the name of the current section
    char *name = elf_strptr(elf, shstrndx, shdr.sh_name);
    if (name == NULL) {
      fprintf(stderr, "Failed to get section name: %s\n", elf_errmsg(-1));
      return false;
    }

    // Check if the section is the .eh_frame section
    if (strcmp(name, section_name) == 0) {
      printf("%s section found at offset 0x%lx, size %ld, vaddr %lx\n",
             section_name, shdr.sh_offset, shdr.sh_size, shdr.sh_addr);
      // Get the data for the .eh_frame section
      Elf_Data *data = elf_getdata(scn, NULL);
      if (data == NULL) {
        fprintf(stderr, "Unable to find section data: %s\n", section_name);
        return false;
      } else {
        section_info._data = reinterpret_cast<const char *>(data->d_buf);
        section_info._offset = shdr.sh_offset;
        section_info._vaddr_sec = shdr.sh_addr;
        found = true;
      }
    }
  }
  if (!found) {
    fprintf(stderr, "Failed to find section: %s\n", section_name);
    return false;
  }

  return true;
}

bool get_elf_offsets(Elf *elf, const char *filepath, ElfAddress_t &vaddr,
                     Offset_t &elf_offset, Offset_t &bias_offset,
                     Offset_t &text_base) {
  vaddr = 0;
  bias_offset = 0;
  GElf_Ehdr ehdr_mem;
  GElf_Ehdr *ehdr = gelf_getehdr(elf, &ehdr_mem);
  if (ehdr == nullptr) {
    LG_WRN("Invalid elf %s", filepath);
    return false;
  }
  text_base = ehdr->e_entry;

  bool found_exec = false;
  switch (ehdr->e_type) {
  case ET_EXEC:
  case ET_CORE:
  case ET_DYN: {
    size_t phnum;
    if (unlikely(elf_getphdrnum(elf, &phnum) != 0)) {
      LG_WRN("Invalid elf %s", filepath);
      return false;
    }
    for (size_t i = 0; i < phnum; ++i) {
      GElf_Phdr phdr_mem;
      // Retrieve the program header
      GElf_Phdr *ph = gelf_getphdr(elf, i, &phdr_mem);
      if (unlikely(ph == NULL)) {
        LG_WRN("Invalid elf %s", filepath);
        return false;
      }
      constexpr int rx = PF_X | PF_R;
      if (ph->p_type == PT_LOAD) {
        if ((ph->p_flags & rx) == rx) {
          if (!found_exec) {
            vaddr = ph->p_vaddr;
            bias_offset = ph->p_vaddr - ph->p_offset;
            printf("%lx - %lx (vaddr - p_offset) \n", ph->p_vaddr,
                   ph->p_offset);
            elf_offset = ph->p_offset;
            found_exec = true;
          } else {
            // There can be multiple executable load segments.
            // The first one should be considered (this is valid)
            // Leaving the failure for now as it allows me to find test cases
            printf("Multiple exec LOAD segments: %s", filepath);
          }
        }
      }
    }
    break;
  }
  default:
    LG_WRN("Unsupported elf type (%d) %s", ehdr->e_type, filepath);
    return false;
  }

  if (!found_exec) {
    LG_WRN("Not executable LOAD segment found in %s", filepath);
  }
  return found_exec;
}

bool get_eh_frame_info(Elf *elf, EhFrameInfo &eh_frame_info) {
  if (!get_section_info(elf, ".eh_frame_hdr", eh_frame_info._eh_frame_hdr)) {
    return false;
  }
  if (!get_section_info(elf, ".eh_frame", eh_frame_info._eh_frame)) {
    return false;
  }
  return true;
}

// correct way of parsing the FDEs
bool process_fdes(Elf *elf) {
  Elf_Scn *scn = NULL;
  Elf_Data *data = NULL;
  GElf_Shdr shdr;

  // Get the string table index for the section header strings
  size_t shstrndx;
  if (elf_getshdrstrndx(elf, &shstrndx) != 0) {
    fprintf(stderr,
            "Failed to get string table index for section header strings: %s\n",
            elf_errmsg(-1));
    return false;
  }

  while ((scn = elf_nextscn(elf, scn)) != NULL) {
    gelf_getshdr(scn, &shdr);
    if (shdr.sh_type == SHT_PROGBITS &&
        (strcmp(".debug_frame", elf_strptr(elf, shstrndx, shdr.sh_name)) == 0 ||
         strcmp(".eh_frame", elf_strptr(elf, shstrndx, shdr.sh_name)) == 0)) {
      // This is the .debug_frame or .eh_frame section
      data = elf_getdata(scn, NULL);
      break;
    }
  }
  if (!data) {
    fprintf(stderr, "Unable to find dwarf information\n");
    return false;
  }

  // Iterate through the CFI records in the .debug_frame or .eh_frame section
  Dwarf_Off offset = 0;
  while (true) {
    // Get the next CFI record
    Dwarf_Off next_offset;
    Dwarf_CFI_Entry entry;

    int result = dwarf_next_cfi(
        reinterpret_cast<const unsigned char *>(elf_getident(elf, NULL)), data,
        strcmp(".eh_frame", elf_strptr(elf, shstrndx, shdr.sh_name)) == 0,
        offset, &next_offset, &entry);
    if (result != 0) {
      // End of CFI records
      break;
    }

    //    printf("cfi id = %lx\n", entry);
    // Process the CFI record
    // ...

    // Move to the next CFI record
    offset = next_offset;
  }
  return true;
}
