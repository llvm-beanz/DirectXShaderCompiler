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


RWByteAddressBuffer DebugOutput : register(u0, space9);
groupshared uint OutputOffset;

struct MessagePrefix {
  uint LaneCount;
  uint StrOffset;
  uint Size;
};

void InitializeDebugStream() {
  if (WaveIsFirstLane()) {
    OutputOffset = 0;
    DebugOffset = 0;
  }
  GroupMemoryBarrierWithGroupSync();
}

template <typename... T> void printf(string Str, T... Args) {
  uint WaveIndex = WavePrefixSum(1);
  uint WaveCount = WaveActiveSum(1);
  uint StrOffset = __builtin_hlsl_string_to_offset(Str);
  uint ArgSize = NumDwords<T...>() * 4;
  uint ThreadOffset =
      OutputOffset + sizeof(MessagePrefix) + (WaveIndex * ArgSize);
  uint StartOffset = OutputOffset;
  if (WaveIsFirstLane()) {
    MessagePrefix Prefix = {WaveCount, StrOffset, ArgSize};
    DebugOutput.Store<MessagePrefix>(OutputOffset, Prefix);
    OutputOffset += sizeof(MessagePrefix) + (WaveCount * ArgSize);
    GroupMemoryBarrierWithGroupSync();
  }
  StoreArgs(DebugOutput, ThreadOffset, Args...);
}

} // namespace dx
