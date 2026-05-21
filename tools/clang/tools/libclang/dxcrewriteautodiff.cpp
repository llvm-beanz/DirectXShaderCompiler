///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// dxcrewriteautodiff.cpp                                                    //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
// Implements -generate-differentials: synthesises forward and backward      //
// mode auto-differentiation variants of functions annotated with the        //
// [[dxc::autodiff(...)]] attribute.                                         //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#include "dxcrewriteautodiff.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace llvm;

namespace {

// ---------------------------------------------------------------------------
// Intrinsic classification
// ---------------------------------------------------------------------------

// Map a differentiable HLSL math intrinsic to the corresponding builder used
// by the backward-mode runtime. The forward-mode runtime overloads the
// standard intrinsic names directly on Value<T>, so no mapping is required
// there.
//
// The list mirrors the differentiable-by-design entries in
// utils/hct/gen_intrin_main.txt: trigonometric functions, exponentials,
// logarithms, roots, common smooth algebraic operations, geometric
// operations, and matrix products. Intrinsics that are piecewise constant
// (sign, step) are listed: their derivative is zero almost everywhere and
// the runtime library implements them with the subgradient convention.
StringRef GetBackwardIntrinsicBuilder(StringRef Name) {
  return StringSwitch<StringRef>(Name)
      // Trigonometry.
      .Case("sin", "sinExpr")
      .Case("cos", "cosExpr")
      .Case("tan", "tanExpr")
      .Case("asin", "asinExpr")
      .Case("acos", "acosExpr")
      .Case("atan", "atanExpr")
      .Case("atan2", "atan2Expr")
      .Case("sinh", "sinhExpr")
      .Case("cosh", "coshExpr")
      .Case("tanh", "tanhExpr")
      // Exponentials and logarithms.
      .Case("exp", "expExpr")
      .Case("exp2", "exp2Expr")
      .Case("log", "logExpr")
      .Case("log2", "log2Expr")
      .Case("log10", "log10Expr")
      .Case("pow", "power")
      // Roots, reciprocals.
      .Case("sqrt", "sqrtExpr")
      .Case("rsqrt", "rsqrtExpr")
      .Case("rcp", "rcpExpr")
      // Smooth algebraic.
      .Case("abs", "absExpr")
      .Case("saturate", "saturateExpr")
      .Case("clamp", "clampExpr")
      .Case("max", "maxExpr")
      .Case("min", "minExpr")
      .Case("lerp", "lerpExpr")
      .Case("mad", "madExpr")
      .Case("fma", "fmaExpr")
      .Case("smoothstep", "smoothstepExpr")
      // Piecewise constant (subgradient).
      .Case("sign", "signExpr")
      .Case("step", "stepExpr")
      .Case("floor", "floorExpr")
      .Case("ceil", "ceilExpr")
      .Case("round", "roundExpr")
      .Case("trunc", "truncExpr")
      .Case("frac", "fracExpr")
      .Case("fmod", "fmodExpr")
      .Case("modf", "modfExpr")
      // Conversion-style numerics.
      .Case("degrees", "degreesExpr")
      .Case("radians", "radiansExpr")
      // Geometric.
      .Case("dot", "dotExpr")
      .Case("cross", "crossExpr")
      .Case("length", "lengthExpr")
      .Case("distance", "distanceExpr")
      .Case("normalize", "normalizeExpr")
      .Case("reflect", "reflectExpr")
      .Case("refract", "refractExpr")
      .Case("faceforward", "faceforwardExpr")
      // Matrix.
      .Case("mul", "mulExpr")
      .Case("determinant", "determinantExpr")
      .Case("transpose", "transposeExpr")
      // Misc smooth.
      .Case("lit", "litExpr")
      .Case("dst", "dstExpr")
      .Case("ldexp", "ldexpExpr")
      .Default(StringRef());
}

// Intrinsics that are well-defined but mathematically not differentiable.
// Using one of them in the body of a function annotated with [[dxc::autodiff]]
// causes the generated forward / backward function to be a stub that fails
// at compile time with a _Static_assert.
//
// The list is intentionally explicit so that "unknown intrinsic" remains a
// distinct failure category (also stubbed, but with a different message).
const char *GetNonDifferentiableReason(StringRef Name) {
  return StringSwitch<const char *>(Name)
      // Predicates: bool return.
      .Case("any", "predicate 'any' is not differentiable")
      .Case("all", "predicate 'all' is not differentiable")
      .Case("isfinite", "predicate 'isfinite' is not differentiable")
      .Case("isinf", "predicate 'isinf' is not differentiable")
      .Case("isnan", "predicate 'isnan' is not differentiable")
      .Case("isnormal", "predicate 'isnormal' is not differentiable")
      // Bit-cast / reinterpret.
      .Case("asfloat", "bit-cast 'asfloat' is not differentiable")
      .Case("asfloat16", "bit-cast 'asfloat16' is not differentiable")
      .Case("asint", "bit-cast 'asint' is not differentiable")
      .Case("asint16", "bit-cast 'asint16' is not differentiable")
      .Case("asuint", "bit-cast 'asuint' is not differentiable")
      .Case("asuint16", "bit-cast 'asuint16' is not differentiable")
      .Case("asdouble", "bit-cast 'asdouble' is not differentiable")
      .Case("f16tof32", "bit-cast 'f16tof32' is not differentiable")
      .Case("f32tof16", "bit-cast 'f32tof16' is not differentiable")
      // Bit manipulation.
      .Case("countbits", "bit-op 'countbits' is not differentiable")
      .Case("firstbithigh", "bit-op 'firstbithigh' is not differentiable")
      .Case("firstbitlow", "bit-op 'firstbitlow' is not differentiable")
      .Case("reversebits", "bit-op 'reversebits' is not differentiable")
      // Integer-typed.
      .Case("D3DCOLORtoUBYTE4",
            "integer-valued 'D3DCOLORtoUBYTE4' is not differentiable")
      .Case("msad4", "integer-valued 'msad4' is not differentiable")
      .Case("dot4add_i8packed",
            "integer-valued 'dot4add_i8packed' is not differentiable")
      .Case("dot4add_u8packed",
            "integer-valued 'dot4add_u8packed' is not differentiable")
      .Case("dot2add", "'dot2add' is not differentiable")
      .Case("unpack_s8s32", "integer-valued 'unpack_s8s32' is not differentiable")
      .Case("unpack_u8u32", "integer-valued 'unpack_u8u32' is not differentiable")
      .Case("AddUint64", "'AddUint64' is not differentiable")
      // Side-effecting / control.
      .Case("clip", "side-effecting 'clip' is not differentiable")
      .Case("abort", "side-effecting 'abort' is not differentiable")
      .Case("printf", "side-effecting 'printf' is not differentiable")
      .Case("DebugBreak", "side-effecting 'DebugBreak' is not differentiable")
      .Case("source_mark", "side-effecting 'source_mark' is not differentiable")
      .Case("Barrier", "barrier 'Barrier' is not differentiable")
      .Case("AllMemoryBarrier",
            "barrier 'AllMemoryBarrier' is not differentiable")
      .Case("AllMemoryBarrierWithGroupSync",
            "barrier 'AllMemoryBarrierWithGroupSync' is not differentiable")
      .Case("DeviceMemoryBarrier",
            "barrier 'DeviceMemoryBarrier' is not differentiable")
      .Case("DeviceMemoryBarrierWithGroupSync",
            "barrier 'DeviceMemoryBarrierWithGroupSync' is not differentiable")
      .Case("GroupMemoryBarrier",
            "barrier 'GroupMemoryBarrier' is not differentiable")
      .Case("GroupMemoryBarrierWithGroupSync",
            "barrier 'GroupMemoryBarrierWithGroupSync' is not differentiable")
      // Atomics / interlocked.
      .Case("InterlockedAdd", "atomic 'InterlockedAdd' is not differentiable")
      .Case("InterlockedAnd", "atomic 'InterlockedAnd' is not differentiable")
      .Case("InterlockedOr", "atomic 'InterlockedOr' is not differentiable")
      .Case("InterlockedXor", "atomic 'InterlockedXor' is not differentiable")
      .Case("InterlockedMin", "atomic 'InterlockedMin' is not differentiable")
      .Case("InterlockedMax", "atomic 'InterlockedMax' is not differentiable")
      .Case("InterlockedExchange",
            "atomic 'InterlockedExchange' is not differentiable")
      .Case("InterlockedCompareStore",
            "atomic 'InterlockedCompareStore' is not differentiable")
      .Case("InterlockedCompareExchange",
            "atomic 'InterlockedCompareExchange' is not differentiable")
      .Case("InterlockedCompareStoreFloatBitwise",
            "atomic 'InterlockedCompareStoreFloatBitwise' "
            "is not differentiable")
      .Case("InterlockedCompareExchangeFloatBitwise",
            "atomic 'InterlockedCompareExchangeFloatBitwise' "
            "is not differentiable")
      // Derivative-of intrinsics are discontinuous quad-level operations.
      .Case("ddx", "quad-derivative 'ddx' is not differentiable")
      .Case("ddx_coarse",
            "quad-derivative 'ddx_coarse' is not differentiable")
      .Case("ddx_fine", "quad-derivative 'ddx_fine' is not differentiable")
      .Case("ddy", "quad-derivative 'ddy' is not differentiable")
      .Case("ddy_coarse",
            "quad-derivative 'ddy_coarse' is not differentiable")
      .Case("ddy_fine", "quad-derivative 'ddy_fine' is not differentiable")
      .Case("fwidth", "quad-derivative 'fwidth' is not differentiable")
      .Case("EvaluateAttributeAtSample",
            "interpolation 'EvaluateAttributeAtSample' is not differentiable")
      .Case("EvaluateAttributeCentroid",
            "interpolation 'EvaluateAttributeCentroid' is not differentiable")
      .Case("EvaluateAttributeSnapped",
            "interpolation 'EvaluateAttributeSnapped' is not differentiable")
      .Case("GetAttributeAtVertex",
            "interpolation 'GetAttributeAtVertex' is not differentiable")
      // Wave/quad ops.
      .Case("WaveActiveAllTrue",
            "wave-op 'WaveActiveAllTrue' is not differentiable")
      .Case("WaveActiveAnyTrue",
            "wave-op 'WaveActiveAnyTrue' is not differentiable")
      .Case("WaveActiveBallot",
            "wave-op 'WaveActiveBallot' is not differentiable")
      .Case("WaveActiveCountBits",
            "wave-op 'WaveActiveCountBits' is not differentiable")
      .Case("WaveGetLaneCount",
            "wave-op 'WaveGetLaneCount' is not differentiable")
      .Case("WaveGetLaneIndex",
            "wave-op 'WaveGetLaneIndex' is not differentiable")
      .Case("WaveIsFirstLane",
            "wave-op 'WaveIsFirstLane' is not differentiable")
      .Case("WaveMatch", "wave-op 'WaveMatch' is not differentiable")
      .Case("WaveMultiPrefixCountBits",
            "wave-op 'WaveMultiPrefixCountBits' is not differentiable")
      .Case("WavePrefixCountBits",
            "wave-op 'WavePrefixCountBits' is not differentiable")
      .Case("QuadAll", "wave-op 'QuadAll' is not differentiable")
      .Case("QuadAny", "wave-op 'QuadAny' is not differentiable")
      .Case("IsHelperLane", "'IsHelperLane' is not differentiable")
      // Raytracing / mesh / system-value queries.
      .Case("TraceRay", "raytracing 'TraceRay' is not differentiable")
      .Case("CallShader", "raytracing 'CallShader' is not differentiable")
      .Case("ReportHit", "raytracing 'ReportHit' is not differentiable")
      .Case("IgnoreHit", "raytracing 'IgnoreHit' is not differentiable")
      .Case("AcceptHitAndEndSearch",
            "raytracing 'AcceptHitAndEndSearch' is not differentiable")
      .Case("AllocateRayQuery",
            "raytracing 'AllocateRayQuery' is not differentiable")
      .Case("DispatchRaysIndex",
            "system-value 'DispatchRaysIndex' is not differentiable")
      .Case("DispatchRaysDimensions",
            "system-value 'DispatchRaysDimensions' is not differentiable")
      .Case("RayFlags", "system-value 'RayFlags' is not differentiable")
      .Case("RayTMin", "system-value 'RayTMin' is not differentiable")
      .Case("RayTCurrent",
            "system-value 'RayTCurrent' is not differentiable")
      .Case("HitKind", "system-value 'HitKind' is not differentiable")
      .Case("InstanceID", "system-value 'InstanceID' is not differentiable")
      .Case("InstanceIndex",
            "system-value 'InstanceIndex' is not differentiable")
      .Case("PrimitiveIndex",
            "system-value 'PrimitiveIndex' is not differentiable")
      .Case("GeometryIndex",
            "system-value 'GeometryIndex' is not differentiable")
      .Case("ClusterID", "system-value 'ClusterID' is not differentiable")
      .Case("WorldRayOrigin",
            "system-value 'WorldRayOrigin' is not differentiable")
      .Case("WorldRayDirection",
            "system-value 'WorldRayDirection' is not differentiable")
      .Case("ObjectRayOrigin",
            "system-value 'ObjectRayOrigin' is not differentiable")
      .Case("ObjectRayDirection",
            "system-value 'ObjectRayDirection' is not differentiable")
      .Case("ObjectToWorld",
            "system-value 'ObjectToWorld' is not differentiable")
      .Case("ObjectToWorld3x4",
            "system-value 'ObjectToWorld3x4' is not differentiable")
      .Case("ObjectToWorld4x3",
            "system-value 'ObjectToWorld4x3' is not differentiable")
      .Case("WorldToObject",
            "system-value 'WorldToObject' is not differentiable")
      .Case("WorldToObject3x4",
            "system-value 'WorldToObject3x4' is not differentiable")
      .Case("WorldToObject4x3",
            "system-value 'WorldToObject4x3' is not differentiable")
      .Case("TriangleObjectPositions",
            "raytracing 'TriangleObjectPositions' is not differentiable")
      .Case("GetRemainingRecursionLevels",
            "system-value 'GetRemainingRecursionLevels' is not differentiable")
      .Case("GetGroupWaveCount",
            "system-value 'GetGroupWaveCount' is not differentiable")
      .Case("GetGroupWaveIndex",
            "system-value 'GetGroupWaveIndex' is not differentiable")
      .Case("GetRenderTargetSampleCount",
            "system-value 'GetRenderTargetSampleCount' is not differentiable")
      .Case("GetRenderTargetSamplePosition",
            "system-value 'GetRenderTargetSamplePosition' "
            "is not differentiable")
      .Case("GetSamplePosition",
            "system-value 'GetSamplePosition' is not differentiable")
      .Case("CheckAccessFullyMapped",
            "'CheckAccessFullyMapped' is not differentiable")
      .Case("CreateResourceFromHeap",
            "'CreateResourceFromHeap' is not differentiable")
      .Case("DispatchMesh",
            "mesh-shader 'DispatchMesh' is not differentiable")
      .Case("SetMeshOutputCounts",
            "mesh-shader 'SetMeshOutputCounts' is not differentiable")
      // Tessellator helpers.
      .Case("Process2DQuadTessFactorsAvg",
            "tessellator helper is not differentiable")
      .Case("Process2DQuadTessFactorsMax",
            "tessellator helper is not differentiable")
      .Case("Process2DQuadTessFactorsMin",
            "tessellator helper is not differentiable")
      .Case("ProcessIsolineTessFactors",
            "tessellator helper is not differentiable")
      .Case("ProcessQuadTessFactorsAvg",
            "tessellator helper is not differentiable")
      .Case("ProcessQuadTessFactorsMax",
            "tessellator helper is not differentiable")
      .Case("ProcessQuadTessFactorsMin",
            "tessellator helper is not differentiable")
      .Case("ProcessTriTessFactorsAvg",
            "tessellator helper is not differentiable")
      .Case("ProcessTriTessFactorsMax",
            "tessellator helper is not differentiable")
      .Case("ProcessTriTessFactorsMin",
            "tessellator helper is not differentiable")
      .Case("frexp", "'frexp' is not differentiable")
      .Case("sincos", "side-effecting 'sincos' is not differentiable; "
                       "use sin and cos separately")
      // Texture sampling family. Any name beginning with "tex", "Sample",
      // "Load", "Gather", "CalculateLevelOfDetail" is filtered below in
      // IsTextureLikeIntrinsic for completeness; the explicit entries here
      // pin down the historical d3d9-style helpers.
      .Case("tex1D", "texture sample is not differentiable")
      .Case("tex2D", "texture sample is not differentiable")
      .Case("tex3D", "texture sample is not differentiable")
      .Case("texCUBE", "texture sample is not differentiable")
      .Case("tex1Dbias", "texture sample is not differentiable")
      .Case("tex1Dgrad", "texture sample is not differentiable")
      .Case("tex1Dlod", "texture sample is not differentiable")
      .Case("tex1Dproj", "texture sample is not differentiable")
      .Case("tex2Dbias", "texture sample is not differentiable")
      .Case("tex2Dgrad", "texture sample is not differentiable")
      .Case("tex2Dlod", "texture sample is not differentiable")
      .Case("tex2Dproj", "texture sample is not differentiable")
      .Case("tex3Dbias", "texture sample is not differentiable")
      .Case("tex3Dgrad", "texture sample is not differentiable")
      .Case("tex3Dlod", "texture sample is not differentiable")
      .Case("tex3Dproj", "texture sample is not differentiable")
      .Case("texCUBEbias", "texture sample is not differentiable")
      .Case("texCUBEgrad", "texture sample is not differentiable")
      .Case("texCUBElod", "texture sample is not differentiable")
      .Case("texCUBEproj", "texture sample is not differentiable")
      .Default(nullptr);
}

// Textures / linear algebra intrinsics carry __builtin_ or Sample / Load /
// Gather / CalculateLevelOfDetail prefixes. Catch them by name pattern so
// new entries in gen_intrin_main.txt are non-differentiable by default.
bool IsTextureLikeIntrinsic(StringRef Name) {
  return Name.startswith("Sample") || Name.startswith("Load") ||
         Name.startswith("Gather") ||
         Name.startswith("CalculateLevelOfDetail") ||
         Name.startswith("__builtin_LinAlg");
}

} // anonymous namespace

