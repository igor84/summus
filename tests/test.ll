; ModuleID = 'test.smm'
target triple = "x86_64-pc-windows-msvc"

@y = global i64 0
@b = global double 0.000000e+00
@a = global float 0.000000e+00
@r = global i32 0

define i32 @main() {
entry:
  store i64 23112010824209201, i64* @y, align 8
  store i64 -1160, i64* @y, align 8
  %0 = load i64, i64* @y, align 8
  %1 = sitofp i64 %0 to double
  %2 = fdiv double 0xC2E58978F1910100, %1
  %3 = fadd double 1.230000e+02, %2
  store double %3, double* @b, align 8
  %4 = load double, double* @b, align 8
  %5 = fptrunc double %4 to float
  %6 = fmul float %5, 0x407433AE20000000
  %7 = fdiv float %6, 0x404B458100000000
  store float %7, float* @a, align 4
  %8 = load float, float* @a, align 4
  %9 = fpext float %8 to double
  %10 = fmul double %9, 2.143230e+03
  %11 = fdiv double %10, 5.432300e+02
  store double %11, double* @b, align 8
  %12 = load double, double* @b, align 8
  %13 = fadd double %12, 3.234000e+01
  %14 = fptosi double %13 to i32
  store i32 %14, i32* @r, align 4
  %15 = load i32, i32* @r, align 4
  %16 = call i32 @bla(i16 2, i16 3)
  %17 = add i32 %15, %16
  store i32 %17, i32* @r, align 4
  %18 = call i32 @putchar(i32 65)
  %19 = call i32 @putchar(i32 10)
  %20 = load i32, i32* @r, align 4
  ret i32 %20
}

define i32 @bla(i16, i16) {
entry:
  %a = alloca i16
  store i16 %0, i16* %a
  %b = alloca i16
  store i16 %1, i16* %b
  %2 = load i16, i16* %a, align 2
  %3 = sext i16 %2 to i32
  %4 = load i16, i16* %b, align 2
  %5 = sext i16 %4 to i32
  %6 = add i32 %3, %5
  ret i32 %6
}

declare i32 @putchar(i32)
