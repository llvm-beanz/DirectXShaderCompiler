// RUN: %dxc -T ps_6_0 -E main -P -Fi %t %s && cat %t | FileCheck %s

// Verify that nested embedded HLSL headers (those under subdirectories of
// tools/clang/lib/Headers/hlsl) are also resolvable through the angled-
// #include path without any -I argument pointing at the hlsl/ directory.

#include <dx/linalg.h>
// COPILOT_TODO: This test's execution and check can be simplified by using the
// -M flag to just check the include paths that the preprocessor searches,
// rather than having to preprocess the file and check the output for evidence
// of the included file being found. It is also probably worth adding a -verify
// RUN line to check that no errors are produced.

float4 main() : SV_Target { return 0; }

// CHECK: <built-in:hlsl>/dx/linalg.h
// CHECK: float4 main() : SV_Target
