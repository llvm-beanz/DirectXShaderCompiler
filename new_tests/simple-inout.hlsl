void fn(inout float x, inout int y) {
  y = 2;
  x = 1;
}

float main(float val: A) : B {
  fn(val, val);
  return val;
}
