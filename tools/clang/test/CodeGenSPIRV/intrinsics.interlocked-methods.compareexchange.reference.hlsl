// RUN: not %dxc -T cs_6_6 -E main -fcgl -spirv %s 2>&1 | FileCheck %s

groupshared uint value;

uint getValue() {
  return 0;
}

uint2 getVector() {
  uint2 output;
  return output;
}

int getArray()[2] {
  int array[2];
  return array;
}

[numthreads(1, 1, 1)]
void main() {
  InterlockedCompareExchange(value, 1, 2, 3);
// CHECK: error: cannot bind non-lvalue argument 3 to out param{{emter|eter}}

  InterlockedAdd(value, 1, getValue());
// CHECK: error: cannot bind non-lvalue argument getValue() to out param{{emter|eter}}

  InterlockedAdd(value, 1, getVector().x);
// CHECK: error: cannot bind non-lvalue argument getVector().x to out param{{emter|eter}}

  InterlockedAdd(value, 1, getArray()[0]);
}
