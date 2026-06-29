# Feature-categorized test suite. Each file maps to one testplan document
# (docs/testplan/NN-*.md) and is spec-compliant. All tests link the single
# iopmp model library; the multi-instance tests additionally use libsystem.
#
# Shared assertion macros live in test_utils.h alongside the tests.

function(add_iopmp_test name lib)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE ${lib})
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/libsystem/include)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_iopmp_test(test_01_registers_warl            iopmp)
add_iopmp_test(test_02_srcmd_table               iopmp)
add_iopmp_test(test_03_mdcfg_table               iopmp)
add_iopmp_test(test_04_entry_addr_modes          iopmp)
add_iopmp_test(test_05_priority_matching         iopmp)
add_iopmp_test(test_06_error_capture_reactions   iopmp)
add_iopmp_test(test_07_config_protection_locks   iopmp)
add_iopmp_test(test_08_stall_safe_runtime        iopmp)
add_iopmp_test(test_09_sps                       iopmp)
add_iopmp_test(test_10_non_priority_entries      iopmp)
add_iopmp_test(test_11_per_entry_suppression     iopmp)
add_iopmp_test(test_12_multi_fault_record        iopmp)
add_iopmp_test(test_13_msi                       iopmp)
add_iopmp_test(test_14_hwcfg_capability          iopmp)
add_iopmp_test(test_15_appnote_features          iopmp)
add_iopmp_test(test_16_implementation_models     iopmp)
add_iopmp_test(test_17_multi_instance_system     iopmp_system)
add_iopmp_test(test_18_cross_combinations        iopmp_system)
