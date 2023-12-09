# Defines the skeleton
bpf_object(sample_processor sample_processor.bpf.c)
add_dependencies(sample_processor_skel libbpf-build bpftool-build generate_vmlinux_h)
message(STATUS "bpf object status =" ${BpfObject_FOUND})
target_include_directories(sample_processor_skel INTERFACE ${SKEL_INCLUDE_DIR})
