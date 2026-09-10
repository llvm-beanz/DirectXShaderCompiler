// RUN: %dxc -T lib_6_9 -HV 2021 -verify %s

[[dxc::no_diff]] float function_subject(float x) { // expected-error {{'no_diff' attribute only applies to parameters}}
  return x;
}

struct [[dxc::no_diff]] RecordSubject { // expected-error {{'no_diff' attribute only applies to parameters}}
  float value;
};

[[dxc::no_diff]] float global_subject; // expected-error {{'no_diff' attribute only applies to parameters}}
