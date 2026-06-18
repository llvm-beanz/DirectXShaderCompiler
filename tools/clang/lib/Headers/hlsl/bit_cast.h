// Header providing hlsl::bit_cast<>, a function template wrapper around the
// __builtin_bit_cast primitive. The source and destination types must have
// the same size and must not be intangible (resource, node record, etc.).

#ifndef _HLSL_BIT_CAST_H_
#define _HLSL_BIT_CAST_H_

namespace hlsl {

template <typename To, typename From>
To bit_cast(From value) {
  return __builtin_bit_cast(To, value);
}

} // namespace hlsl

#endif // _HLSL_BIT_CAST_H_