namespace {

// Translate a function body expression into a textual representation in either
// forward or backward auto-diff form. The translation is intentionally
// conservative: a documented subset of HLSL is recognised and translated; any
// construct that falls outside that subset is rendered with /*TODO*/ markers
// and the original tokens so that the generated output is still useful to a
// human reviewer.
class AutoDiffEmitter {
public:
  enum Mode { Fwd, Bwd };

  AutoDiffEmitter(Mode M, StringRef ElemType, raw_ostream &OS,
                  const PrintingPolicy &P)
      : M(M), ElemType(ElemType), OS(OS), Policy(P) {}

  // Whether emission of the current function has encountered a construct that
  // is fundamentally not differentiable. Such constructs cause the emitter to
  // wrap the function body in a `_Static_assert(false, ...)` stub.
  bool sawNonDifferentiable() const { return NonDifferentiable; }
  StringRef nonDifferentiableReason() const { return Reason; }

  // Emit `<expr>` translated into the chosen mode.
  void emitExpr(const Expr *E) {
    E = E ? E->IgnoreParenImpCasts() : nullptr;
    if (!E) {
      OS << "/*null*/";
      return;
    }
    if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
      emitBinaryOp(BO);
      return;
    }
    if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
      emitUnaryOp(UO);
      return;
    }
    if (const auto *CE = dyn_cast<CallExpr>(E)) {
      emitCall(CE);
      return;
    }
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      emitDeclRef(DRE);
      return;
    }
    if (isa<ConditionalOperator>(E)) {
      // Ternary is not differentiable in a sound way unless both arms have
      // identical gradients; flag the function and emit the expression as-is
      // so the surrounding stub still parses.
      markNonDifferentiable("the ternary ?: operator is not differentiable");
      E->printPretty(OS, nullptr, Policy);
      return;
    }
    if (isa<FloatingLiteral>(E) || isa<IntegerLiteral>(E)) {
      // Literals are printed as-is; the ad library expects raw scalars in
      // many positions, so we don't wrap them in constant<T>(...).
      E->printPretty(OS, nullptr, Policy);
      return;
    }
    // Fallback: pretty print as-is so the generated function still compiles
    // approximately in the user's hand-off cases. Mark with a TODO.
    OS << "/*TODO: unsupported expr*/ ";
    E->printPretty(OS, nullptr, Policy);
  }

  // Translate a single statement. `Indent` is the leading whitespace for the
  // line(s) produced. Unrecognised statements pass through verbatim with a
  // TODO marker and flag the function as non-differentiable so it gets a
  // stub.
  void emitStmt(const Stmt *S, StringRef Indent) {
    if (!S) {
      OS << Indent << ";\n";
      return;
    }
    // [[dxc::no_diff]] copies the substatement verbatim.
    if (const auto *AS = dyn_cast<AttributedStmt>(S)) {
      for (const Attr *A : AS->getAttrs()) {
        if (isa<HLSLNoDiffAttr>(A)) {
          const Stmt *Sub = AS->getSubStmt();
          OS << Indent;
          Sub->printPretty(OS, nullptr, Policy);
          // Bare expressions used in statement position don't print their
          // trailing semicolon themselves; add it so the generated body
          // remains syntactically well-formed.
          if (isa<Expr>(Sub))
            OS << ";";
          OS << "\n";
          return;
        }
      }
      // Other attributes: fall through and process the substatement.
      emitStmt(AS->getSubStmt(), Indent);
      return;
    }
    if (const auto *CS = dyn_cast<CompoundStmt>(S)) {
      OS << Indent << "{\n";
      SmallString<32> Nested(Indent);
      Nested += "  ";
      for (const Stmt *Child : CS->body())
        emitStmt(Child, Nested);
      OS << Indent << "}\n";
      return;
    }
    if (const auto *RS = dyn_cast<ReturnStmt>(S)) {
      OS << Indent << "return";
      if (RS->getRetValue()) {
        OS << " ";
        emitExpr(RS->getRetValue());
      }
      OS << ";\n";
      return;
    }
    if (const auto *IS = dyn_cast<IfStmt>(S)) {
      // Control flow with data-dependent conditions is unsound to
      // differentiate without special handling. Flag it but still emit
      // structurally so the stub diagnostic is informative.
      markNonDifferentiable(
          "data-dependent control flow (if) is not differentiable; "
          "use [[dxc::no_diff]] or branchless math");
      OS << Indent << "if (";
      if (IS->getCond())
        IS->getCond()->printPretty(OS, nullptr, Policy);
      OS << ")\n";
      emitStmt(IS->getThen(), Indent);
      if (IS->getElse()) {
        OS << Indent << "else\n";
        emitStmt(IS->getElse(), Indent);
      }
      return;
    }
    if (const auto *WS = dyn_cast<WhileStmt>(S)) {
      markNonDifferentiable(
          "data-dependent control flow (while) is not differentiable");
      OS << Indent << "while (";
      if (WS->getCond())
        WS->getCond()->printPretty(OS, nullptr, Policy);
      OS << ")\n";
      emitStmt(WS->getBody(), Indent);
      return;
    }
    if (const auto *FS = dyn_cast<ForStmt>(S)) {
      markNonDifferentiable(
          "data-dependent control flow (for) is not differentiable");
      OS << Indent << "for (";
      if (FS->getInit())
        FS->getInit()->printPretty(OS, nullptr, Policy);
      else
        OS << ";";
      if (FS->getCond()) {
        OS << " ";
        FS->getCond()->printPretty(OS, nullptr, Policy);
      }
      OS << ";";
      if (FS->getInc()) {
        OS << " ";
        FS->getInc()->printPretty(OS, nullptr, Policy);
      }
      OS << ")\n";
      emitStmt(FS->getBody(), Indent);
      return;
    }
    if (const auto *DS = dyn_cast<DeclStmt>(S)) {
      // Declarations: translate each VarDecl, mapping its initializer
      // through the expression translator. The static type becomes
      // Value<T>/Variable<T> for the chosen mode.
      const char *Wrap = (M == Fwd) ? "Value" : "Variable";
      for (const Decl *D : DS->decls()) {
        const auto *VD = dyn_cast<VarDecl>(D);
        if (!VD) {
          markNonDifferentiable("unsupported declaration in body");
          OS << Indent;
          DS->printPretty(OS, nullptr, Policy);
          OS << "\n";
          return;
        }
        OS << Indent << Wrap << "<" << ElemType << "> " << VD->getName();
        if (VD->hasInit()) {
          OS << " = ";
          emitExpr(VD->getInit());
        }
        OS << ";\n";
      }
      return;
    }
    // ExprStmt / NullStmt and friends. Use printPretty for ExprStmt, but
    // translate the expression where possible.
    if (const auto *E = dyn_cast<Expr>(S)) {
      OS << Indent;
      emitExpr(E);
      OS << ";\n";
      return;
    }
    // Anything else: flag and pass through.
    markNonDifferentiable("unsupported statement in function body");
    OS << Indent << "/*TODO: unsupported statement*/ ";
    S->printPretty(OS, nullptr, Policy);
    OS << "\n";
  }

