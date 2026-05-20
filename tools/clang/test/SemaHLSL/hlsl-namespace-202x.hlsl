// RUN: %dxc -T lib_6_6 -HV 202x -ast-dump-implicit %s | FileCheck %s

// Verify that under HLSL 202x the built-in 'hlsl' namespace exists and that
// intrinsic functions used by the program are placed inside it.  An implicit
// 'using namespace hlsl;' directive should also be added at translation-unit
// scope so that unqualified references still resolve.

[shader("compute")]
[numthreads(1,1,1)]
void main() {
  float x;
  float3 v;
  float a = sin(x);
  float b = cos(x);
  float c = dot(v, v);
}

// Translation unit should contain an implicit 'hlsl' namespace.
// CHECK: TranslationUnitDecl
// CHECK: NamespaceDecl {{.*}} implicit hlsl

// The intrinsic functions actually used by the shader should be parented to
// the 'hlsl' namespace.  Match the FunctionDecl entries that appear after the
// NamespaceDecl line above.
// CHECK: FunctionDecl {{.*}} implicit used sin 'float (float)'
// CHECK: FunctionDecl {{.*}} implicit used cos 'float (float)'
// CHECK: FunctionDecl {{.*}} implicit used dot

// The translation unit should also contain an implicit using-directive
// nominating the 'hlsl' namespace, so unqualified intrinsic names resolve.
// CHECK: UsingDirectiveDecl {{.*}} implicit Namespace {{.*}} 'hlsl'
