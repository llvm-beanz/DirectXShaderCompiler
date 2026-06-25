// RUN: %dxc -T lib_6_6 -HV 202x -verify %s

// HLSL 202x relaxes the global-operator restriction for the operators that
// HLSL permits to be overloaded, but the operators that are not overloadable
// in HLSL (assignment, compound assignment, ++, --, ->, ->*, new and delete)
// must continue to be rejected even at the namespace scope.

struct S { int v; };

// expected-error@+1 {{overloading 'operator=' is not allowed}}
S operator=(S a, S b);

// expected-error@+1 {{overloading 'operator+=' is not allowed}}
S operator+=(S a, S b);

// expected-error@+1 {{overloading 'operator-=' is not allowed}}
S operator-=(S a, S b);

// expected-error@+1 {{overloading 'operator*=' is not allowed}}
S operator*=(S a, S b);

// expected-error@+1 {{overloading 'operator++' is not allowed}}
S operator++(S a);

// expected-error@+1 {{overloading 'operator--' is not allowed}}
S operator--(S a);

// expected-error@+1 {{overloading 'operator->' is not allowed}}
S operator->(S a);

// expected-error@+1 {{overloading 'operator->*' is not allowed}}
S operator->*(S a, S b);
