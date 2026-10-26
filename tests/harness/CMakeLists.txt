# Stasis's test harness, and its own checks, the CTest tests harness.*.

# The harness: TEST, TEST_LIST, the assertions, and main.
add_library(stasis_harness STATIC harness.c harness.h)
target_include_directories(stasis_harness PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
stasis_target_conventions(stasis_harness)

# The harness's own checks run two programs, one whose cases pass or fail on purpose and one whose
# cases all pass, and look at how each exits and at what it prints.
foreach(program IN ITEMS selftest passing)
    add_executable(stasis_harness_${program} ${program}.c)
    target_link_libraries(stasis_harness_${program} PRIVATE stasis_harness)
    stasis_target_conventions(stasis_harness_${program})
endforeach()

# Adds the CTest test harness.<name>, which runs the self-test program, or the one PROGRAM names,
# with the arguments after ARGS. It expects the program to exit with EXIT_CODE and to print the
# lines that LINES gives, as expect.cmake checks them.
function(stasis_add_harness_check name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "PROGRAM;EXIT_CODE" "ARGS;LINES")
    if(NOT arg_PROGRAM)
        set(arg_PROGRAM selftest)
    endif()
    set(command "$<TARGET_FILE:stasis_harness_${arg_PROGRAM}>" ${arg_ARGS})
    add_test(NAME harness.${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DCOMMAND=${command}"
            "-DEXIT_CODE=${arg_EXIT_CODE}"
            "-DLINES=${arg_LINES}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect.cmake"
    )
endfunction()

stasis_add_harness_check(lists_every_case ARGS --list EXIT_CODE 0
    LINES
        passes fails_true fails_int fails_uint fails_size fails_str fails_str_null fails_mem
        fails_long_mem fails_float fails_double
)
stasis_add_harness_check(runs_every_case PROGRAM passing EXIT_CODE 0
    LINES "PASS first" "PASS second" "2 passed"
)
stasis_add_harness_check(stops_at_the_first_failing_case EXIT_CODE 1
    LINES
        "PASS passes"
        [[.*selftest\.c:[0-9]+: ASSERT_TRUE\(1 \+ 1 == 3\) failed]]
        "FAIL fails_true"
)
stasis_add_harness_check(runs_one_case_by_name ARGS passes EXIT_CODE 0 LINES "PASS passes")
stasis_add_harness_check(refuses_a_name_that_matches_no_case ARGS pass EXIT_CODE 2
    LINES [[.*: no test case is named pass]]
)
stasis_add_harness_check(refuses_two_names ARGS passes fails_int EXIT_CODE 2
    LINES [=[usage: .* \[--list \| <case>\]]=]
)
stasis_add_harness_check(refuses_an_unknown_option ARGS --all EXIT_CODE 2
    LINES [=[usage: .* \[--list \| <case>\]]=]
)

# Each failed assertion names where it failed, and a comparison prints both values. It ends the
# program, so nothing after it runs.
stasis_add_harness_check(reports_a_false_condition ARGS fails_true EXIT_CODE 1
    LINES
        [[.*selftest\.c:[0-9]+: ASSERT_TRUE\(1 \+ 1 == 3\) failed]]
        "FAIL fails_true"
)
stasis_add_harness_check(reports_both_ints ARGS fails_int EXIT_CODE 1
    LINES
        [[.*selftest\.c:[0-9]+: ASSERT_INT_EQ\(-2, 3\) failed]]
        "  actual:   -2"
        "  expected: 3"
        "FAIL fails_int"
)
stasis_add_harness_check(reports_both_uints_from_a_called_function ARGS fails_uint EXIT_CODE 1
    LINES
        [[.*selftest\.c:[0-9]+: ASSERT_UINT_EQ\(value, 0\) failed]]
        "  actual:   18446744073709551615"
        "  expected: 0"
        "FAIL fails_uint"
)
stasis_add_harness_check(reports_both_sizes ARGS fails_size EXIT_CODE 1
    LINES
        [[.*selftest\.c:[0-9]+: ASSERT_SIZE_EQ\(SIZE_MAX, 1\) failed]]
        "  actual:   18446744073709551615"
        "  expected: 1"
        "FAIL fails_size"
)
stasis_add_harness_check(reports_both_strings_escaped ARGS fails_str EXIT_CODE 1
    LINES
        [[.*selftest\.c:[0-9]+: ASSERT_STR_EQ\("tab\\there", "quote\\"there"\) failed]]
        [[  actual:   "tab\\there"]]
        [[  expected: "quote\\"there"]]
        "FAIL fails_str"
)
stasis_add_harness_check(reports_a_null_string ARGS fails_str_null EXIT_CODE 1
    LINES
        [[.*selftest\.c:[0-9]+: ASSERT_STR_EQ\(NULL, "stasis"\) failed]]
        "  actual:   NULL"
        [[  expected: "stasis"]]
        "FAIL fails_str_null"
)
stasis_add_harness_check(reports_both_memory_blocks ARGS fails_mem EXIT_CODE 1
    LINES
        [[.*selftest\.c:[0-9]+: ASSERT_MEM_EQ\(.*, 4\) failed]]
        "  first difference at byte 2 of 4; bytes 0 to 3 are:"
        "  actual:   00 01 02 03"
        "  expected: 00 01 ff 03"
        "FAIL fails_mem"
)
stasis_add_harness_check(reports_memory_around_the_difference ARGS fails_long_mem EXIT_CODE 1
    LINES
        [[.*selftest\.c:[0-9]+: ASSERT_MEM_EQ\(actual, expected, sizeof actual\) failed]]
        "  first difference at byte 40 of 64; bytes 32 to 63 are:"
        "  actual:   00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
        "  expected: 00 00 00 00 00 00 00 00 ab 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
        "FAIL fails_long_mem"
)
stasis_add_harness_check(reports_both_floats ARGS fails_float EXIT_CODE 1
    LINES
        [[.*selftest\.c:[0-9]+: ASSERT_FLOAT_EQ\(0\.0F, -0\.0F\) failed]]
        "  actual:   0"
        "  expected: -0"
        "FAIL fails_float"
)
stasis_add_harness_check(reports_both_doubles ARGS fails_double EXIT_CODE 1
    LINES
        [[.*selftest\.c:[0-9]+: ASSERT_DOUBLE_EQ\(0\.1, \(double\)NAN\) failed]]
        "  actual:   0.10000000000000001"
        "  expected: nan"
        "FAIL fails_double"
)
