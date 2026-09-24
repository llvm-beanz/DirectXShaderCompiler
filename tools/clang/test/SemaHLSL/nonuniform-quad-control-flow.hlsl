// RUN: %dxc -Tlib_6_3 -verify %s

// Tests for the "quad uniformity" scope of the HLSL control-flow uniformity
// analysis: operations that only require the four invocations of a single
// 2x2 quad to execute uniformly (ddx/ddy and the explicit Quad* intrinsics),
// as opposed to operations that require uniformity across the whole thread
// group/wave (such as GroupMemoryBarrierWithGroupSync, covered by
// nonuniform-control-flow.hlsl).

cbuffer Constants {
  uint UniformFlag;
};

// True positive: branching on SV_Position is non-uniform within a quad (the
// four pixels of a quad have different screen positions), so a ddx/ddy
// reachable only through such a branch should be flagged.
float4 main_position_ddx(float4 pos : SV_Position) : SV_Target {
  if (pos.x < 100) { // expected-note {{control flow depends on this condition, which may evaluate differently across the invocations of the current quad}}
    return float4(ddx(pos.x), 0, 0, 0); // expected-warning {{call to ddx, which requires uniform control flow across the current quad, occurs in a branch whose condition may be non-uniform}}
  }
  return 0;
}

float4 main_position_ddy(float4 pos : SV_Position) : SV_Target {
  if (pos.y < 100) { // expected-note {{control flow depends on this condition, which may evaluate differently across the invocations of the current quad}}
    return float4(ddy(pos.y), 0, 0, 0); // expected-warning {{call to ddy, which requires uniform control flow across the current quad, occurs in a branch whose condition may be non-uniform}}
  }
  return 0;
}

// True negative: branching on a value read from a cbuffer is uniform, so no
// diagnostic should be produced.
float4 main_uniform_branch(float4 pos : SV_Position) : SV_Target {
  if (UniformFlag != 0) {
    return float4(ddx(pos.x), 0, 0, 0);
  }
  return 0;
}

// True negative: "uniformizing" wave reduction ops always produce a value
// that is uniform across the whole wave, and therefore also within any
// single quad, so using one as a branch condition that guards a derivative
// operation should not warn.
float4 main_wave_uniform(float4 pos : SV_Position) : SV_Target {
  if (WaveActiveAllTrue(pos.x < 100)) {
    return float4(ddx(pos.x), 0, 0, 0);
  }
  return 0;
}

// True negative: Quad* broadcast/reduction intrinsics always produce a value
// that is uniform *within* the quad (even though it may still vary from one
// quad to the next), so using one as a branch condition that guards a
// derivative operation should not warn, even though the intrinsic's operand
// is itself non-uniform.
float4 main_quad_read_across_x(float4 pos : SV_Position) : SV_Target {
  float v = QuadReadAcrossX(pos.x);
  if (v < 100) {
    return float4(ddy(pos.y), 0, 0, 0);
  }
  return 0;
}

float4 main_quad_any(float4 pos : SV_Position) : SV_Target {
  if (QuadAny(pos.x < 100)) {
    return float4(ddx(pos.x), 0, 0, 0);
  }
  return 0;
}

// True positive: a Quad* broadcast/reduction intrinsic's result is *not*
// guaranteed to be uniform across the whole thread group (only within the
// quad), so it does not suffice to guard an operation that requires
// group-wide uniformity, such as GroupMemoryBarrierWithGroupSync.
RWStructuredBuffer<float> Buf;
[shader("compute")]
[numthreads(64, 1, 1)]
void main_quad_any_barrier(uint3 dtid : SV_DispatchThreadID) {
  if (QuadAny(dtid.x < 32)) { // expected-note {{control flow depends on this condition, which may evaluate differently across the invocations of the thread group}}
    GroupMemoryBarrierWithGroupSync(); // expected-warning {{call to GroupMemoryBarrierWithGroupSync, which requires uniform control flow across the thread group, occurs in a branch whose condition may be non-uniform}}
  }
}

// True negative: no branch at all guards the derivative operation.
float4 main_no_branch(float4 pos : SV_Position) : SV_Target {
  return float4(ddx(pos.x), ddy(pos.y), 0, 0);
}
