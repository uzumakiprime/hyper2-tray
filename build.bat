@echo off
REM Run this from "x64 Native Tools Command Prompt for VS" (or Developer Command Prompt).
cl /nologo /EHsc /O2 /std:c++17 /DUNICODE /D_UNICODE main.cpp ^
   /Fe:Haste2Battery.exe ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib setupapi.lib hid.lib advapi32.lib
if %errorlevel%==0 (echo. & echo Built Haste2Battery.exe) else (echo. & echo Build failed)
