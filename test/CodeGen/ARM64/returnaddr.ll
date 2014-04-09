; RUN: llc < %s -march=arm64 | FileCheck %s

define i8* @rt0(i32 %x) nounwind readnone {
entry:
; CHECK-LABEL: rt0:
; CHECK: mov x0, lr
; CHECK: ret
  %0 = tail call i8* @llvm.returnaddress(i32 0)
  ret i8* %0
}

define i8* @rt2() nounwind readnone {
entry:
; CHECK-LABEL: rt2:
; CHECK: stp x29, lr, [sp, #-16]!
; CHECK: mov x29, sp
; CHECK: ldr x[[REG:[0-9]+]], [x29]
; CHECK: ldr x[[REG2:[0-9]+]], [x[[REG]]]
; CHECK: ldr x0, [x[[REG2]], #8]
; CHECK: ldp x29, lr, [sp], #16
; CHECK: ret
  %0 = tail call i8* @llvm.returnaddress(i32 2)
  ret i8* %0
}

declare i8* @llvm.returnaddress(i32) nounwind readnone
