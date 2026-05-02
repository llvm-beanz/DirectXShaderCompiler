struct Agg {
  float3 f3;
};

void get(out Agg agg);

static Agg s_agg;

export
float3 main() {
  get(s_agg);
  return s_agg.f3;
}

