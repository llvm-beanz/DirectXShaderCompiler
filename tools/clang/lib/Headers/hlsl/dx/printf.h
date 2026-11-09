//===----------------------------------------------------------------------===//
//
// Part of the DirectXShaderCompiler, under the Apache License v2.0 with LLVM
// Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _HLSL_DX_PRINTF_H_
#define _HLSL_DX_PRINTF_H_

#if defined(__spirv__)
#error "dx::printf is only supported for DirectX targets"
#elif __SHADER_TARGET_STAGE == __SHADER_STAGE_LIBRARY
#error "dx::printf is not yet supported in library shaders"
#endif

namespace dx {
namespace _detail  {
template <typename... T>
struct size_impl;

template <>
struct size_impl<> {
  static uint size() {
    return 0;
  }

}; 
template <typename First, typename... Rest>
struct size_impl<First, Rest...> {

  static uint size() {
    return (sizeof(First) + 3) / 4 + size_impl<Rest...>::size();
  }
};

template <typename ... T>
struct store_impl;

template<>
struct store_impl<> {
  static void store(RWByteAddressBuffer Buffer, uint Offset) {
  }
};

template <typename First, typename... Rest>
struct store_impl<First, Rest...> {
  static void store(RWByteAddressBuffer Buffer, uint Offset, First FirstArg, Rest... RestArgs) {
    Buffer.Store<First>(Offset, FirstArg);
    Offset += ((sizeof(First) + 3) / 4) * 4;
    store_impl<Rest...>::store(Buffer, Offset, RestArgs...);
  }
};

} // namespace _detail

template <typename ...Args>
uint NumDwords() {
  return _detail::size_impl<Args...>::size();
}

template <typename... T>
void StoreArgs(RWByteAddressBuffer Buffer, uint Offset, T... Args) {
  _detail::store_impl<T...>::store(Buffer, Offset, Args...);
}

#ifndef HLSL_DX_PRINTF_BINDING
#define HLSL_DX_PRINTF_BINDING u0, space9
#endif

RWByteAddressBuffer DebugOutput : register(HLSL_DX_PRINTF_BINDING);

struct MessagePrefix {
  uint LaneCount;
  uint Size;
};

void InitializeDebugStream() {
  if (WaveIsFirstLane()) {
    uint Unused = 0;
    DebugOutput.InterlockedCompareExchange(0, 0, 4, Unused);
  }
}

template <typename... T> void printf(string Str, T... Args) {
  uint ThreadIndex = WavePrefixSum(1);
  uint ThreadCount = WaveActiveSum(1);
  uint StrOffset = dx::__builtin_hlsl_string_to_offset(Str);
  uint ArgSize = NumDwords<T...>() * 4;
  uint LaneSize = sizeof(uint) + ArgSize;
  uint MessageSize = sizeof(MessagePrefix) + (ThreadCount * LaneSize);
  uint StartOffset = 0;
  if (WaveIsFirstLane()) {
    DebugOutput.InterlockedAdd(0, MessageSize, StartOffset);
    MessagePrefix Prefix = {ThreadCount, LaneSize};
    DebugOutput.Store<MessagePrefix>(StartOffset, Prefix);
  }
  StartOffset = WaveReadLaneFirst(StartOffset);
  uint ThreadOffset =
      StartOffset + sizeof(MessagePrefix) + (ThreadIndex * LaneSize);
  DebugOutput.Store(ThreadOffset, StrOffset);
  StoreArgs(DebugOutput, ThreadOffset + sizeof(uint), Args...);
}

} // namespace dx

#endif // _HLSL_DX_PRINTF_H_
