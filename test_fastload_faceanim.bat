@echo off
REM Test for issue #847: fastload face animation stuck
REM Launches vkQuake with the test script

set VKQUAKE=C:\project\vkQuake\Windows\VisualStudio\Build-vkQuake\x64\Release\vkQuake.exe

echo Running test_faceanim via %VKQUAKE%...
echo Look for "PASS" or "FAIL" in the console output.
echo.

"%VKQUAKE%" +exec test_faceanim.cfg +quit
