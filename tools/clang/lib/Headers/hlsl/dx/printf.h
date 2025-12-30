namespace dx {
template <int, typename...> struct tuple_impl;

template <typename HEAD> struct tuple_impl<0, HEAD> {
  HEAD head;
};

template <int N, typename HEAD, typename... TAIL>
struct tuple_impl<N, HEAD, TAIL...> {
  HEAD head;
  tuple_impl<N - 1, TAIL...> tail;
};

template <typename... T> struct tuple {
  tuple_impl<sizeof...(T) - 1, T...> values;
};

RWByteAddressBuffer DebugOutput : register(u0, space9);
groupshared uint OutputOffset = 0;

struct MessagePrefix {
  uint LaneCount;
  uint Size;
};

template <typename... T> void printf(string Str, T... Args) {
  using ArgTuple_t = tuple<uint, uint, T...>;
  ArgTuple_t ArgStruct = {WaveGetLaneIndex(), __builtin_hlsl_string_to_offset(Str), Args...};
  uint WaveOffset = WavePrefixSum(1) - 1;
  uint WaveCount = WaveActiveSum(1);
  uint ThreadOffset =
      OutputOffset + sizeof(MessagePrefix) + (WaveOffset * sizeof(ArgTuple_t));
  if (WaveIsFirstLane()) {
    MessagePrefix Prefix = {WaveCount, sizeof(ArgTuple_t)};
    DebugOutput.Store<MessagePrefix>(OutputOffset, Prefix);
    OutputOffset += sizeof(MessagePrefix) + (WaveCount * sizeof(ArgTuple_t));
  }
  DebugOutput.Store<ArgTuple_t>(ThreadOffset, ArgStruct);
}

} // namespace hlsl
