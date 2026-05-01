// RUN: %dxc -E main -T cs_6_10 %s | FileCheck %s

// Test arithmetic, bitwise, shift, comparison operators and sizeof for
// int8_t and uint8_t scalar and vector types.

RWStructuredBuffer<int8_t> ibuf : register(u0);
RWStructuredBuffer<uint8_t> ubuf : register(u1);
RWStructuredBuffer<int8_t4> iv4buf : register(u2);

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

  // Shift operators: shl/ashr for signed, lshr for unsigned.
  // CHECK: shl i8
  int8_t shl_result = a << 2;
  ibuf[13] = shl_result;

  // CHECK: ashr i8
  int8_t ashr_result = a >> 1;
  ibuf[14] = ashr_result;

  // CHECK: lshr i8
  uint8_t lshr_result = ua >> 1;
  ubuf[4] = lshr_result;

  // Comparison operators for signed int8_t.
  // CHECK: icmp eq i8
  bool eq_result = (a == b);
  // CHECK: icmp ne i8
  bool ne_result = (a != b);
  // CHECK: icmp slt i8
  bool lt_result = (a < b);
  // CHECK: icmp sgt i8
  bool gt_result = (a > b);
  // CHECK: icmp sle i8
  bool le_result = (a <= b);
  // CHECK: icmp sge i8
  bool ge_result = (a >= b);
  ibuf[15] = (int8_t)(eq_result ? 1 : 0);
  ibuf[16] = (int8_t)(ne_result ? 1 : 0);
  ibuf[17] = (int8_t)(lt_result ? 1 : 0);
  ibuf[18] = (int8_t)(gt_result ? 1 : 0);
  ibuf[19] = (int8_t)(le_result ? 1 : 0);
  ibuf[20] = (int8_t)(ge_result ? 1 : 0);

  // Comparison operators for unsigned uint8_t.
  // CHECK: icmp ult i8
  bool ult_result = (ua < ub);
  // CHECK: icmp ugt i8
  bool ugt_result = (ua > ub);
  ubuf[5] = (uint8_t)(ult_result ? 1 : 0);
  ubuf[6] = (uint8_t)(ugt_result ? 1 : 0);

  // Vector arithmetic: int8_t4 operations use widened <4 x i32>.
  int8_t4 va = iv4buf[0];
  int8_t4 vb = iv4buf[1];

  // CHECK: add <4 x i32>
  int8_t4 vadd = va + vb;
  iv4buf[2] = vadd;

  // CHECK: sub <4 x i32>
  int8_t4 vsub = va - vb;
  iv4buf[3] = vsub;

  // CHECK: mul <4 x i32>
  int8_t4 vmul = va * vb;
  iv4buf[4] = vmul;

  // Vector comparisons operate on i8 element type.
  // CHECK: icmp eq <4 x i8>
  bool4 veq = (va == vb);
  // CHECK: icmp slt <4 x i8>
  bool4 vlt = (va < vb);
  iv4buf[5] = (int8_t4)veq;
  iv4buf[6] = (int8_t4)vlt;
}
