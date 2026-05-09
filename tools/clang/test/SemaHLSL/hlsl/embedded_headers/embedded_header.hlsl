// RUN: %dxc -T ps_6_0 -E main -P -Fi %t %s && cat %t | FileCheck %s

// Verify that the bundled HLSL header tools/clang/lib/Headers/hlsl/enable_if.h
// is found via the angled-#include "embedded headers" path even when no -I
// search path points at the hlsl/ directory.

#include <enable_if.h>

float4 main() : SV_Target { return 0; }

// CHECK: <built-in:hlsl>/enable_if.h
// CHECK: float4 main() : SV_Target