private:
  Mode M;
  StringRef ElemType;
  raw_ostream &OS;
  const PrintingPolicy &Policy;
  bool NonDifferentiable = false;
  std::string Reason;

  void markNonDifferentiable(StringRef R) {
    if (!NonDifferentiable) {
      NonDifferentiable = true;
      Reason = R.str();
    }
  }

  void emitBinaryOp(const BinaryOperator *BO) {
    // Classify operators up-front so we have a single source of truth for
    // both modes. Anything not in either list is "unknown" and produces a
    // diagnostic stub.
    BinaryOperatorKind Op = BO->getOpcode();
    const char *Fn = nullptr;
    bool Diff = true;
    switch (Op) {
    case BO_Add: Fn = "add"; break;
    case BO_Sub: Fn = "subtract"; break;
    case BO_Mul: Fn = "multiply"; break;
    case BO_Div: Fn = "divide"; break;
    case BO_Assign: Fn = "assign"; break;
    case BO_AddAssign: Fn = "addAssign"; break;
    case BO_SubAssign: Fn = "subAssign"; break;
    case BO_MulAssign: Fn = "mulAssign"; break;
    case BO_DivAssign: Fn = "divAssign"; break;
    // Comparison operators return bool: not meaningfully differentiable.
    case BO_LT:
    case BO_GT:
    case BO_LE:
    case BO_GE:
    case BO_EQ:
    case BO_NE:
      markNonDifferentiable("comparison operators are not differentiable");
      Diff = false;
      break;
    // Logical operators on bool: not meaningfully differentiable.
    case BO_LAnd:
    case BO_LOr:
      markNonDifferentiable("logical operators are not differentiable");
      Diff = false;
      break;
    // Bitwise operators on integral types: not differentiable.
    case BO_And:
    case BO_Or:
    case BO_Xor:
    case BO_Shl:
    case BO_Shr:
    case BO_AndAssign:
    case BO_OrAssign:
    case BO_XorAssign:
    case BO_ShlAssign:
    case BO_ShrAssign:
      markNonDifferentiable("bitwise operators are not differentiable");
      Diff = false;
      break;
    case BO_Rem:
    case BO_RemAssign:
      markNonDifferentiable("integer remainder is not differentiable");
      Diff = false;
      break;
    default:
      break;
    }
    if (!Diff) {
      // Best-effort pass-through; the stub assertion will reject the
      // generated function at compile time anyway.
      BO->printPretty(OS, nullptr, Policy);
      return;
    }
    if (M == Fwd) {
      // Forward mode: Value<T> overloads the standard operators.
      OS << "(";
      emitExpr(BO->getLHS());
      OS << " " << BinaryOperator::getOpcodeStr(Op) << " ";
      emitExpr(BO->getRHS());
      OS << ")";
      return;
    }
    if (!Fn) {
      OS << "/*TODO: unsupported binop*/ ";
      BO->printPretty(OS, nullptr, Policy);
      return;
    }
    OS << Fn << "<" << ElemType << ">(";
    emitExpr(BO->getLHS());
    OS << ", ";
    emitExpr(BO->getRHS());
    OS << ")";
  }

  void emitUnaryOp(const UnaryOperator *UO) {
    UnaryOperatorKind Op = UO->getOpcode();
    // Classify the unary operator first.
    switch (Op) {
    case UO_Plus:
    case UO_Minus:
      break; // differentiable
    case UO_LNot:
      markNonDifferentiable("logical-not is not differentiable");
      UO->printPretty(OS, nullptr, Policy);
      return;
    case UO_Not:
      markNonDifferentiable("bitwise-not is not differentiable");
      UO->printPretty(OS, nullptr, Policy);
      return;
    case UO_PreInc:
    case UO_PreDec:
    case UO_PostInc:
    case UO_PostDec:
      markNonDifferentiable(
          "increment/decrement operators are not differentiable");
      UO->printPretty(OS, nullptr, Policy);
      return;
    default:
      break;
    }
    if (M == Fwd) {
      OS << UnaryOperator::getOpcodeStr(Op);
      OS << "(";
      emitExpr(UO->getSubExpr());
      OS << ")";
      return;
    }
    if (Op == UO_Minus) {
      OS << "negate<" << ElemType << ">(";
      emitExpr(UO->getSubExpr());
      OS << ")";
      return;
    }
    if (Op == UO_Plus) {
      // Unary plus is a no-op even in backward mode.
      emitExpr(UO->getSubExpr());
      return;
    }
    OS << "/*TODO: unsupported unop*/ ";
    UO->printPretty(OS, nullptr, Policy);
  }

  void emitCall(const CallExpr *CE) {
    const FunctionDecl *Callee = CE->getDirectCallee();
    StringRef Name = Callee ? Callee->getName() : "";

    // Reject known-non-differentiable intrinsics, plus the texture / linalg
    // intrinsic families recognised by prefix.
    if (const char *R = GetNonDifferentiableReason(Name))
      markNonDifferentiable(R);
    else if (IsTextureLikeIntrinsic(Name))
      markNonDifferentiable("texture / linear-algebra intrinsic '" +
                            std::string(Name) + "' is not differentiable");

    if (M == Fwd) {
      // Forward mode: the ad library overloads the standard names.
      OS << Name << "(";
      for (unsigned I = 0, N = CE->getNumArgs(); I < N; ++I) {
        if (I)
          OS << ", ";
        emitExpr(CE->getArg(I));
      }
      OS << ")";
      return;
    }
    // Backward mode: map to a builder, when known.
    StringRef Mapped = GetBackwardIntrinsicBuilder(Name);
    if (Mapped.empty()) {
      // Unknown / non-differentiable callee: still write the original call
      // text so that, when paired with the _Static_assert stub, the
      // diagnostic includes the offending name.
      if (!NonDifferentiable)
        markNonDifferentiable("unknown callee '" + std::string(Name) +
                              "' has no auto-diff builder");
      OS << "/*non-differentiable call " << Name << "*/ ";
      CE->printPretty(OS, nullptr, Policy);
      return;
    }
    OS << Mapped << "<" << ElemType << ">(";
    for (unsigned I = 0, N = CE->getNumArgs(); I < N; ++I) {
      if (I)
        OS << ", ";
      emitExpr(CE->getArg(I));
    }
    OS << ")";
  }

  void emitDeclRef(const DeclRefExpr *DRE) {
    StringRef Name = DRE->getDecl()->getName();
    if (M == Fwd) {
      OS << Name;
      return;
    }
    // In backward mode, parameters are referenced through their _expr
    // VariableExpr wrappers declared at the top of the body.
    if (isa<ParmVarDecl>(DRE->getDecl())) {
      OS << Name << "_expr";
      return;
    }
    OS << Name;
  }
};

