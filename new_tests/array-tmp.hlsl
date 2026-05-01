void fn(float x[2]) { }

float main(float val: A) : B {
  float Arr[2] = {0, 0};
  fn(Arr);
  return Arr[0];
}
