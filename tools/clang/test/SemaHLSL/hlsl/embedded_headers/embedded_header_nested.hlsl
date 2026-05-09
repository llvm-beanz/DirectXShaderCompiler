// RUN: %dxc -T ps_6_0 -E main -P -Fi %t %s && cat %t | FileCheck %s

// Verify that nested embedded HLSL headers (those under subdirectories of
// tools/clang/lib/Headers/hlsl) are also resolvable through the angled-
// #include path without any -I argument pointing at the hlsl/ directory.

#include <dx/linalg.h>

float4 main() : SV_Target { return 0; }

// CHECK: <built-in:hlsl>/dx/linalg.h
// CHECK: float4 main() : SV_Target
