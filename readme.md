# Summus
Basic compiler frontend using LLVM as backend.

# Mission
Create and refine a most basic compiler using LLVM as backend that can easily be used as a starting point for any kind of compiler and can also be used in learning the compiler construction basics.

# Requirements and compiling

## Windows
- [Visual Studio 2015](https://www.visualstudio.com/products/free-developer-offers-vs) (Community Edition)
- [Clang](https://sourceforge.net/projects/clangonwin/files/MsvcBuild/3.9/) compiler for Windows (LLVM-3.9.0svn-r258602-win64.exe)

If you install Clang to C:\\Program Files\\LLVM\\ the VS project should just work. Otherwise you will have to change project library path under linker options. Also in case you get a lot of linker errors try changing Runtime Library in project options, under C/C++ -> Code Generation, to /MD or /MT.

## Linux
- Clang compiler
- LLVM (On Kubuntu: `# sudo apt-get install llvm`)
- LibZ (On Kubuntu: `# sudo apt-get install libz-dev`)

You can use clangCompile.sh or gccCompile.sh scripts to compile the compiler. You will get the binary output in the bin/ subdirectory.

# Features
Language at the moment supports:
- arithmetic, relational and boolean expressions
- type inference
- functions and blocks
- block scope
- multiline string literals that parse escape sequences using <"> as delimiter
- multiline string literals that read backslash literally using either <'> or <`> delimiter
- string literal modificators:
  - <-"..."> means collapse all whitespace to one space
  - <|"..."> means remove as much leading space from every line as there is in second line of literal. This is very useful for keeping code aligned when you write multiline strings like SQL queries and similar.
- char literals defined as <@c> for example
- and that is it :)

The goal of this project is not to create a perfect new language but to try and create a perfect compiler for minimal possible language.

### Character literal examples
| C char | Summus char |
|:------:|:-----------:|
| 'a'    | @a          |
| 'z'    | @z          |
| '\n'   | @\n         |
| '\t'   | @\t         |
| '@'    | @@          |
| '\x20' | @\x20       |
| '\20'  | @\16        |
| '\\'   | @\\         |
| ' '    | @&nbsp;     |

# Commands
Built compiler outputs readable LLVM assembly into a test.ll file at the moment. Here are some useful commands you can run on that file:
- `clang -x ir -o test.exe test.ll` to make executable from ll file
- `clang -c -x ir -o test.o test.ll` to make native object file from ll file
- `llvm-objdump.exe -disassemble test.o` to get native disassembly of object file

## TODO
- Do complete code review and add all the comments
- Add test cases for every possible error condition
- Add test cases for every construct
- Add bitwise operators
- Add LLVM debug info
- Add compiling to native code
- Add auto linker invocation
- Add Settings
