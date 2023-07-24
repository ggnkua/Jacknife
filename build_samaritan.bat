@set PATH=%PATH%;c:\msys64\mingw64\bin
gcc -O2 samaritan.c dllmain.c dosfs-1.03\dosfs.c -shared -fpic -lshlwapi -o samaritan.exe
strip samaritan.exe