// Render the autodiff signature for a function in either mode.
void emitAutoDiffSignature(const FunctionDecl *FD, AutoDiffEmitter::Mode M,
                           StringRef ElemType, raw_ostream &OS) {
  if (M == AutoDiffEmitter::Fwd) {
    OS << "Value<" << ElemType << "> " << FD->getName() << "(";
    bool First = true;
    for (const ParmVarDecl *P : FD->parameters()) {
      if (!First)
        OS << ", ";
      First = false;
      OS << "Value<" << ElemType << "> " << P->getName();
    }
    OS << ")";
    return;
  }
  // Backward mode.
  OS << "Variable<" << ElemType << "> " << FD->getName()
     << "(inout GradientContext<" << ElemType << "> context";
  for (const ParmVarDecl *P : FD->parameters()) {
    OS << ", Variable<" << ElemType << "> " << P->getName();
  }
  OS << ")";
}

// Emit the auto-diff variant of a single function inside the appropriate
// namespace block. Returns true if anything was written.
bool emitAutoDiffFunction(const FunctionDecl *FD, AutoDiffEmitter::Mode M,
                          const PrintingPolicy &Policy, raw_ostream &OS) {
  // Determine the element type from the return type. We support scalar
  // float-like functions for now; other return types produce a TODO.
  std::string ElemType;
  raw_string_ostream ES(ElemType);
  FD->getReturnType().getCanonicalType().print(ES, Policy);
  ES.flush();

  // Render the body into a temporary buffer first so that, if a
  // non-differentiable construct was encountered, we can discard the body
  // and emit a _Static_assert stub instead. This makes the generated
  // header self-diagnosing: instantiating the function fails at compile
  // time with a clear message.
  std::string BodyText;
  raw_string_ostream BodyOS(BodyText);
  std::string Reason;
  bool ValidBody = false;

  if (const auto *CS = dyn_cast_or_null<CompoundStmt>(FD->getBody())) {
    AutoDiffEmitter Em(M, ElemType, BodyOS, Policy);
    if (M == AutoDiffEmitter::Bwd) {
      for (const ParmVarDecl *P : FD->parameters()) {
        BodyOS << "    VariableExpr<" << ElemType << "> " << P->getName()
               << "_expr = makeVariableExpr<" << ElemType << ">("
               << P->getName() << ");\n";
      }
    }
    for (const Stmt *S : CS->body())
      Em.emitStmt(S, "    ");
    BodyOS.flush();
    ValidBody = !Em.sawNonDifferentiable();
    if (!ValidBody)
      Reason = Em.nonDifferentiableReason().str();
  } else {
    ValidBody = false;
    Reason = "function has no body";
  }

  emitAutoDiffSignature(FD, M, ElemType, OS);
  OS << " {\n";
  if (ValidBody) {
    OS << BodyText;
  } else {
    // Stub: instantiating the generated function is a compile-time error.
    // Quote the reason for the diagnostic and provide a concrete return so
    // the surrounding code still parses.
    OS << "    _Static_assert(false, \"auto-diff cannot generate "
       << (M == AutoDiffEmitter::Fwd ? "forward" : "backward")
       << "-mode for '" << FD->getName() << "': " << Reason << "\");\n";
    if (M == AutoDiffEmitter::Fwd)
      OS << "    return Value<" << ElemType << ">();\n";
    else
      OS << "    return Variable<" << ElemType << ">();\n";
  }
  OS << "}\n";
  return true;
}

