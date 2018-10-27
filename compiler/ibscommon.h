#pragma once

/**
 * All files in project should include this file as the first include
 * because it contains compile options and common defines
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1

// Disabled warnings pragmas
// Idented are suggests by LLVM which seem uneeded (time will tell if we need them).

// 4068 - unknown pragma
//		4091 - 'keyword' : ignored on left of 'type' when no variable is declared
// 4100 - 'identifier': unreferenced formal parameter
// 4127 - conditional expression is constant
//		4141 - 'modifier': used more than once
//		4146 - unary minus operator applied to unsigned type, result still unsigned
//		4180 - qualifier applied to function type has no meaning; ignored
// 4201 - anonimous unions in structs
// 4204 - non-constant aggregate initializer
//		4244 - conversion from 'type1' to 'type2', possible loss of data
//		4245 - conversion from 'type1' to 'type2', signed/unsigned mismatch
//		4258 - 'variable': definition from the for loop is ignored; the definition from the enclosing scope is used
//		4267 - 'variable': conversion from 'size_t' to 'type', possible loss of data
//		4310 - cast truncates constant value
//		4324 - 'structname': structure was padded due to __declspec(align())
//		4389 - 'operator': signed/unsigned mismatch
//		4456 - declaration of 'identifier' hides previous local declaration
//		4457 - declaration of 'identifier' hides function parameter
//		4459 - declaration of 'identifier' hides global declaration
//		4505 - 'function' : unreferenced local function has been removed
//		4592 - 'function': 'constexpr' call evaluation failed; function will be called at run-time
//		4701 - potentially uninitialized local variable 'name' used
//		4702 - unreachable code
//		4703 - potentially uninitialized local pointer variable '%s' used
//		4706 - assignment within conditional expression
//		4800 - 'type' : forcing value to bool 'true' or 'false' (performance warning)
//		4805 - unsafe mix of two different types in operation
// -w14062 - enumerator value in switch is not handled
// #pragma warning (disable : 4068 4100 4127 4201 4204)
#pragma warning (disable :  4068 4201 4204)

#else

#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

#endif // if MSVC
