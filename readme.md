# Summus
Basic compiler frontend using LLVM as backend.

# Mission
Create and refine a most basic compiler using LLVM as backend that can easily be used as a starting point for any kind of compiler and can also be used in learning the compiler construction basics.

# Commands
`clang -x ir -o test.exe test.ll` to make executable from ll file
`clang -c -x ir -o test.o test.ll` to make native object file from ll file
`llvm-objdump.exe -disassemble test.o` to get native disassembly of object file

## TODO
- Do complete code review and add all the comment
- Add test cases for every possible error condition
- Add test cases for every construct
- Add logical operators
- Add bitwise operators
- Add LLVM debug info
- Add compiling to native code
- Add auto linker invocation
- Add Settings
