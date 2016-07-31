; ModuleID = 'test.smm'
target triple = "x86_64-pc-windows-msvc"

define i32 @main() {
entry:
  %ui32 = alloca i32, align 4
  %i8 = alloca i8, align 1
  %result = alloca i32, align 4
  %z = alloca i8, align 1
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
  store i32 100, i32* %ui32, align 4
  store i8 -1, i8* %i8, align 1
  %0 = load i32, i32* %ui32, align 4
  %1 = load i8, i8* %i8, align 1
  %2 = sext i8 %1 to i32
  %3 = sdiv i32 %0, %2
  %4 = add i32 123, %3
  store i32 %4, i32* %result, align 4
  store i8 -76, i8* %z, align 1
  store i64 0, i64* %y, align 8
  store i64 23112010824209933, i64* %y, align 8
  store i64 -1160, i64* %y, align 8
  %5 = load i64, i64* %y, align 8
  %6 = sitofp i64 %5 to double
  %7 = fdiv double 0xC2E58978F19101C0, %6
  %8 = fadd double 1.230000e+02, %7
  store double %8, double* %f64, align 8
  %9 = load double, double* %f64, align 8
  %10 = fptrunc double %9 to float
  %11 = fmul float %10, 0x407433AE20000000
  %12 = fdiv float %11, 0x404B458100000000
  store float %12, float* %f32, align 4
  %13 = load float, float* %f32, align 4
  %14 = fpext float %13 to double
  %15 = fmul double %14, 2.143230e+03
  %16 = fdiv double %15, 5.432300e+02
  store double %16, double* %f64, align 8
  %17 = load double, double* %f64, align 8
  %18 = fadd double %17, 3.234000e+01
  %19 = fptosi double %18 to i32
  store i32 %19, i32* %r, align 4
  %20 = load i32, i32* %r, align 4
  %21 = call i32 @bla(i16 2, i16 3)
  %22 = add i32 %20, %21
  store i32 %22, i32* %r, align 4
  %23 = call i32 @putchar(i32 65)
  %24 = call i32 @putchar(i32 10)
  %25 = load i64, i64* %y, align 8
  %26 = load i8, i8* %z, align 1
  %27 = zext i8 %26 to i64
  %28 = icmp sgt i64 %25, %27
  br i1 %28, label %29, label %32

; <label>:29                                      ; preds = %entry
  %30 = load i8, i8* %z, align 1
  %31 = icmp ne i8 %30, 0
  br label %32

; <label>:32                                      ; preds = %29, %entry
  %33 = phi i1 [ false, %entry ], [ %31, %29 ]
  store i1 %33, i1* %a, align 1
  %34 = load double, double* %f64, align 8
  %35 = load float, float* %f32, align 4
  %36 = fpext float %35 to double
  %37 = fcmp olt double %34, %36
  br i1 %37, label %41, label %38

; <label>:38                                      ; preds = %32
  %39 = load float, float* %f32, align 4
  %40 = fcmp ogt float %39, 0xC2E5897900000000
  br label %41

; <label>:41                                      ; preds = %38, %32
  %42 = phi i1 [ true, %32 ], [ %40, %38 ]
  store i1 %42, i1* %b, align 1
  store i1 false, i1* %c, align 1
  store i1 true, i1* %d, align 1
  store i1 true, i1* %e, align 1
  store i1 false, i1* %f, align 1
  store i1 false, i1* %g, align 1
  store i1 true, i1* %h, align 1
  %43 = load i1, i1* %a, align 1
  br i1 %43, label %46, label %44

; <label>:44                                      ; preds = %41
  %45 = load i1, i1* %b, align 1
  br i1 %45, label %46, label %53

; <label>:46                                      ; preds = %44, %41
  %47 = load i1, i1* %c, align 1
  br i1 %47, label %50, label %48

; <label>:48                                      ; preds = %46
  %49 = load i1, i1* %d, align 1
  br label %50

; <label>:50                                      ; preds = %48, %46
  %51 = phi i1 [ true, %46 ], [ %49, %48 ]
  %52 = xor i1 %51, true
  br label %53

; <label>:53                                      ; preds = %50, %44
  %54 = phi i1 [ false, %44 ], [ %52, %50 ]
  %55 = load i1, i1* %e, align 1
  br i1 %55, label %62, label %56

; <label>:56                                      ; preds = %53
  %57 = load i1, i1* %f, align 1
  br i1 %57, label %62, label %58

; <label>:58                                      ; preds = %56
  %59 = load i1, i1* %g, align 1
  br i1 %59, label %60, label %62

; <label>:60                                      ; preds = %58
  %61 = load i1, i1* %h, align 1
  br label %62

; <label>:62                                      ; preds = %60, %58, %56, %53
  %63 = phi i1 [ true, %53 ], [ true, %56 ], [ false, %58 ], [ %61, %60 ]
  %64 = icmp ne i1 %54, %63
  %65 = zext i1 %64 to i32
  store i32 %65, i32* %res, align 4
  %66 = load i32, i32* %res, align 4
  ret i32 %66
}

define i32 @bla(i16 %a, i16 %b) {
entry:
  %0 = alloca i16
  %1 = alloca i16
  store i16 %a, i16* %0
  store i16 %b, i16* %1
  %2 = load i16, i16* %0, align 2
  %3 = sext i16 %2 to i32
  %4 = load i16, i16* %1, align 2
  %5 = sext i16 %4 to i32
  %6 = add i32 %3, %5
  ret i32 %6
}

declare i32 @putchar(i32)
