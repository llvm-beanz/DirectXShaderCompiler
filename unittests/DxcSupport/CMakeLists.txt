set(LLVM_LINK_COMPONENTS
  Support
  dxcSupport
  )

add_clang_unittest(DxcSupportTests
  ShaderPrintfTest.cpp
  WinAdapterTest.cpp
  )
