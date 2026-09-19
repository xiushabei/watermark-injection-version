@echo off
set VCDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207
set KITINC=C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0
set KITLIB=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0
"%VCDIR%\bin\Hostx64\x64\cl.exe" /nologo /EHsc /O2 /utf-8 /I "%VCDIR%\include" /I "%KITINC%\um" /I "%KITINC%\shared" /I "%KITINC%\ucrt" giftest.cpp /link /LIBPATH:"%VCDIR%\lib\x64" /LIBPATH:"%KITLIB%\um\x64" /LIBPATH:"%KITLIB%\ucrt\x64" windowscodecs.lib ole32.lib
