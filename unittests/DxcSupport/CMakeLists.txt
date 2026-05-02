set(LLVM_LINK_COMPONENTS
  Support
  dxcSupport
  )

add_clang_unittest(DxcSupportTests
  UnicodeTest.cpp
  WinAdapterTest.cpp
  )
