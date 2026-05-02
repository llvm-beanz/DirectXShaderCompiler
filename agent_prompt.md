# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LVLM Coding Standards](docs/CodingStandards.rst).

Verify your changes by building and testing using the
cmake/caches/PredefinedParams.cmake cache file with CMake's -C option and
building the check-all target.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository and commit it in its own commit when you're done.

# Request

When I build this locally I'm still seeing test failures:

********************
Failing Tests (20):
    Clang :: CodeGenSPIRV/coopmatrix_muladd_test.hlsl
    Clang :: CodeGenSPIRV/rayquery_init_expr.hlsl
    Clang :: HLSLFileCheckLit/hlsl/operators/swizzle/swizzleBitfieldNotAllowed.hlsl
    Clang :: LitDXILValidation/GroupShared/groupshared_shadermodels.hlsl
    Clang-Unit :: HLSL/ClangHLSLTests/CompilerTest.BatchDxil
    Clang-Unit :: HLSL/ClangHLSLTests/CompilerTest.BatchHLSL
    Clang-Unit :: HLSL/ClangHLSLTests/CompilerTest.BatchSamples
    Clang-Unit :: HLSL/ClangHLSLTests/CompilerTest.BatchShaderTargets
    Clang-Unit :: HLSL/ClangHLSLTests/PixTest.DebugInstrumentation_VectorAllocaWrite_Structs
    Clang-Unit :: HLSL/ClangHLSLTests/ValidationTest.AtomicsInvalidDests
    Clang-Unit :: HLSL/ClangHLSLTests/ValidationTest.CallableParamIsStruct
    Clang-Unit :: HLSL/ClangHLSLTests/ValidationTest.RayAttrIsStruct
    Clang-Unit :: HLSL/ClangHLSLTests/ValidationTest.RayPayloadIsStruct
    Clang-Unit :: HLSL/ClangHLSLTests/ValidationTest.RayShaderExtraArg
    Clang-Unit :: HLSL/ClangHLSLTests/ValidationTest.RayShaderWithSignaturesFail
    Clang-Unit :: HLSL/ClangHLSLTests/ValidationTest.ShaderFunctionReturnTypeVoid
    Clang-Unit :: HLSL/ClangHLSLTests/ValidationTest.WhenMissingPayloadThenFail
    Clang-Unit :: HLSL/ClangHLSLTests/ValidationTest.WhenPayloadSizeTooSmallThenFail
    Clang-Unit :: HLSL/ClangHLSLTests/VerifierTest.RunCppErrors
    Clang-Unit :: HLSL/ClangHLSLTests/VerifierTest.RunCppErrorsHV2015

  Expected Passes    : 4584
  Expected Failures  : 9
  Unsupported Tests  : 32
  Unexpected Failures: 20

Please address all test failures on this branch even any that may have been
pre-existing.

See the test_output.txt file for the full logs of my local test run.
