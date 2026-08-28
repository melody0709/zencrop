@echo off
setlocal
rem Inventory-only CTest entry for tests not yet built as CMake targets.
rem Always exits 77 so CTest marks SKIP (SKIP_RETURN_CODE 77).
rem Args: <test_id> [reason words...]
echo SKIP inventory: %*
exit /b 77