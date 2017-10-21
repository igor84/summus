; ModuleID = 'test.smm'
source_filename = "test.smm"
target triple = "x86_64-pc-windows-msvc"

@ui32 = global i32 100

define i32 @bla_int16_int16(i16 %a, i16 %b) {
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

define i32 @main() {
entry:
  %i8 = alloca i8, align 1
  %ui32 = alloca i32, align 4
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
  store i8 -1, i8* %i8, align 1
  store i32 20, i32* %ui32, align 4
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
  %21 = call i32 @bla_int16_int16(i16 2, i16 3)
  %22 = add i32 %20, %21
  store i32 %22, i32* %r, align 4
  %23 = call i32 @putchar(i32 65)
  %24 = call i32 @putchar(i32 10)
  %25 = load i64, i64* %y, align 8
  %26 = icmp ne i64 %25, 0
  br i1 %26, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  %27 = load i8, i8* %z, align 1
  %28 = zext i8 %27 to i64
  store i64 %28, i64* %y, align 8
  %29 = load i64, i64* %y, align 8
  %30 = trunc i64 %29 to i8
  store i8 %30, i8* %z, align 1
  br label %if.end

if.end:                                           ; preds = %if.then, %entry
  %31 = load i64, i64* %y, align 8
  %32 = load i8, i8* %z, align 1
  %33 = zext i8 %32 to i64
  %34 = icmp sgt i64 %31, %33
  br i1 %34, label %35, label %38

; <label>:35:                                     ; preds = %if.end
  %36 = load i8, i8* %z, align 1
  %37 = icmp ne i8 %36, 0
  br label %38

; <label>:38:                                     ; preds = %35, %if.end
  %39 = phi i1 [ false, %if.end ], [ %37, %35 ]
  store i1 %39, i1* %a, align 1
  %40 = load double, double* %f64, align 8
  %41 = load float, float* %f32, align 4
  %42 = fpext float %41 to double
  %43 = fcmp olt double %40, %42
  br i1 %43, label %47, label %44

; <label>:44:                                     ; preds = %38
  %45 = load float, float* %f32, align 4
  %46 = fcmp ogt float %45, 0xC2E5897900000000
  br label %47

; <label>:47:                                     ; preds = %44, %38
  %48 = phi i1 [ true, %38 ], [ %46, %44 ]
  store i1 %48, i1* %b, align 1
  store i1 false, i1* %c, align 1
  store i1 true, i1* %d, align 1
  store i1 true, i1* %e, align 1
  store i1 false, i1* %f, align 1
  store i1 false, i1* %g, align 1
  store i1 true, i1* %h, align 1
  store i32 0, i32* %res, align 4
  %49 = load i1, i1* %a, align 1
  br i1 %49, label %52, label %50

; <label>:50:                                     ; preds = %47
  %51 = load i1, i1* %b, align 1
  br i1 %51, label %52, label %59

; <label>:52:                                     ; preds = %50, %47
  %53 = load i1, i1* %c, align 1
  br i1 %53, label %56, label %54

; <label>:54:                                     ; preds = %52
  %55 = load i1, i1* %d, align 1
  br label %56

; <label>:56:                                     ; preds = %54, %52
  %57 = phi i1 [ true, %52 ], [ %55, %54 ]
  %58 = xor i1 %57, true
  br label %59

; <label>:59:                                     ; preds = %56, %50
  %60 = phi i1 [ false, %50 ], [ %58, %56 ]
  %61 = load i1, i1* %e, align 1
  br i1 %61, label %68, label %62

; <label>:62:                                     ; preds = %59
  %63 = load i1, i1* %f, align 1
  br i1 %63, label %68, label %64

; <label>:64:                                     ; preds = %62
  %65 = load i1, i1* %g, align 1
  br i1 %65, label %66, label %68

; <label>:66:                                     ; preds = %64
  %67 = load i1, i1* %h, align 1
  br label %68

; <label>:68:                                     ; preds = %66, %64, %62, %59
  %69 = phi i1 [ true, %59 ], [ true, %62 ], [ false, %64 ], [ %67, %66 ]
  %70 = icmp ne i1 %60, %69
  br i1 %70, label %if.then1, label %if.else

if.then1:                                         ; preds = %68
  store i32 1, i32* %res, align 4
  br label %if.else

if.else:                                          ; preds = %if.then1, %68
  store i32 2, i32* %res, align 4
  br label %if.end2

if.end2:                                          ; preds = %if.else
  %71 = load i32, i32* %res, align 4
  ret i32 %71
}