// Walk every nested NamespaceDecl in \p DC whose name matches the next
// element of \p Path; when the path is exhausted, record the name of every
// FunctionDecl directly declared in that namespace into \p Names.
//
// Namespaces in C++ may be reopened, so the matching is done across all
// declarations in \p DC, not just the first match. The walk descends into
// nested NamespaceDecls only — it deliberately ignores other declaration
// contexts (linkage specs, records, etc.) because the user::ad::{fwd,bwd}
// names are required to be namespaces by the autodiff convention.
void collectFunctionNamesInNamespace(const DeclContext *DC,
                                     ArrayRef<StringRef> Path,
                                     StringSet<> &Names) {
  if (Path.empty()) {
    for (const Decl *D : DC->decls()) {
      if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
        if (FD->getIdentifier())
          Names.insert(FD->getName());
      }
    }
    return;
  }
  StringRef Head = Path.front();
  ArrayRef<StringRef> Tail = Path.slice(1);
  for (const Decl *D : DC->decls()) {
    const auto *NS = dyn_cast<NamespaceDecl>(D);
    if (!NS || !NS->getIdentifier() || NS->getName() != Head)
      continue;
    collectFunctionNamesInNamespace(NS, Tail, Names);
  }
}

// Populate \p FwdNames and \p BwdNames with the unqualified names of every
// function the user has already declared (or defined) inside
// `user::ad::fwd` and `user::ad::bwd` respectively. These names are used to
// suppress regeneration of the corresponding autodiff stubs, allowing users
// to provide hand-written differentials or to incrementally check the
// generated ones into source control.
void collectUserAdFunctionNames(const TranslationUnitDecl *TU,
                                StringSet<> &FwdNames,
                                StringSet<> &BwdNames) {
  static const StringRef FwdPath[] = {"user", "ad", "fwd"};
  static const StringRef BwdPath[] = {"user", "ad", "bwd"};
  collectFunctionNamesInNamespace(TU, FwdPath, FwdNames);
  collectFunctionNamesInNamespace(TU, BwdPath, BwdNames);
}

