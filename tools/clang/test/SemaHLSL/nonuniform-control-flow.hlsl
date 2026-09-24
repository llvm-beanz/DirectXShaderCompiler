// RUN: %dxc -Tlib_6_3 -verify %s

// Tests for the HLSL control-flow uniformity analysis, which warns when an
// operation that requires uniform control flow across the thread group (such
// as GroupMemoryBarrierWithGroupSync) is reachable from a branch whose
// condition may be non-uniform across the invocations of the thread group.

RWStructuredBuffer<float> Buf;
groupshared float SharedBuf[64];

cbuffer Constants {
  uint UniformFlag;
};

// True positive: branching on SV_DispatchThreadID is classically non-uniform.
[shader("compute")]
[numthreads(64, 1, 1)]
void main_dispatch_id(uint3 dtid : SV_DispatchThreadID) {
  if (dtid.x < 32) { // expected-note {{control flow depends on this condition, which may evaluate differently across the invocations of the thread group}}
    GroupMemoryBarrierWithGroupSync(); // expected-warning {{call to GroupMemoryBarrierWithGroupSync, which requires uniform control flow across the thread group, occurs in a branch whose condition may be non-uniform}}
  }
}

// True positive: SV_GroupIndex is also a non-uniform source.
[shader("compute")]
[numthreads(64, 1, 1)]
void main_group_index(uint gi : SV_GroupIndex) {
  if (gi == 0) { // expected-note {{control flow depends on this condition, which may evaluate differently across the invocations of the thread group}}
    GroupMemoryBarrierWithGroupSync(); // expected-warning {{call to GroupMemoryBarrierWithGroupSync, which requires uniform control flow across the thread group, occurs in a branch whose condition may be non-uniform}}
  }
}

// True positive: WaveGetLaneIndex() is always non-uniform.
[shader("compute")]
[numthreads(64, 1, 1)]
void main_lane_index() {
  if (WaveGetLaneIndex() == 0) { // expected-note {{control flow depends on this condition, which may evaluate differently across the invocations of the thread group}}
    GroupMemoryBarrierWithGroupSync(); // expected-warning {{call to GroupMemoryBarrierWithGroupSync, which requires uniform control flow across the thread group, occurs in a branch whose condition may be non-uniform}}
  }
}

// True negative: branching on a value read from a cbuffer is uniform across
// the thread group, so no diagnostic should be produced.
[shader("compute")]
[numthreads(64, 1, 1)]
void main_uniform_branch(uint3 dtid : SV_DispatchThreadID) {
  if (UniformFlag != 0) {
    GroupMemoryBarrierWithGroupSync();
  }
  SharedBuf[dtid.x % 64] = Buf[dtid.x];
}

// True negative: no branch at all guards the barrier.
[shader("compute")]
[numthreads(64, 1, 1)]
void main_no_branch(uint3 dtid : SV_DispatchThreadID) {
  SharedBuf[dtid.x % 64] = Buf[dtid.x];
  GroupMemoryBarrierWithGroupSync();
}

// True negative: "uniformizing" wave reduction ops always produce a value
// that is uniform across the wave, so using one as a branch condition that
// guards a barrier should not warn.
[shader("compute")]
[numthreads(64, 1, 1)]
void main_wave_uniform(uint3 dtid : SV_DispatchThreadID) {
  if (WaveActiveAllTrue(dtid.x < 32)) {
    GroupMemoryBarrierWithGroupSync();
  }
}

// Nested branches: only the innermost enclosing divergent branch should be
// reported for a given call site.
[shader("compute")]
[numthreads(64, 1, 1)]
void main_nested(uint3 dtid : SV_DispatchThreadID) {
  if (UniformFlag != 0) {
    if (dtid.x < 16) { // expected-note {{control flow depends on this condition, which may evaluate differently across the invocations of the thread group}}
      GroupMemoryBarrierWithGroupSync(); // expected-warning {{call to GroupMemoryBarrierWithGroupSync, which requires uniform control flow across the thread group, occurs in a branch whose condition may be non-uniform}}
    }
  }
}

// Loop-based divergence: a barrier reachable only through a loop whose
// trip count is non-uniform should also be flagged.
[shader("compute")]
[numthreads(64, 1, 1)]
void main_loop(uint3 dtid : SV_DispatchThreadID) {
  for (uint i = 0; i < dtid.x; i++) { // expected-note {{control flow depends on this condition, which may evaluate differently across the invocations of the thread group}}
    GroupMemoryBarrierWithGroupSync(); // expected-warning {{call to GroupMemoryBarrierWithGroupSync, which requires uniform control flow across the thread group, occurs in a branch whose condition may be non-uniform}}
  }
}
