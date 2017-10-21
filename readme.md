# Summus
Basic compiler frontend using LLVM as backend written without C macros (includes and simple defines don't count 😊) so it is easy to read and understand.

# Mission
Create and refine a most basic compiler using LLVM as backend that can easily be used as a starting point for any kind of compiler and can also be used in learning the compiler construction basics.

Feel free to open issues if you feel any part of the code could be better documented.

# Requirements and compiling

## Windows
- [Visual Studio 2017](https://www.visualstudio.com/vs/community/) (Community Edition)
- [Clang](https://sourceforge.net/projects/clangonwin/files/MsvcBuild/3.9/) compiler for Windows (LLVM-3.9.0svn-r258602-win64.exe)

If you install Clang to C:\\Program Files\\LLVM\\ the VS project should just work. Otherwise you will have to change project library path under linker options. Also in case you get a lot of linker errors try changing Runtime Library in project options, under C/C++ -> Code Generation, to /MD or /MT.

You could also try to setup official Clang build from [llvm.org](http://llvm.org) but I found it is not as straightforward as the unofficial build I linked above.

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
- if and if/else statements
- while statements
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
Once you build summus compiler you can use these commands with it:
- `summus inputfile.smm -o outfile.ll` to compile given smm file to LLVM assembly which will be written in given ll file
- `summus -pp1 inputfile.smm | dot -Tsvg -oast.svg` to generate image of AST tree if you have [GraphViz](http://www.graphviz.org/) installed (pp1 stands for `print pass 1` and it supports pp1, pp2 and pp3)

Here are some useful commands you can run on that output ll file:
- `clang -x ir -o test.exe test.ll` to make native executable from ll file
- `clang -c -x ir -o test.o test.ll` to make native object file from ll file
- `llvm-objdump.exe -disassemble test.o` to get native disassembly of object file

Also if you want to experiment and discover what kind of llvm code needs to be written for certain constructs in C and C++ you can write the code you want to compile in a test.cpp file and then run:
- `clang -S -emit-llvm test.cpp -o test.ll` to compile any cpp code to llvm assembly
- `llc -march=cpp test.ll -o llvmtest.cpp` to convert llvm assembly to cpp file of llvm API calls that generate that assembly

For this you need llc built with cpp option which isn't always included in binary builds of LLVM so you may need to compile LLVM manually in order to get it.

# Compiler structure

Compiler source is in compiler directory but it also uses smmgvpass from utility folder:
- `ibscommon` just contains some common C compiler directives or pragmas
- `ibsallocator` contains implementation of custom memory allocator
- `ibsdictionary` contains implementation of custom key-value store where multiple values can be pushed and popup under the same key
- `smmmsgs` contains code that collects error and warning messages from compiler and can output them
- `smmlexer` contains code that transforms input file text into a sequence of tokens, parsing numbers, keywords, symbols etc.
- `smmparser` contains code that parses the sequence of tokens from lexer and builds Abstract Syntax Tree (AST) doing some validations on the way
- `smmtypeinference` does further validations and infers type of expressions and variables based on basic elements of expressions
- `smmsempass` does further validations and propagates the biggest infered type down toward basic elements of expressions
- `smmllvmcodegen` goes through now valid AST and generates LLVM module which it then outputs as LLVM assembly
- `smmgvpass` from utility folder goes through AST and prints it in a form that [GraphViz](http://www.graphviz.org/) can then parse and generate an image of it as you can see in ast.svg file

Test folder contains code and samples for automatic tests
- `AllTests` is entry point for running tests
- `CuTest` is small C unit testing framework from http://cutest.sourceforge.net/
- `smmlexertests` contains unit tests for lexer
- `smmparsertests` contains unit tests for parser which use the 3 files bellow to process samples from tests/samples directory 
- `smmastwritter` will write AST of a sample to ast file in easy to parse but still human readable format if such file doesn't already exist 
- `smmastreader` will read AST from ast file if it already exists
- `smmastmatcher` will compare AST generated from parsing the sample with the one read from corresponding ast file and report if there are any differences

# Example improvement: How I added support for if statement
You can see the exact changes mentioned bellow in a commit called "Adds support for if and while statements" from January 14th 2017.

1.  First I decided what the sintax of if statement I want. I never liked parantheses around conditions so I decided I want this format:  
    `if condition then statement; else statement; // statement can also be { block }`
2.  This means I first need to define 3 new keywords in lexer: `if`, `then` and `else`.
3.  In smmlexer.h I add three new values to SmmTokenKind enum: tkIf, tkThen and tkElse.
4.  In smmlexer.c I add string representations of these new enum values to tokenTypeToString array.
5.  I add definitions of new keywords to keywords array within initSymTableWithKeywords function in the same file.
6.  Within parser I need to have a node that represents if statement. That node must, besides a pointer to next statement, also have pointers to then and else statement so I need to define a new node type in smmparser.h. The new node type is called SmmAstIfWhileNode because I can reuse it for while statement as well. I also add new enum value to SmmAstNodeKind: nkSmmIf.
7.  In smmparser.c I first add string representations of new node kind values to nodeKindToString array. Since I am adding new statement I need to handle it in function parseStatement so I add a case for tkSmmIf token and make it call a new function: parseIfWhileStmt. In it I call parseExpression to parse the condition, then I check if tkSmmThen token is present and after that I call parseStament to parse if body. If after this the next token is tkSmmElse I again call parseStatement to parse the else part and I link it all into a newly created `if` node. Thus I get AST representation of `if` statement.
8.  To make sure all this works I now add handling of this new node kind to smmgvpass.c so that I can print it and see how it looks like. I just add some code under case nkSmmIf inside processStatement function to print `if` node itself and call processExpression and processStatement in order to print nodes for its condition and body.
9.  I compile all this, write some sample if/then code in inputfile.smm and I run `summus -pp1 inputfile.smm | dot -Tsvg -oast.svg` to generate an image of AST tree.
10. After all that works I can add handling to further passes. First is smmtypeinference.c. Like every pass it has processStatement function and within it I add a new case for nkSmmIf node where I just call processExpression and processStatement to do type inference for its condition and body.
11. Next is smmsempass.c where again I add handling of new node under processStatement function. In processIfWhile function that I call there I also just call processExpression and processStatement for condition and body which will check if all operand types are correct.
12. At this point I can compile summus and run `summus -pp2 inputfile.smm | dot -Tsvg -oast.svg` and the same with `-pp3` parameter to see how AST looks after each pass.
13. Last pass is smmllvmcodegen.c where I need to construct LLVM module. In new processIf function that I again call from processStatement I first build LLVMBasicBlock for `then` body, for `else` body if it is given and for code that comes after if statement. If root of condition node is logical `and` or `or` node I call processAndOrInstr function directly because I just need conditional jumps it generates while I call processExpression for all other types of nodes. If I also called processExpression for logical nodes I would get a node that represents a resulting value of the condition (which is true or false) and then generate additional conditional jump based on that value which creates some extra instructions that I don't need. After this I position instruction building in `then` body block and call processStatement to generate instructions for it. At the end I add a branch instruction which jumps to end block. If there is an else body I position builder in its block and again call processStatement after which I also add a branch instruction that jumps to end block. At the end I position the builder in end block so the rest of code is generated in it.

This is where I stopped and left the rest of the work for the reader. The next step is to test different kind of invalid code and see how summus compiler handles it like missing `then` and similar. Once you are happy how it does that you need to write automatic tests for it:

1.  First you write a sample for all invalid situations and a sample for valid combinations of if statement inside tests/samples.
2.  You add handling of if statement to smmastwritter.c under processStatement function so it writes relevant data.
3.  You run the tests and you get ast files generated for new samples. You need to manually check these ast files and make sure all the data is as expected.
4.  You add handling of if statement to smmastreader.c and smmastmatcher.c so you can load the if node from ast file and then compare it with original.
5.  When you run the tests next time they should load ast file and compare it to parsed original and everything you check for in smmastmatcher should pass.

## Further things that can be done
- Add support for `for` statement
- Add bitwise operators
- Add support for pointers
- Add support for structs
- Add support for arrays (they are most flexible if in native code they are written as struct which contains length and pointer to first element)
- Add support for strings (lexer can already read them but parser and further passes don't know how to handle them)
- Add some string and array operators like `[start:end]` for slicing and `~` for contcatenating
- Add LLVM debug info
- Add compiling to native code
- Add auto linker invocation
