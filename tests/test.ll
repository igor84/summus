; ModuleID = 'test.smm'
target triple = "x86_64-pc-windows-msvc"

define i32 @main() {
entry:
  %y = alloca i64, align 8
  %f64 = alloca double, align 8
  %f32 = alloca float, align 4
  %r = alloca i32, align 4
  %a = alloca i1, align 1
  %b = alloca i1, align 1
  %c = alloca i1, align 1
  %d = alloca i1, align 1
  %e = alloca i1, align 1
  %f = alloca i1, align 1
  %g = alloca i1, align 1
  %h = alloca i1, align 1
  %res = alloca i32, align 4
  store i64 0, i64* %y, align 8
  store i64 23112010824209201, i64* %y, align 8
  store i64 -1160, i64* %y, align 8
  %0 = load i64, i64* %y, align 8
  %1 = sitofp i64 %0 to double
  %2 = fdiv double 0xC2E58978F1910100, %1
  %3 = fadd double 1.230000e+02, %2
  store double %3, double* %f64, align 8
  %4 = load double, double* %f64, align 8
  %5 = fptrunc double %4 to float
  %6 = fmul float %5, 0x407433AE20000000
  %7 = fdiv float %6, 0x404B458100000000
  store float %7, float* %f32, align 4
  %8 = load float, float* %f32, align 4
  %9 = fpext float %8 to double
  %10 = fmul double %9, 2.143230e+03
  %11 = fdiv double %10, 5.432300e+02
  store double %11, double* %f64, align 8
  %12 = load double, double* %f64, align 8
  %13 = fadd double %12, 3.234000e+01
  %14 = fptosi double %13 to i32
  store i32 %14, i32* %r, align 4
  %15 = load i32, i32* %r, align 4
  %16 = call i32 @bla(i16 2, i16 3)
  %17 = add i32 %15, %16
  store i32 %17, i32* %r, align 4
  %18 = call i32 @putchar(i32 65)
  %19 = call i32 @putchar(i32 10)
  store i1 true, i1* %a, align 1
  store i1 false, i1* %b, align 1
  store i1 false, i1* %c, align 1
  store i1 true, i1* %d, align 1
  store i1 true, i1* %e, align 1
  store i1 false, i1* %f, align 1
  store i1 false, i1* %g, align 1
  store i1 true, i1* %h, align 1
  %20 = load i1, i1* %a, align 1
  br i1 %20, label %23, label %21

; <label>:21                                      ; preds = %entry
  %22 = load i1, i1* %b, align 1
  br i1 %22, label %23, label %27

; <label>:23                                      ; preds = %21, %entry
  %24 = load i1, i1* %c, align 1
  br i1 %24, label %27, label %25

; <label>:25                                      ; preds = %23
  %26 = load i1, i1* %d, align 1
  br label %27

; <label>:27                                      ; preds = %25, %23, %21
  %28 = phi i1 [ false, %21 ], [ true, %23 ], [ %26, %25 ]
  %29 = load i1, i1* %e, align 1
  br i1 %29, label %36, label %30

; <label>:30                                      ; preds = %27
  %31 = load i1, i1* %f, align 1
  br i1 %31, label %36, label %32

; <label>:32                                      ; preds = %30
  %33 = load i1, i1* %g, align 1
  br i1 %33, label %34, label %36

; <label>:34                                      ; preds = %32
  %35 = load i1, i1* %h, align 1
  br label %36

; <label>:36                                      ; preds = %34, %32, %30, %27
  %37 = phi i1 [ true, %27 ], [ true, %30 ], [ false, %32 ], [ %35, %34 ]
  %38 = icmp ne i1 %28, %37
  %39 = zext i1 %38 to i32
  store i32 %39, i32* %res, align 4
  %40 = load i32, i32* %res, align 4
  ret i32 %40
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
