# RunRuntimeTest.cmake
# If ZENCROP_RUN_RUNTIME_TESTS is not "1", exit with code 77 (CTest SKIP).
# Otherwise run TEST_EXE and fail the cmake -P process on non-zero.

if(NOT DEFINED ENV{ZENCROP_RUN_RUNTIME_TESTS} OR NOT "$ENV{ZENCROP_RUN_RUNTIME_TESTS}" STREQUAL "1")
  message(STATUS "SKIP runtime test (set ZENCROP_RUN_RUNTIME_TESTS=1): ${TEST_EXE}")
  if(WIN32)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env CMD /c "exit /b 77" RESULT_VARIABLE _skip_rc)
    # Fallback: write a tiny skip runner
    set(_skip_bat "${CMAKE_CURRENT_BINARY_DIR}/_skip77.bat")
    file(WRITE "${_skip_bat}" "@echo off\r\nexit /b 77\r\n")
    execute_process(COMMAND "${_skip_bat}" RESULT_VARIABLE _skip_rc)
  else()
    execute_process(COMMAND sh -c "exit 77" RESULT_VARIABLE _skip_rc)
  endif()
  # Propagate skip to ctest: cmake -P always returns 0 unless FATAL_ERROR.
  # So we use FATAL_ERROR with a special message only for real failures.
  # CTest skip requires the *test COMMAND* to exit 77, not cmake -P.
  # Therefore callers should set COMMAND to a script that exits 77.
  # This file is kept for documentation; prefer RunRuntimeTest.cmd.
  return()
endif()

execute_process(
  COMMAND "${TEST_EXE}"
  RESULT_VARIABLE _rc
)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Runtime test failed: ${TEST_EXE} rc=${_rc}")
endif()
