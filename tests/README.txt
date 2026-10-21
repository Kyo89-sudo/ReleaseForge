Test del core portabile (template, JSON, torrent, titolo release):
  g++ -std=c++17 -Isrc tests/core_tests.cpp src/core/*.cpp -o core_tests && ./core_tests
Con Visual Studio: build.bat li esegue automaticamente (target core_tests).
