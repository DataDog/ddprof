// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dwarf_helpers.hpp"

#include "ddres.hpp"
#include "logger.hpp"

#include <cassert>
#include <map>
#include <string>

namespace ddprof {

struct DieSearchParam {
  Dwarf_Addr addr;
  Dwarf_Die *die_mem;
};

/* die_find callback for non-inlined function search */
static int die_search_func_cb(Dwarf_Die *fn_die, void *data) {
  DieSearchParam *ad = reinterpret_cast<DieSearchParam *>(data);
  if (dwarf_tag(fn_die) == DW_TAG_subprogram && dwarf_haspc(fn_die, ad->addr)) {
    memcpy(ad->die_mem, fn_die, sizeof(Dwarf_Die));
    return DWARF_CB_ABORT;
  }
  return DWARF_CB_OK;
}

Dwarf_Die *die_find_realfunc(Dwarf_Die *cu_die, Dwarf_Addr addr,
                             Dwarf_Die *die_mem) {
  DieSearchParam ad;
  ad.addr = addr;
  ad.die_mem = die_mem;
  /* dwarf_getscopes can't find subprogram. */
  if (!dwarf_getfuncs(cu_die, die_search_func_cb, &ad, 0))
    return NULL;
  else
    return die_mem;
}

// return index to added element, else returns -1
static int store_die_information(Dwarf_Die *sc_die, int parent_index,
                                 DieInformation &data,
                                 Dwarf_Files *dwarf_files) {
#ifdef DEEP_DEBUG
  // dwarf_dieoffset is good to figure out what element we are working on
  dwarf_getattrs(sc_die, print_attribute, nullptr, 0);
#endif

  // function or inlined function
  DieInformation::Function function{};
  // die_name is usually the raw function name (no mangling info)
  // link name can have mangling info
  function.func_name = dwarf_diename(sc_die);
  Dwarf_Attribute attr_mem;
  Dwarf_Attribute *attr;
  if ((attr = dwarf_attr(sc_die, DW_AT_low_pc, &attr_mem))) {
    if (attr) {
      Dwarf_Addr ret_value;
      if (dwarf_formaddr(attr, &ret_value) == 0) {
        function.start_addr = ret_value;
      }
    }
  }
  // end is stored as a unsigned (not as a pointer)
  if ((attr = dwarf_attr(sc_die, DW_AT_high_pc, &attr_mem))) {
    if (attr) {
      Dwarf_Word return_uval;
      if (dwarf_formudata(attr, &return_uval) == 0) {
        function.end_addr = function.start_addr + return_uval;
      }
    }
  }
  // some of the functions don't have the start and end info
  if (!function.start_addr || !function.end_addr) {
    return -1;
  }

  // declaration files come with an indirection
  // dwarf_attr_integrate follows the indirections
  // for inlined functions, we could cache this access (as we are making several
  // of them)
  if (dwarf_files &&
      ((attr = dwarf_attr_integrate(sc_die, DW_AT_decl_file, &attr_mem)))) {
    Dwarf_Word fileIdx = 0;
    if (dwarf_formudata(attr, &fileIdx) == 0) {
      const char *file = dwarf_filesrc(dwarf_files, fileIdx, NULL, NULL);
      // Store or process the file name
      function.file_name = file;
    }
  }

  if ((attr = dwarf_attr_integrate(sc_die, DW_AT_decl_line, &attr_mem))) {
    Dwarf_Word return_uval;
    if (dwarf_formudata(attr, &return_uval) == 0) {
      function.decl_line_number = return_uval;
    }
  }

  if ((attr = dwarf_attr(sc_die, DW_AT_call_line, &attr_mem))) {
    Dwarf_Word return_uval;
    if (dwarf_formudata(attr, &return_uval) == 0) {
      function.call_line_number = return_uval;
    }
  }
  // other fields of interest
  // - DW_AT_call_file
  // - DW_AT_call_line to define parent line

  // we often can find duplicates within the dwarf information
  function.parent_pos = parent_index;
  data.die_mem_vec.push_back(std::move(function));
  return (data.die_mem_vec.size() - 1);
}

static Dwarf_Die *find_functions_in_child_die(Dwarf_Die *current_die,
                                              int parent_index,
                                              DieInformation &die_info,
                                              Dwarf_Die *die_mem,
                                              Dwarf_Files *dwarf_files) {
  Dwarf_Die child_die;
  int ret;
  ret = dwarf_child(current_die, die_mem);
  if (ret != 0)
    return nullptr;
  do {
    int tag_val = dwarf_tag(die_mem);
    int next_parent_idx = parent_index;
    if (tag_val == DW_TAG_subprogram || tag_val == DW_TAG_inlined_subroutine) {
      int current_idx =
          store_die_information(die_mem, parent_index, die_info, dwarf_files);
      next_parent_idx = (current_idx != -1 ? current_idx : next_parent_idx);
    }
    //
    // todo: optimize the exploration to avoid going through soo many elements
    // Child dies can have functions, even without being a child of another func
    find_functions_in_child_die(die_mem, next_parent_idx, die_info, &child_die,
                                dwarf_files);
  } while (dwarf_siblingof(die_mem, die_mem) == 0);
  return nullptr;
}

DDRes parse_die_information(Dwarf_Die *cudie, ElfAddress_t elf_addr,
                            DieInformation &die_information) {
  Dwarf_Files *files = nullptr;
  size_t nfiles = 0;
  assert(cudie);
  // cached within the CU
  if (dwarf_getsrcfiles(cudie, &files, &nfiles) != 0) {
    files = nullptr;
  }
  Dwarf_Die die_mem;
  Dwarf_Die *sc_die = die_find_realfunc(cudie, elf_addr, &die_mem);
  if (sc_die == nullptr) {
    LG_DBG("Unable to retrieve sc_die at %lx", elf_addr);
    return ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  }
  // store parent function at index 0
  if (store_die_information(sc_die, -1, die_information, files) == -1) {
    LG_DBG("Incomplete die information for parent function");
    // On some functions we are unable to find start / end info
    return ddres_warn(DD_WHAT_DWFL_LIB_ERROR);
  }
  find_functions_in_child_die(sc_die, 0, die_information, &die_mem, files);

  for (auto &el : die_information.die_mem_vec) {
    LG_DBG("Inlined func start=%lx / end=%lx / Sym = %s / file=%s",
           el.start_addr, el.end_addr, el.func_name, el.file_name);
  }
  return {};
}

const char *get_attribute_name(int attrCode) {
  // Should not get init unless cablled
  // Something like following awk can help generate this map:
  // cat file_with_dwarf_attributes.txt |
  //      awk '!/\/\*/ { print  "{ "$1", \""$1"\"},"}'
  static const std::map<int, std::string> attributeNameMap = {
      {DW_AT_sibling, "DW_AT_sibling"},
      {DW_AT_location, "DW_AT_location"},
      {DW_AT_name, "DW_AT_name"},
      {DW_AT_ordering, "DW_AT_ordering"},
      {DW_AT_byte_size, "DW_AT_byte_size"},
      {DW_AT_bit_size, "DW_AT_bit_size"},
      {DW_AT_stmt_list, "DW_AT_stmt_list"},
      {DW_AT_low_pc, "DW_AT_low_pc"},
      {DW_AT_high_pc, "DW_AT_high_pc"},
      {DW_AT_language, "DW_AT_language"},
      {DW_AT_discr, "DW_AT_discr"},
      {DW_AT_discr_value, "DW_AT_discr_value"},
      {DW_AT_visibility, "DW_AT_visibility"},
      {DW_AT_import, "DW_AT_import"},
      {DW_AT_string_length, "DW_AT_string_length"},
      {DW_AT_common_reference, "DW_AT_common_reference"},
      {DW_AT_comp_dir, "DW_AT_comp_dir"},
      {DW_AT_const_value, "DW_AT_const_value"},
      {DW_AT_containing_type, "DW_AT_containing_type"},
      {DW_AT_default_value, "DW_AT_default_value"},
      {DW_AT_inline, "DW_AT_inline"},
      {DW_AT_is_optional, "DW_AT_is_optional"},
      {DW_AT_lower_bound, "DW_AT_lower_bound"},
      {DW_AT_producer, "DW_AT_producer"},
      {DW_AT_prototyped, "DW_AT_prototyped"},
      {DW_AT_return_addr, "DW_AT_return_addr"},
      {DW_AT_start_scope, "DW_AT_start_scope"},
      {DW_AT_bit_stride, "DW_AT_bit_stride"},
      {DW_AT_upper_bound, "DW_AT_upper_bound"},
      {DW_AT_abstract_origin, "DW_AT_abstract_origin"},
      {DW_AT_accessibility, "DW_AT_accessibility"},
      {DW_AT_address_class, "DW_AT_address_class"},
      {DW_AT_artificial, "DW_AT_artificial"},
      {DW_AT_base_types, "DW_AT_base_types"},
      {DW_AT_calling_convention, "DW_AT_calling_convention"},
      {DW_AT_count, "DW_AT_count"},
      {DW_AT_data_member_location, "DW_AT_data_member_location"},
      {DW_AT_decl_column, "DW_AT_decl_column"},
      {DW_AT_decl_file, "DW_AT_decl_file"},
      {DW_AT_decl_line, "DW_AT_decl_line"},
      {DW_AT_declaration, "DW_AT_declaration"},
      {DW_AT_discr_list, "DW_AT_discr_list"},
      {DW_AT_encoding, "DW_AT_encoding"},
      {DW_AT_external, "DW_AT_external"},
      {DW_AT_frame_base, "DW_AT_frame_base"},
      {DW_AT_friend, "DW_AT_friend"},
      {DW_AT_identifier_case, "DW_AT_identifier_case"},
      {DW_AT_namelist_item, "DW_AT_namelist_item"},
      {DW_AT_priority, "DW_AT_priority"},
      {DW_AT_segment, "DW_AT_segment"},
      {DW_AT_specification, "DW_AT_specification"},
      {DW_AT_static_link, "DW_AT_static_link"},
      {DW_AT_type, "DW_AT_type"},
      {DW_AT_use_location, "DW_AT_use_location"},
      {DW_AT_variable_parameter, "DW_AT_variable_parameter"},
      {DW_AT_virtuality, "DW_AT_virtuality"},
      {DW_AT_vtable_elem_location, "DW_AT_vtable_elem_location"},
      {DW_AT_allocated, "DW_AT_allocated"},
      {DW_AT_associated, "DW_AT_associated"},
      {DW_AT_data_location, "DW_AT_data_location"},
      {DW_AT_byte_stride, "DW_AT_byte_stride"},
      {DW_AT_entry_pc, "DW_AT_entry_pc"},
      {DW_AT_use_UTF8, "DW_AT_use_UTF8"},
      {DW_AT_extension, "DW_AT_extension"},
      {DW_AT_ranges, "DW_AT_ranges"},
      {DW_AT_trampoline, "DW_AT_trampoline"},
      {DW_AT_call_column, "DW_AT_call_column"},
      {DW_AT_call_file, "DW_AT_call_file"},
      {DW_AT_call_line, "DW_AT_call_line"},
      {DW_AT_description, "DW_AT_description"},
      {DW_AT_binary_scale, "DW_AT_binary_scale"},
      {DW_AT_decimal_scale, "DW_AT_decimal_scale"},
      {DW_AT_small, "DW_AT_small"},
      {DW_AT_decimal_sign, "DW_AT_decimal_sign"},
      {DW_AT_digit_count, "DW_AT_digit_count"},
      {DW_AT_picture_string, "DW_AT_picture_string"},
      {DW_AT_mutable, "DW_AT_mutable"},
      {DW_AT_threads_scaled, "DW_AT_threads_scaled"},
      {DW_AT_explicit, "DW_AT_explicit"},
      {DW_AT_object_pointer, "DW_AT_object_pointer"},
      {DW_AT_endianity, "DW_AT_endianity"},
      {DW_AT_elemental, "DW_AT_elemental"},
      {DW_AT_pure, "DW_AT_pure"},
      {DW_AT_recursive, "DW_AT_recursive"},
      {DW_AT_signature, "DW_AT_signature"},
      {DW_AT_main_subprogram, "DW_AT_main_subprogram"},
      {DW_AT_data_bit_offset, "DW_AT_data_bit_offset"},
      {DW_AT_const_expr, "DW_AT_const_expr"},
      {DW_AT_enum_class, "DW_AT_enum_class"},
      {DW_AT_linkage_name, "DW_AT_linkage_name"},
      {DW_AT_string_length_bit_size, "DW_AT_string_length_bit_size"},
      {DW_AT_string_length_byte_size, "DW_AT_string_length_byte_size"},
      {DW_AT_rank, "DW_AT_rank"},
      {DW_AT_str_offsets_base, "DW_AT_str_offsets_base"},
      {DW_AT_addr_base, "DW_AT_addr_base"},
      {DW_AT_rnglists_base, "DW_AT_rnglists_base"},
      {DW_AT_dwo_name, "DW_AT_dwo_name"},
      {DW_AT_reference, "DW_AT_reference"},
      {DW_AT_rvalue_reference, "DW_AT_rvalue_reference"},
      {DW_AT_macros, "DW_AT_macros"},
      {DW_AT_call_all_calls, "DW_AT_call_all_calls"},
      {DW_AT_call_all_source_calls, "DW_AT_call_all_source_calls"},
      {DW_AT_call_all_tail_calls, "DW_AT_call_all_tail_calls"},
      {DW_AT_call_return_pc, "DW_AT_call_return_pc"},
      {DW_AT_call_value, "DW_AT_call_value"},
      {DW_AT_call_origin, "DW_AT_call_origin"},
      {DW_AT_call_parameter, "DW_AT_call_parameter"},
      {DW_AT_call_pc, "DW_AT_call_pc"},
      {DW_AT_call_tail_call, "DW_AT_call_tail_call"},
      {DW_AT_call_target, "DW_AT_call_target"},
      {DW_AT_call_target_clobbered, "DW_AT_call_target_clobbered"},
      {DW_AT_call_data_location, "DW_AT_call_data_location"},
      {DW_AT_call_data_value, "DW_AT_call_data_value"},
      {DW_AT_noreturn, "DW_AT_noreturn"},
      {DW_AT_alignment, "DW_AT_alignment"},
      {DW_AT_export_symbols, "DW_AT_export_symbols"},
      {DW_AT_deleted, "DW_AT_deleted"},
      {DW_AT_defaulted, "DW_AT_defaulted"},
      {DW_AT_loclists_base, "DW_AT_loclists_base"},
      {DW_AT_lo_user, "DW_AT_lo_user"},
      {DW_AT_MIPS_fde, "DW_AT_MIPS_fde"},
      {DW_AT_MIPS_loop_begin, "DW_AT_MIPS_loop_begin"},
      {DW_AT_MIPS_tail_loop_begin, "DW_AT_MIPS_tail_loop_begin"},
      {DW_AT_MIPS_epilog_begin, "DW_AT_MIPS_epilog_begin"},
      {DW_AT_MIPS_loop_unroll_factor, "DW_AT_MIPS_loop_unroll_factor"},
      {DW_AT_MIPS_software_pipeline_depth,
       "DW_AT_MIPS_software_pipeline_depth"},
      {DW_AT_MIPS_linkage_name, "DW_AT_MIPS_linkage_name"},
      {DW_AT_MIPS_stride, "DW_AT_MIPS_stride"},
      {DW_AT_MIPS_abstract_name, "DW_AT_MIPS_abstract_name"},
      {DW_AT_MIPS_clone_origin, "DW_AT_MIPS_clone_origin"},
      {DW_AT_MIPS_has_inlines, "DW_AT_MIPS_has_inlines"},
      {DW_AT_MIPS_stride_byte, "DW_AT_MIPS_stride_byte"},
      {DW_AT_MIPS_stride_elem, "DW_AT_MIPS_stride_elem"},
      {DW_AT_MIPS_ptr_dopetype, "DW_AT_MIPS_ptr_dopetype"},
      {DW_AT_MIPS_allocatable_dopetype, "DW_AT_MIPS_allocatable_dopetype"},
      {DW_AT_MIPS_assumed_shape_dopetype, "DW_AT_MIPS_assumed_shape_dopetype"},
      {DW_AT_MIPS_assumed_size, "DW_AT_MIPS_assumed_size"},
      {DW_AT_sf_names, "DW_AT_sf_names"},
      {DW_AT_src_info, "DW_AT_src_info"},
      {DW_AT_mac_info, "DW_AT_mac_info"},
      {DW_AT_src_coords, "DW_AT_src_coords"},
      {DW_AT_body_begin, "DW_AT_body_begin"},
      {DW_AT_body_end, "DW_AT_body_end"},
      {DW_AT_GNU_vector, "DW_AT_GNU_vector"},
      {DW_AT_GNU_guarded_by, "DW_AT_GNU_guarded_by"},
      {DW_AT_GNU_pt_guarded_by, "DW_AT_GNU_pt_guarded_by"},
      {DW_AT_GNU_guarded, "DW_AT_GNU_guarded"},
      {DW_AT_GNU_pt_guarded, "DW_AT_GNU_pt_guarded"},
      {DW_AT_GNU_locks_excluded, "DW_AT_GNU_locks_excluded"},
      {DW_AT_GNU_exclusive_locks_required,
       "DW_AT_GNU_exclusive_locks_required"},
      {DW_AT_GNU_shared_locks_required, "DW_AT_GNU_shared_locks_required"},
      {DW_AT_GNU_odr_signature, "DW_AT_GNU_odr_signature"},
      {DW_AT_GNU_template_name, "DW_AT_GNU_template_name"},
      {DW_AT_GNU_call_site_value, "DW_AT_GNU_call_site_value"},
      {DW_AT_GNU_call_site_data_value, "DW_AT_GNU_call_site_data_value"},
      {DW_AT_GNU_call_site_target, "DW_AT_GNU_call_site_target"},
      {DW_AT_GNU_call_site_target_clobbered,
       "DW_AT_GNU_call_site_target_clobbered"},
      {DW_AT_GNU_tail_call, "DW_AT_GNU_tail_call"},
      {DW_AT_GNU_all_tail_call_sites, "DW_AT_GNU_all_tail_call_sites"},
      {DW_AT_GNU_all_call_sites, "DW_AT_GNU_all_call_sites"},
      {DW_AT_GNU_all_source_call_sites, "DW_AT_GNU_all_source_call_sites"},
      {DW_AT_GNU_locviews, "DW_AT_GNU_locviews"},
      {DW_AT_GNU_entry_view, "DW_AT_GNU_entry_view"},
      {DW_AT_GNU_macros, "DW_AT_GNU_macros"},
      {DW_AT_GNU_deleted, "DW_AT_GNU_deleted"},
      {DW_AT_GNU_dwo_name, "DW_AT_GNU_dwo_name"},
      {DW_AT_GNU_dwo_id, "DW_AT_GNU_dwo_id"},
      {DW_AT_GNU_ranges_base, "DW_AT_GNU_ranges_base"},
      {DW_AT_GNU_addr_base, "DW_AT_GNU_addr_base"},
      {DW_AT_GNU_pubnames, "DW_AT_GNU_pubnames"},
      {DW_AT_GNU_pubtypes, "DW_AT_GNU_pubtypes"},
      {DW_AT_GNU_numerator, "DW_AT_GNU_numerator"},
      {DW_AT_GNU_denominator, "DW_AT_GNU_denominator"},
      {DW_AT_GNU_bias, "DW_AT_GNU_bias"},
      {DW_AT_hi_user, "DW_AT_hi_user"},
  };
  auto it = attributeNameMap.find(attrCode);
  if (it != attributeNameMap.end()) {
    return it->second.c_str();
  }
  return "Unknown Attribute";
}

int print_attribute(Dwarf_Attribute *attr, void *arg) {
  // Extract information from the attribute and print it
  // For example, you might want to print the attribute's name and value
  // The implementation depends on how you want to display the attributes
  LG_DBG("Attribute code %x(%s) - form %d", attr->code,
         get_attribute_name(attr->code), attr->form);
  // Return a non-zero value to continue iterating through attributes
  return 0;
}
} // namespace ddprof
