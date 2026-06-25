// RUN: %dxc -T ps_6_6 -E main -HV 202x %s | FileCheck %s

// Verify that const local resource objects can still be used through their
// instance methods (which are now properly marked const), while still
// allowing normal reads of resource data.

Texture2D<float4>      tex   : register(t0);
SamplerState           samp  : register(s0);
RWBuffer<float4>       buf   : register(u0);
ByteAddressBuffer      bab   : register(t1);
StructuredBuffer<int>  sb    : register(t2);

// CHECK: define void @main()
float4 main(float2 uv : TEXCOORD) : SV_Target {
  // Const local handles. They cannot be reassigned, but their methods
  // (which don't mutate the handle) must still be callable.
  const Texture2D<float4>     ltex = tex;
  const SamplerState          lsamp = samp;
  const RWBuffer<float4>      lbuf = buf;
  const ByteAddressBuffer     lbab = bab;
  const StructuredBuffer<int> lsb = sb;

  float4 sampled = ltex.Sample(lsamp, uv);
  float4 loaded  = ltex.Load(int3(0, 0, 0));
  float4 fromBuf = lbuf.Load(0);
  uint   raw     = lbab.Load(0);
  int    si      = lsb.Load(0);

  uint w, h, l;
  ltex.GetDimensions(0, w, h, l);

  return sampled + loaded + fromBuf + float4(raw, si, 0, 0);
}
