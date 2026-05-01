// RUN: %dxc -E main -T cs_6_10 %s | FileCheck %s

// Test arithmetic operators and sizeof for int8_t and uint8_t.

RWStructuredBuffer<int8_t> ibuf : register(u0);
RWStructuredBuffer<uint8_t> ubuf : register(u1);

[numthreads(1, 1, 1)]
void main() {
  int8_t a = ibuf[0];
  int8_t b = ibuf[1];

  // CHECK: add{{.*}} i8
  int8_t add_result = a + b;
  ibuf[2] = add_result;

  // CHECK: sub{{.*}} i8
  int8_t sub_result = a - b;
  ibuf[3] = sub_result;

  // CHECK: mul{{.*}} i8
  int8_t mul_result = a * b;
  ibuf[4] = mul_result;

  // CHECK: sdiv i8
  int8_t div_result = a / b;
  ibuf[5] = div_result;

  // CHECK: srem i8
  int8_t mod_result = a % b;
  ibuf[6] = mod_result;

  // CHECK: and i{{8|32}}
  int8_t and_result = a & b;
  ibuf[7] = and_result;

  // CHECK: or i{{8|32}}
  int8_t or_result = a | b;
  ibuf[8] = or_result;

  // CHECK: xor i{{8|32}}
  int8_t xor_result = a ^ b;
  ibuf[9] = xor_result;

  // CHECK: sub{{.*}} i8 0,
  int8_t neg_result = -a;
  ibuf[10] = neg_result;

  // CHECK: xor i8 {{.*}}, -1
  int8_t not_result = ~a;
  ibuf[11] = not_result;

  uint8_t ua = ubuf[0];
  uint8_t ub = ubuf[1];

  // CHECK: udiv i8
  uint8_t udiv_result = ua / ub;
  ubuf[2] = udiv_result;

  // CHECK: urem i8
  uint8_t umod_result = ua % ub;
  ubuf[3] = umod_result;

  // sizeof(int8_t) == 1; optimizer folds this to constant 1.
  // CHECK: rawBufferStore.i8{{.*}}, i8 1,
  int sz = sizeof(int8_t);
  ibuf[12] = (int8_t)(sz == 1 ? 1 : 0);
}

// COPILOT-TODO: wee should also test shift operators (<<, >>) and comparisons
// (==, !=, <, >, <=, >=) for int8_t and uint8_t. We should also test operators
// on vector types like int8_t2, uint8_t4, etc. to ensure they generate the
// expected code.
