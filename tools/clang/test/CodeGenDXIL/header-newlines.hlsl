// RUN: %dxc -T ps_6_0 -E main -Fh %t.header.h -Vn g_main %s
// RUN: FileCheck %s --input-file=%t.header.h
//
// Make sure dxc does not emit hard-coded carriage returns into the generated
// header. The shader source above contains only LF newlines; on platforms that
// use LF-only newlines the produced header should not contain any CR bytes.
//
// RUN: not grep -U $'\r' %t.header.h

float4 main() : SV_Target { return 0; }

// CHECK: #if 0
// CHECK: #endif
// CHECK: const unsigned char g_main[] = {
// CHECK: };
