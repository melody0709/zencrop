@echo off
setlocal
rem Usage: run_runtime_test.cmd path\to\test.exe [args...]
rem If ZENCROP_RUN_RUNTIME_TESTS is not 1, exit 77 (CTest SKIP).
if not "%ZENCROP_RUN_RUNTIME_TESTS%"=="1" (
  echo SKIP runtime test: %~1
  exit /b 77
)
"%~1" %2 %3 %4 %5 %6 %7 %8 %9
exit /b %ERRORLEVEL%
