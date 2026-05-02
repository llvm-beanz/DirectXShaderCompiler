// dxc /Tvs_6_0 -spirv longlong.hlsl
uint main() : A {
   return vk::ReadClock(vk::SubgroupScope);
}
