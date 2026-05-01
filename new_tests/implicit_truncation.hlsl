struct Color {
  uint16_t r;
  uint16_t g;
  uint16_t b;
};

RWStructuredBuffer<uint> buf : r0;

[numthreads(4, 8, 16)]
void main() {
  Color s;
  s.r = 4;
  s.g = 5;
  s.b = 6;
  uint64_t value = (uint)s;
}
