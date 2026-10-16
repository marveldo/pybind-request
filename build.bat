@echo off
set PY=C:\Users\utibe\AppData\Local\Programs\Python\Python314
set SSL=C:\vcpkg\installed\x64-mingw-static

g++ -O2 -std=c++20 -shared -static -o netcp.cp314-win_amd64.pyd winsock.cpp ^
  -I%PY%\Include -I.venv\Lib\site-packages\pybind11\include -I%SSL%\include ^
  -L%PY%\libs -L%SSL%\lib ^
  -lssl -lcrypto -lpython314 -lws2_32 -lcrypt32 -lbcrypt -ladvapi32 -luser32
