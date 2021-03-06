set(SEEC_TEST_PREFIX "${SEEC_TEST_PREFIX}pointers-")

seec_test_build(arithmetic arithmetic.c "")
seec_test_run_pass(arithmetic "ok-zero"      "0")
seec_test_run_pass(arithmetic "ok-one-past"  "3")
seec_test_run_fail(arithmetic "fail-low"    "-1")
seec_test_run_fail(arithmetic "fail-high"    "4")

seec_test_build(constexpr constexpr.c "")
seec_test_run_pass(constexpr "ok" "")

seec_test_build(dereferencing dereferencing.c "")
seec_test_run_pass(dereferencing "ok-zero"       "0")
seec_test_run_fail(dereferencing "fail-one-past" "3")
seec_test_run_fail(dereferencing "fail-low"     "-1")
seec_test_run_fail(dereferencing "fail-high"     "4")

seec_test_build(indexing indexing.c "")
seec_test_run_pass(indexing "ok-zero"       "0")
seec_test_run_fail(indexing "fail-one-past" "3")
seec_test_run_fail(indexing "fail-low"     "-1")
seec_test_run_fail(indexing "fail-high"     "4")

seec_test_build(ptr_to_static ptr_to_static.c "")
seec_test_run_pass(ptr_to_static "ok-zero"       "0" )
seec_test_run_fail(ptr_to_static "fail-one-past" "10")
seec_test_run_fail(ptr_to_static "fail-low"     "-1" )
seec_test_run_fail(ptr_to_static "fail-high"     "11")

seec_test_build(ptr_to_literal ptr_to_literal.c "")
seec_test_run_pass(ptr_to_literal "ok-zero"       "0" )
seec_test_run_fail(ptr_to_literal "fail-one-past" "10")
seec_test_run_fail(ptr_to_literal "fail-low"     "-1" )
seec_test_run_fail(ptr_to_literal "fail-high"     "11")

seec_test_build(return_pointer_to_local return_pointer_to_local.c "")
seec_test_run_fail(return_pointer_to_local "fail" "")

seec_test_build(struct_byval struct_byval.c "")
seec_test_run_pass(struct_byval "valid" "valid")

seec_test_build(struct_return struct_return.c "")
seec_test_run_pass(struct_return "valid" "valid")

seec_test_build(temporal_local temporal_local.c "")
seec_test_run_fail(temporal_local "fail" "")

seec_test_build(print_argv_envp print_argv_envp.c "")
seec_test_run_pass_without_comparison(print_argv_envp "ok" "arg1 arg2 arg3")

