// RUN: %dxc -T ps_6_0 -E main -P -Fi %t %s && cat %t | FileCheck %s

// Verify that the bundled HLSL header tools/clang/lib/Headers/hlsl/enable_if.h
// is found via the angled-#include "embedded headers" path even when no -I
// search path points at the hlsl/ directory.

#include <enable_if.h>
// COPILOT_TODO: This test's execution and check can be simplified by using the
// -M flag to just check the include paths that the preprocessor searches,
// rather than having to preprocess the file and check the output for evidence
// of the included file being found. It is also probably worth adding a -verify
// RUN line to check that no errors are produced.

float4 main() : SV_Target { return 0; }

// CHECK: <built-in:hlsl>/enable_if.h
// CHECK: float4 main() : SV_Target