// Emit forward and/or backward generated functions for a single annotated
// function, wrapped in the user::ad::{fwd,bwd}:: namespaces.
//
// If a function with the same unqualified name already exists in the
// corresponding user::ad::fwd / user::ad::bwd namespace (see
// collectUserAdFunctionNames) the corresponding mode is skipped, leaving
// the user's existing implementation untouched.
void EmitAutoDiffForFunction(const FunctionDecl *FD,
                             const HLSLAutoDiffAttr *Attr,
                             const StringSet<> &ExistingFwd,
                             const StringSet<> &ExistingBwd,
                             const PrintingPolicy &Policy, raw_ostream &OS) {
  StringRef Name = FD->getName();
  if (Attr->hasForward() && !ExistingFwd.count(Name)) {
    OS << "\nnamespace user { namespace ad { namespace fwd {\n";
    OS << "using namespace ::ad::fwd;\n";
    emitAutoDiffFunction(FD, AutoDiffEmitter::Fwd, Policy, OS);
    OS << "} } } // namespace user::ad::fwd\n";
  }
  if (Attr->hasBackward() && !ExistingBwd.count(Name)) {
    OS << "\nnamespace user { namespace ad { namespace bwd {\n";
    OS << "using namespace ::ad::bwd;\n";
    emitAutoDiffFunction(FD, AutoDiffEmitter::Bwd, Policy, OS);
    OS << "} } } // namespace user::ad::bwd\n";
  }
}

} // anonymous namespace

