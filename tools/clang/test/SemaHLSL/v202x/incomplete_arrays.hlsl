// RUN: %dxc -T cs_6_0 -HV 202x -verify %s

void fn(int Arr[]) {} // expected-error{{function parameters of incomplete array type are incompatible with HLSL 202x and later}}

void fn2(RWBuffer<float> Arr[]) {} // expected-error{{function parameters of incomplete array type are incompatible with HLSL 202x and later}}

RWBuffer<float> Arry[];
int GlobalArr[]; // expected-error{{definition of variable with array type needs an explicit size or an initializer}}
int GlobalZeroArr[0]; // expected-error{{zero-length arrays are not permitted in HLSL}}

[numthreads(1,1,1)]
void main() {
  int Arr[] = {1,2,3};
  fn(Arr);
  int ZeroArr[0]; // expected-error{{zero-length arrays are not permitted in HLSL}}
  fn(ZeroArr);
  fn2(Arry);
}
