// RUN: %dxc -T lib_6_9 -verify -HV 202x %s
// RUN: %dxc -T lib_6_9 -verify -HV 2021 %s

int A = 1;

cbuffer CB {
  int B = 2;
}

tbuffer TB {
  int C = 3;
}

static int D = 4;

namespace NS {

int A = 1;

cbuffer CB1 {
  int B = 2;
}

tbuffer TB2 {
  int C = 3;
}
static int D = 4;

} // namespace NS
