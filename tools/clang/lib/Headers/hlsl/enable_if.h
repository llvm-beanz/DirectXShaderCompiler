// Header for enable_if APIs.

#ifndef HLSL_ENABLE_IF_H
#define HLSL_ENABLE_IF_H

#if __HLSL_VERSION >= 2021

namespace hlsl {

template <bool B, typename T> struct enable_if {};

template <typename T> struct enable_if<true, T> {
  using type = T;
};

} // namespace hlsl

#endif

#endif // HLSL_ENABLE_IF_H
