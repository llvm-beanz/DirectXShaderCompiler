cbuffer Uniforms : register(b0) {
  float4x4 modelViewProjection;
  uint4 mipOffset[16];
  float bwdMinLOD;
};

Texture2D<float4> texRef : register(t0);
Texture2D<float4> bwdTextureResource : register(t1);
SamplerState textureSampler : register(s0);
RWStructuredBuffer<int> bwdAccumulateBuffer : register(u0);

struct DifferentiableTexture {
  RWStructuredBuffer<int> accumulateBuffer;
  Texture2D<float4> texture;
  float minLOD;

  [[dxc::backward_derivative(bwd_LoadTexel)]]
  float4 LoadTexel([[dxc::no_diff]] int3 location,
                   [[dxc::no_diff]] int2 offset,
                   [[dxc::no_diff]] uint dLayerW,
                   [[dxc::no_diff]] uint dMipOffset) {
    return texture.Load(location, offset);
  }

  void bwd_LoadTexel([[dxc::no_diff]] int3 location,
                     [[dxc::no_diff]] int2 offset,
                     [[dxc::no_diff]] uint dLayerW,
                     [[dxc::no_diff]] uint dMipOffset, float4 dResult) {
    int4 fixedPointGradient = int4(int3(dResult.xyz * 65536.0f), 1);
    uint base = dMipOffset +
                ((uint)location.y * dLayerW + (uint)location.x) * 4;
    InterlockedAdd(accumulateBuffer[base + 0], fixedPointGradient.x);
    InterlockedAdd(accumulateBuffer[base + 1], fixedPointGradient.y);
    InterlockedAdd(accumulateBuffer[base + 2], fixedPointGradient.z);
    InterlockedAdd(accumulateBuffer[base + 3], fixedPointGradient.w);
  }

  [[dxc::autodiff(bwd)]]
  float4 sampleTexture_linear([[dxc::no_diff]] uint lod, float2 uv,
                              [[dxc::no_diff]] uint width,
                              [[dxc::no_diff]] uint height) {
    width >>= lod;
    height >>= lod;

    uv = uv - [[dxc::no_diff]] floor(uv);
    float2 location = uv * float2(width, height) - float2(0.5f, 0.5f);
    float x0 = [[dxc::no_diff]] floor(location.x);
    float y0 = [[dxc::no_diff]] floor(location.y);
    float fracX = location.x - x0;
    float fracY = location.y - y0;
    float x1 = x0 + 1.0f;
    float y1 = y0 + 1.0f;

    if (x0 < 0.0f)
      x0 += width;
    if (y0 < 0.0f)
      y0 += height;
    if (x1 >= width)
      x1 -= width;
    if (y1 >= height)
      y1 -= height;

    float weight0 = 1.0f - fracY;
    float weight1 = fracY;
    float weight00 = weight0 * (1.0f - fracX);
    float weight01 = weight0 * fracX;
    float weight10 = weight1 * (1.0f - fracX);
    float weight11 = weight1 * fracX;

    uint dLayerW = width;
    uint offset = mipOffset[lod / 4][lod % 4];
    int lodAsInt = (int)lod;
    return LoadTexel(int3((int)x0, (int)y0, lodAsInt), int2(0, 0),
                     dLayerW, offset) *
               weight00 +
           LoadTexel(int3((int)x1, (int)y0, lodAsInt), int2(0, 0),
                     dLayerW, offset) *
               weight01 +
           LoadTexel(int3((int)x0, (int)y1, lodAsInt), int2(0, 0),
                     dLayerW, offset) *
               weight10 +
           LoadTexel(int3((int)x1, (int)y1, lodAsInt), int2(0, 0),
                     dLayerW, offset) *
               weight11;
  }

  [[dxc::autodiff(bwd)]]
  float4 sampleTexture_trilinear([[dxc::no_diff]] uint width,
                                 [[dxc::no_diff]] uint height,
                                 [[dxc::no_diff]] uint levels, float2 uv,
                                 [[dxc::no_diff]] float2 dX,
                                 [[dxc::no_diff]] float2 dY) {
    dX *= float2(width, height);
    dY *= float2(width, height);

    float lengthX = length(dX);
    float lengthY = length(dY);
    float lod = log2(max(lengthX, lengthY));
    float maxLOD = (float)(levels - 1);
    float clampedLOD = max(minLOD, min(maxLOD, lod));

    float lodFrac = clampedLOD - [[dxc::no_diff]] floor(clampedLOD);
    uint lod0 = (uint)floor(clampedLOD);
    uint lod1 = min(levels - 1, lod0 + 1);
    float weightLod0 = 1.0f - lodFrac;
    float weightLod1 = lodFrac;

    float4 value0 = sampleTexture_linear(lod0, uv, width, height) *
                    weightLod0;
    float4 value1 = sampleTexture_linear(lod1, uv, width, height) *
                    weightLod1;
    return value0 + value1;
  }

  static float4 hardwareSample(
      DifferentiableTexture value,
      [[dxc::no_diff]] SamplerState sampleState, float2 uv,
      [[dxc::no_diff]] float2 dX, [[dxc::no_diff]] float2 dY) {
    return value.texture.Sample(sampleState, uv);
  }

  [[dxc::primal_substitute_of(hardwareSample)]]
  [[dxc::autodiff(bwd)]]
  static float4 sample_reference_impl(
      DifferentiableTexture value,
      [[dxc::no_diff]] SamplerState sampleState, float2 uv,
      [[dxc::no_diff]] float2 dX, [[dxc::no_diff]] float2 dY) {
    uint width;
    uint height;
    uint levels;
    [[dxc::no_diff]]
    value.texture.GetDimensions(0, width, height, levels);
    return value.sampleTexture_trilinear(width, height, levels, uv, dX, dY);
  }
};

DifferentiableTexture getBackwardTexture() {
  DifferentiableTexture result;
  result.accumulateBuffer = bwdAccumulateBuffer;
  result.texture = bwdTextureResource;
  result.minLOD = bwdMinLOD;
  return result;
}

[[dxc::autodiff(bwd)]]
float4 shadeFragment(float2 uv) {
  uv *= 2.0f;

  float2 dX = [[dxc::no_diff]] ddx_coarse(uv);
  float2 dY = [[dxc::no_diff]] ddy_coarse(uv);
  DifferentiableTexture texture = [[dxc::no_diff]] getBackwardTexture();
  float3 color = DifferentiableTexture::hardwareSample(
                     texture, textureSampler, uv, dX, dY)
                     .xyz;
  return float4(color, 1.0f);
}

[[dxc::autodiff(bwd)]]
float3 loss([[dxc::no_diff]] float2 uv,
            [[dxc::no_diff]] float4 screenPosition) {
  float3 referenceColor =
      ([[dxc::no_diff]] texRef.Load(
          int3((int2)screenPosition.xy, 0)))
          .xyz;
  float3 residual = shadeFragment(uv).xyz - referenceColor;
  residual *= residual;
  return residual;
}

float4 fragmentMain(float2 uv : UV) : SV_Target {
  return shadeFragment(uv);
}
