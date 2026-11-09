// RUN: not %dxc -T lib_6_3 -HV 202x %s 2>&1 | FileCheck %s
// RUN: not %dxc -T lib_6_8 -HV 202x -Od %s 2>&1 | FileCheck %s

// CHECK: error: "dx::printf is not yet supported in library shaders"

#include <dx/printf.h>