namespace hlsl {

void PrintTranslationUnitWithDifferentials(TranslationUnitDecl *tu,
                                           raw_ostream &OS,
                                           PrintingPolicy &Policy) {
  // Collect the names of any user::ad::fwd / user::ad::bwd functions the
  // input translation unit has already declared. We use these sets to skip
  // regenerating differentials a user has either hand-written or previously
  // checked in.
  StringSet<> ExistingFwd;
  StringSet<> ExistingBwd;
  collectUserAdFunctionNames(tu, ExistingFwd, ExistingBwd);

  // Determine which auto-diff library headers the generated output needs
  // and emit the corresponding #include directives at the very top of the
  // file. The wrapped namespace blocks pull names in with a
  // `using namespace ::ad::{fwd,bwd};` directive each, so the rewritten
  // bodies can stay free of fully-qualified names. We only include a
  // header if we are actually about to emit at least one function for
  // that mode; functions whose user-provided differentials are already
  // present do not require us to drag the library in.
  bool NeedFwd = false;
  bool NeedBwd = false;
  for (Decl *D : tu->decls()) {
    const auto *FD = dyn_cast<FunctionDecl>(D);
    if (!FD)
      continue;
    if (const auto *AD = FD->getAttr<HLSLAutoDiffAttr>()) {
      StringRef Name = FD->getName();
      NeedFwd |= AD->hasForward() && !ExistingFwd.count(Name);
      NeedBwd |= AD->hasBackward() && !ExistingBwd.count(Name);
    }
  }
  if (NeedFwd)
    OS << "#include <ad/fwd>\n";
  if (NeedBwd)
    OS << "#include <ad/bwd>\n";
  if (NeedFwd || NeedBwd)
    OS << "\n";

  for (Decl *D : tu->decls()) {
    if (D->isImplicit())
      continue;
    D->print(OS, Policy);
    // DeclPrinter typically does not append trailing semicolons for some
    // forms; we always insert a newline so that any subsequent text we
    // write begins on its own line.
    OS << "\n";
    auto *FD = dyn_cast<FunctionDecl>(D);
    if (!FD)
      continue;
    if (auto *AD = FD->getAttr<HLSLAutoDiffAttr>())
      EmitAutoDiffForFunction(FD, AD, ExistingFwd, ExistingBwd, Policy, OS);
  }
}

} // namespace hlsl
