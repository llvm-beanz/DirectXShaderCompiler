// RUN: not %dxc -T ps_6_0 -E main %s 2>&1 | FileCheck %s

// Quoted #includes should still go through the normal filesystem search
// path; the embedded-headers mechanism only kicks in for angle-bracket
// includes.  Without a -I pointing at the hlsl/ directory, this quoted
// include must fail.

#include "enable_if.h"

float4 main() : SV_Target { return 0; }

// CHECK: 'enable_if.h' file not found
