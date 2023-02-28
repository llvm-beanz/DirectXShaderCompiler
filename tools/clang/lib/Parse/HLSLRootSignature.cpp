//===--- HLSLRootSignature.cpp -- HLSL root signature parsing -------------===//
///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// HLSLRootSignature.cpp                                                     //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
//  This file implements the parser for the root signature mini-language.    //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#include "HLSLRootSignature.h"
#include "dxc/Support/Global.h"
#include "dxc/Support/WinIncludes.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/ParseHLSL.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

#include <float.h>

using namespace llvm;
using namespace hlsl;

DEFINE_ENUM_FLAG_OPERATORS(DxilRootSignatureFlags)
DEFINE_ENUM_FLAG_OPERATORS(DxilRootDescriptorFlags)
DEFINE_ENUM_FLAG_OPERATORS(DxilDescriptorRangeFlags)
DEFINE_ENUM_FLAG_OPERATORS(DxilRootSignatureCompilationFlags)

RootSignatureTokenizer::RootSignatureTokenizer(const char *pStr, size_t len)
    : m_pStrPos(pStr), m_pEndPos(pStr + len) {
  m_TokenBufferIdx = 0;
  ReadNextToken(m_TokenBufferIdx);
}

RootSignatureTokenizer::RootSignatureTokenizer(const char *pStr)
    : m_pStrPos(pStr), m_pEndPos(pStr + strlen(pStr)) {
  m_TokenBufferIdx = 0;
  ReadNextToken(m_TokenBufferIdx);
}

RootSignatureTokenizer::Token RootSignatureTokenizer::GetToken() {
  uint32_t CurBufferIdx = m_TokenBufferIdx;
  m_TokenBufferIdx = (m_TokenBufferIdx + 1) % kNumBuffers;
  ReadNextToken(m_TokenBufferIdx);
  return m_Tokens[CurBufferIdx];
}

RootSignatureTokenizer::Token RootSignatureTokenizer::PeekToken() {
  return m_Tokens[m_TokenBufferIdx];
}

bool RootSignatureTokenizer::IsDone() const { return m_pStrPos == m_pEndPos; }

void RootSignatureTokenizer::ReadNextToken(uint32_t BufferIdx) {
  char *pBuffer = m_TokenStrings[BufferIdx];
  Token &T = m_Tokens[BufferIdx];
  bool bFloat = false;
  bool bKW = false;
  char c = 0;

  EatSpace();

  // Identify token
  uint32_t TokLen = 0;
  char ch;
  // Any time m_pStrPos moves, update ch; no null termination assumptions made.
  // Test ch, not m_StrPos[0], which may be at end.
#define STRPOS_MOVED() ch = m_pStrPos == m_pEndPos ? '\0' : m_pStrPos[0];
  STRPOS_MOVED();
  if (IsSeparator(ch)) {
    pBuffer[TokLen++] = *m_pStrPos++;
  } else if (IsDigit(ch) || ch == '+' || ch == '-' || ch == '.') {
    if (ch == '+' || ch == '-') {
      pBuffer[TokLen++] = *m_pStrPos++;
      STRPOS_MOVED();
    }

    bool bSeenDigit = false;
    while (IsDigit(ch) && TokLen < kMaxTokenLength) {
      pBuffer[TokLen++] = *m_pStrPos++;
      STRPOS_MOVED();
      bSeenDigit = true;
    }

    if (ch == '.') {
      bFloat = true;
      pBuffer[TokLen++] = *m_pStrPos++;
      STRPOS_MOVED();
      if (!bSeenDigit && !IsDigit(ch)) {
        goto lUnknownToken;
      }
      while (IsDigit(ch) && TokLen < kMaxTokenLength) {
        pBuffer[TokLen++] = *m_pStrPos++;
        STRPOS_MOVED();
        bSeenDigit = true;
      }
    }

    if (!bSeenDigit) {
      goto lUnknownToken;
    }

    if (ch == 'e' || ch == 'E') {
      bFloat = true;
      pBuffer[TokLen++] = *m_pStrPos++;
      STRPOS_MOVED()
      if (ch == '+' || ch == '-') {
        pBuffer[TokLen++] = *m_pStrPos++;
        STRPOS_MOVED();
      }
      if (!IsDigit(ch)) {
        goto lUnknownToken;
      }
      while (IsDigit(ch) && TokLen < kMaxTokenLength) {
        pBuffer[TokLen++] = *m_pStrPos++;
        STRPOS_MOVED();
      }
    }

    if (ch == 'f' || ch == 'F') {
      bFloat = true;
      pBuffer[TokLen++] = *m_pStrPos++;
      STRPOS_MOVED();
    }
  } else if (IsAlpha(ch) || ch == '_') {
    while ((IsAlpha(ch) || ch == '_' || IsDigit(ch)) &&
           TokLen < kMaxTokenLength) {
      pBuffer[TokLen++] = *m_pStrPos++;
      STRPOS_MOVED();
    }
  } else {
    while (m_pStrPos < m_pEndPos && TokLen < kMaxTokenLength) {
      pBuffer[TokLen++] = *m_pStrPos++;
      STRPOS_MOVED(); // not really needed, but good for consistency
    }
  }
  pBuffer[TokLen] = '\0';
#undef STRPOS_MOVED

  //
  // Classify token
  //
  c = pBuffer[0];

  // Delimiters
  switch (c) {
  case '\0':
    T = Token(Token::Type::EOL, pBuffer);
    return;
  case ',':
    T = Token(Token::Type::Comma, pBuffer);
    return;
  case '(':
    T = Token(Token::Type::LParen, pBuffer);
    return;
  case ')':
    T = Token(Token::Type::RParen, pBuffer);
    return;
  case '=':
    T = Token(Token::Type::EQ, pBuffer);
    return;
  case '|':
    T = Token(Token::Type::OR, pBuffer);
    return;
  }

  // Number
  if (IsDigit(c) || c == '+' || c == '-' || c == '.') {
    if (bFloat) {
      float v = 0.f;
      if (ToFloat(pBuffer, v)) {
        T = Token(Token::Type::NumberFloat, pBuffer, v);
        return;
      }
    } else {
      if (c == '-') {
        int n = 0;
        if (ToI32(pBuffer, n)) {
          T = Token(Token::Type::NumberI32, pBuffer, (uint32_t)n);
          return;
        }
      } else {
        uint32_t n = 0;
        if (ToU32(pBuffer, n)) {
          T = Token(Token::Type::NumberU32, pBuffer, n);
          return;
        }
      }
    }

    goto lUnknownToken;
  }

  // Register
  if (IsDigit(pBuffer[1]) && (c == 't' || c == 's' || c == 'u' || c == 'b')) {
    if (ToRegister(pBuffer, T))
      return;
  }

  // Keyword
#define KW(__name) ToKeyword(pBuffer, T, #__name, Token::Type::__name)

  // Case-incensitive
  switch (toupper(c)) {
  case 'A':
    bKW = KW(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT) || KW(ALLOW_STREAM_OUTPUT) ||
          KW(addressU) || KW(addressV) || KW(addressW);
    break;

  case 'B':
    bKW = KW(borderColor);
    break;

  case 'C':
    bKW = KW(CBV) || KW(comparisonFunc) || KW(COMPARISON_NEVER) ||
          KW(COMPARISON_LESS) || KW(COMPARISON_EQUAL) ||
          KW(COMPARISON_LESS_EQUAL) || KW(COMPARISON_GREATER) ||
          KW(COMPARISON_NOT_EQUAL) || KW(COMPARISON_GREATER_EQUAL) ||
          KW(COMPARISON_ALWAYS) || KW(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
    break;

  case 'D':
    bKW = KW(DescriptorTable) || KW(DESCRIPTOR_RANGE_OFFSET_APPEND) ||
          KW(DENY_VERTEX_SHADER_ROOT_ACCESS) ||
          KW(DENY_HULL_SHADER_ROOT_ACCESS) ||
          KW(DENY_DOMAIN_SHADER_ROOT_ACCESS) ||
          KW(DENY_GEOMETRY_SHADER_ROOT_ACCESS) ||
          KW(DENY_PIXEL_SHADER_ROOT_ACCESS) ||
          KW(DENY_AMPLIFICATION_SHADER_ROOT_ACCESS) ||
          KW(DENY_MESH_SHADER_ROOT_ACCESS) || KW(DESCRIPTORS_VOLATILE) ||
          KW(DATA_VOLATILE) || KW(DATA_STATIC) ||
          KW(DATA_STATIC_WHILE_SET_AT_EXECUTE) ||
          KW(DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS);
    break;

  case 'F':
    bKW =
        KW(flags) || KW(filter) || KW(FILTER_MIN_MAG_MIP_POINT) ||
        KW(FILTER_MIN_MAG_POINT_MIP_LINEAR) ||
        KW(FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT) ||
        KW(FILTER_MIN_POINT_MAG_MIP_LINEAR) ||
        KW(FILTER_MIN_LINEAR_MAG_MIP_POINT) ||
        KW(FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR) ||
        KW(FILTER_MIN_MAG_LINEAR_MIP_POINT) || KW(FILTER_MIN_MAG_MIP_LINEAR) ||
        KW(FILTER_ANISOTROPIC) || KW(FILTER_COMPARISON_MIN_MAG_MIP_POINT) ||
        KW(FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR) ||
        KW(FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT) ||
        KW(FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR) ||
        KW(FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT) ||
        KW(FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR) ||
        KW(FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT) ||
        KW(FILTER_COMPARISON_MIN_MAG_MIP_LINEAR) ||
        KW(FILTER_COMPARISON_ANISOTROPIC) ||
        KW(FILTER_MINIMUM_MIN_MAG_MIP_POINT) ||
        KW(FILTER_MINIMUM_MIN_MAG_POINT_MIP_LINEAR) ||
        KW(FILTER_MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT) ||
        KW(FILTER_MINIMUM_MIN_POINT_MAG_MIP_LINEAR) ||
        KW(FILTER_MINIMUM_MIN_LINEAR_MAG_MIP_POINT) ||
        KW(FILTER_MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR) ||
        KW(FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT) ||
        KW(FILTER_MINIMUM_MIN_MAG_MIP_LINEAR) ||
        KW(FILTER_MINIMUM_ANISOTROPIC) ||
        KW(FILTER_MAXIMUM_MIN_MAG_MIP_POINT) ||
        KW(FILTER_MAXIMUM_MIN_MAG_POINT_MIP_LINEAR) ||
        KW(FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT) ||
        KW(FILTER_MAXIMUM_MIN_POINT_MAG_MIP_LINEAR) ||
        KW(FILTER_MAXIMUM_MIN_LINEAR_MAG_MIP_POINT) ||
        KW(FILTER_MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR) ||
        KW(FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT) ||
        KW(FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR) || KW(FILTER_MAXIMUM_ANISOTROPIC);
    break;

  case 'L':
    bKW = KW(LOCAL_ROOT_SIGNATURE);
    break;

  case 'M':
    bKW = KW(maxAnisotropy) || KW(mipLODBias) || KW(minLOD) || KW(maxLOD);
    break;

  case 'N':
    bKW = KW(numDescriptors) || KW(num32BitConstants);
    break;

  case 'O':
    bKW = KW(offset);
    break;

  case 'R':
    bKW = KW(RootFlags) || KW(RootConstants);
    break;

  case 'S':
    bKW = KW(space) || KW(Sampler) || KW(StaticSampler) || KW(SRV) ||
          KW(SAMPLER_HEAP_DIRECTLY_INDEXED) || KW(SHADER_VISIBILITY_ALL) ||
          KW(SHADER_VISIBILITY_VERTEX) || KW(SHADER_VISIBILITY_HULL) ||
          KW(SHADER_VISIBILITY_DOMAIN) || KW(SHADER_VISIBILITY_GEOMETRY) ||
          KW(SHADER_VISIBILITY_PIXEL) || KW(SHADER_VISIBILITY_AMPLIFICATION) ||
          KW(SHADER_VISIBILITY_MESH) ||
          KW(STATIC_BORDER_COLOR_TRANSPARENT_BLACK) ||
          KW(STATIC_BORDER_COLOR_OPAQUE_BLACK) ||
          KW(STATIC_BORDER_COLOR_OPAQUE_WHITE) ||
          KW(STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT) ||
          KW(STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT) ||
          KW(SAMPLER_HEAP_DIRECTLY_INDEXED);

    break;

  case 'T':
    bKW = KW(TEXTURE_ADDRESS_WRAP) || KW(TEXTURE_ADDRESS_MIRROR) ||
          KW(TEXTURE_ADDRESS_CLAMP) || KW(TEXTURE_ADDRESS_BORDER) ||
          KW(TEXTURE_ADDRESS_MIRROR_ONCE);
    break;

  case 'U':
    bKW = KW(unbounded) || KW(UAV);
    break;

  case 'V':
    bKW = KW(visibility);
    break;
  }
#undef KW

  if (!bKW)
    goto lUnknownToken;

  return;

lUnknownToken:
  T = Token(Token::Type::Unknown, pBuffer);
}

void RootSignatureTokenizer::EatSpace() {
  while (m_pStrPos < m_pEndPos && *m_pStrPos == ' ')
    m_pStrPos++;
}

bool RootSignatureTokenizer::ToI32(LPCSTR pBuf, int &n) {
  if (pBuf[0] == '\0')
    return false;

  long long N = _atoi64(pBuf);
  if (N > INT_MAX || N < INT_MIN)
    return false;

  n = static_cast<int>(N);
  return true;
}

bool RootSignatureTokenizer::ToU32(LPCSTR pBuf, uint32_t &n) {
  if (pBuf[0] == '\0')
    return false;

  long long N = _atoi64(pBuf);
  if (N > UINT32_MAX || N < 0)
    return false;

  n = static_cast<uint32_t>(N);
  return true;
}

bool RootSignatureTokenizer::ToFloat(LPCSTR pBuf, float &n) {
  if (pBuf[0] == '\0')
    return false;

  errno = 0;
  double N = strtod(pBuf, NULL);
  if (errno == ERANGE || (N > FLT_MAX || N < -FLT_MAX))
    return false;

  n = static_cast<float>(N);
  return true;
}

bool RootSignatureTokenizer::ToRegister(LPCSTR pBuf, Token &T) {
  uint32_t n;
  if (ToU32(&pBuf[1], n)) {
    switch (pBuf[0]) {
    case 't':
      T = Token(Token::Type::TReg, pBuf, n);
      return true;
    case 's':
      T = Token(Token::Type::SReg, pBuf, n);
      return true;
    case 'u':
      T = Token(Token::Type::UReg, pBuf, n);
      return true;
    case 'b':
      T = Token(Token::Type::BReg, pBuf, n);
      return true;
    }
  }

  return false;
}

bool RootSignatureTokenizer::ToKeyword(const char *pBuf, Token &T,
                                       llvm::StringRef pKeyword,
                                       Token::Type Type) {
  // Tokens are case-insencitive to allow more flexibility for programmers
  if (pKeyword.equals_lower(llvm::StringRef(pBuf))) {
    T = Token(Type, pBuf);
    return true;
  }
  T = Token(Token::Type::Unknown, pBuf);
  return false;
}

bool RootSignatureTokenizer::IsSeparator(char c) const {
  return (c == ',' || c == '=' || c == '|' || c == '(' || c == ')' ||
          c == ' ' || c == '\t' || c == '\n');
}

bool RootSignatureTokenizer::IsDigit(char c) const { return isdigit(c) > 0; }

bool RootSignatureTokenizer::IsAlpha(char c) const { return isalpha(c) > 0; }

RootSignatureParser::RootSignatureParser(
    RootSignatureTokenizer *pTokenizer, DxilRootSignatureVersion DefaultVersion,
    DxilRootSignatureCompilationFlags CompilationFlags, llvm::raw_ostream &OS)
    : m_pTokenizer(pTokenizer), m_Version(DefaultVersion),
      m_CompilationFlags(CompilationFlags), m_OS(OS) {}

bool RootSignatureParser::Parse(
    DxilVersionedRootSignatureDesc **ppRootSignature) {

  DXASSERT(!((bool)(m_CompilationFlags &
                    DxilRootSignatureCompilationFlags::GlobalRootSignature) &&
             (bool)(m_CompilationFlags &
                    DxilRootSignatureCompilationFlags::LocalRootSignature)),
           "global and local cannot be both set");
  if (!ppRootSignature)
    return true;
  return ParseRootSignature(ppRootSignature);
}

bool RootSignatureParser::GetAndMatchToken(TokenType &Token,
                                           TokenType::Type Type) {
  Token = m_pTokenizer->GetToken();
  if (Token.GetType() != Type)
    return Error("Unexpected token '%s'", Token.GetStr());
  return true;
}

bool RootSignatureParser::Error(LPCSTR pError, ...) {
  va_list Args;
  char msg[512];
  va_start(Args, pError);
  vsnprintf_s(msg, _countof(msg), pError, Args);
  va_end(Args);
  m_OS << msg;
  return false;
}

bool RootSignatureParser::ParseRootSignature(
    DxilVersionedRootSignatureDesc **ppRootSignature) {
  TokenType Token;
  bool bSeenFlags = false;
  SmallVector<DxilRootParameter1, 8> RSParameters;
  SmallVector<DxilStaticSamplerDesc, 8> StaticSamplers;
  DxilVersionedRootSignatureDesc *pRS = nullptr;

  *ppRootSignature = NULL;
  pRS = new DxilVersionedRootSignatureDesc();

  struct Cleanup {
    std::function<void()> Fn = []() {};
    ~Cleanup() { Fn(); }
  } ScopeCleanup;

  ScopeCleanup.Fn = [pRS]() { hlsl::DeleteRootSignature(pRS); };

  // Always parse root signature string to the latest version.
  pRS->Version = DxilRootSignatureVersion::Version_1_1;
  memset(&pRS->Desc_1_1, 0, sizeof(pRS->Desc_1_1));

  Token = m_pTokenizer->PeekToken();
  while (Token.GetType() != TokenType::EOL) {
    switch (Token.GetType()) {
    case TokenType::RootFlags:
      if (!bSeenFlags) {
        if (!ParseRootSignatureFlags(pRS->Desc_1_1.Flags))
          return false;
        bSeenFlags = true;
      } else {
        return Error("RootFlags cannot be specified more than once");
      }
      break;

    case TokenType::RootConstants: {
      DxilRootParameter1 P;
      if (!ParseRootConstants(P))
        return false;
      RSParameters.push_back(P);
      break;
    }

    case TokenType::CBV: {
      DxilRootParameter1 P;
      if (!ParseRootShaderResource(Token.GetType(), TokenType::BReg,
                                   DxilRootParameterType::CBV, P))
        return false;
      RSParameters.push_back(P);
      break;
    }

    case TokenType::SRV: {
      DxilRootParameter1 P;
      if (!ParseRootShaderResource(Token.GetType(), TokenType::TReg,
                                   DxilRootParameterType::SRV, P))
        return false;
      RSParameters.push_back(P);
      break;
    }

    case TokenType::UAV: {
      DxilRootParameter1 P;
      if (!ParseRootShaderResource(Token.GetType(), TokenType::UReg,
                                   DxilRootParameterType::UAV, P))
        return false;
      RSParameters.push_back(P);
      break;
    }

    case TokenType::DescriptorTable: {
      DxilRootParameter1 P;
      if (!ParseRootDescriptorTable(P))
        return false;
      RSParameters.push_back(P);
      break;
    }

    case TokenType::StaticSampler: {
      DxilStaticSamplerDesc P;
      if (!ParseStaticSampler(P))
        return false;
      StaticSamplers.push_back(P);
      break;
    }

    default:
      return Error("Unexpected token '%s' when parsing root signature",
                   Token.GetStr());
    }

    Token = m_pTokenizer->GetToken();
    if (Token.GetType() == TokenType::EOL)
      break;

    // Consume ','
    if (Token.GetType() != TokenType::Comma)
      return Error("Expected ',', found: '%s'", Token.GetStr());

    Token = m_pTokenizer->PeekToken();
  }

  if (RSParameters.size() > 0) {
    pRS->Desc_1_1.pParameters = new DxilRootParameter1[RSParameters.size()];
    pRS->Desc_1_1.NumParameters = RSParameters.size();
    memcpy(pRS->Desc_1_1.pParameters, RSParameters.data(),
           pRS->Desc_1_1.NumParameters * sizeof(DxilRootParameter1));
  }
  if (StaticSamplers.size() > 0) {
    pRS->Desc_1_1.pStaticSamplers =
        new DxilStaticSamplerDesc[StaticSamplers.size()];
    pRS->Desc_1_1.NumStaticSamplers = StaticSamplers.size();
    memcpy(pRS->Desc_1_1.pStaticSamplers, StaticSamplers.data(),
           pRS->Desc_1_1.NumStaticSamplers * sizeof(DxilStaticSamplerDesc));
  }

  // Set local signature flag if not already on
  if ((bool)(m_CompilationFlags &
             DxilRootSignatureCompilationFlags::LocalRootSignature))
    pRS->Desc_1_1.Flags |= DxilRootSignatureFlags::LocalRootSignature;

  // Down-convert root signature to the right version, if needed.
  if (pRS->Version != m_Version) {
    DxilVersionedRootSignatureDesc *pRS1 = NULL;
    try {
      hlsl::ConvertRootSignature(
          pRS, m_Version,
          const_cast<const DxilVersionedRootSignatureDesc **>(&pRS1));
    } catch (...) {
      return false;
    }
    hlsl::DeleteRootSignature(pRS);
    pRS = pRS1;
  }

  *ppRootSignature = pRS;
  ScopeCleanup.Fn = []() {};

  return true;
}

bool RootSignatureParser::ParseRootSignatureFlags(
    DxilRootSignatureFlags &Flags) {
  // RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
  // DENY_VERTEX_SHADER_ROOT_ACCESS)
  //  ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
  //  DENY_VERTEX_SHADER_ROOT_ACCESS
  //  DENY_HULL_SHADER_ROOT_ACCESS
  //  DENY_DOMAIN_SHADER_ROOT_ACCESS
  //  DENY_GEOMETRY_SHADER_ROOT_ACCESS
  //  DENY_PIXEL_SHADER_ROOT_ACCESS
  //  DENY_AMPLIFICATION_SHADER_ROOT_ACCESS
  //  DENY_MESH_SHADER_ROOT_ACCESS
  //  ALLOW_STREAM_OUTPUT
  //  LOCAL_ROOT_SIGNATURE

  TokenType Token;

  if (!GetAndMatchToken(Token, TokenType::RootFlags))
    return false;
  if (!GetAndMatchToken(Token, TokenType::LParen))
    return false;

  Flags = DxilRootSignatureFlags::None;

  Token = m_pTokenizer->PeekToken();
  if (Token.GetType() == TokenType::NumberU32) {
    if (!GetAndMatchToken(Token, TokenType::NumberU32))
      return false;
    uint32_t n = Token.GetU32Value();
    if (n != 0) {
      return Error("Root signature flag values can only be 0 or flag enum "
                   "values, found: '%s'",
                   Token.GetStr());
    }
  } else {
    for (;;) {
      Token = m_pTokenizer->GetToken();
      switch (Token.GetType()) {
      case TokenType::ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT:
        Flags |= DxilRootSignatureFlags::AllowInputAssemblerInputLayout;
        break;
      case TokenType::DENY_VERTEX_SHADER_ROOT_ACCESS:
        Flags |= DxilRootSignatureFlags::DenyVertexShaderRootAccess;
        break;
      case TokenType::DENY_HULL_SHADER_ROOT_ACCESS:
        Flags |= DxilRootSignatureFlags::DenyHullShaderRootAccess;
        break;
      case TokenType::DENY_DOMAIN_SHADER_ROOT_ACCESS:
        Flags |= DxilRootSignatureFlags::DenyDomainShaderRootAccess;
        break;
      case TokenType::DENY_GEOMETRY_SHADER_ROOT_ACCESS:
        Flags |= DxilRootSignatureFlags::DenyGeometryShaderRootAccess;
        break;
      case TokenType::DENY_PIXEL_SHADER_ROOT_ACCESS:
        Flags |= DxilRootSignatureFlags::DenyPixelShaderRootAccess;
        break;
      case TokenType::DENY_AMPLIFICATION_SHADER_ROOT_ACCESS:
        Flags |= DxilRootSignatureFlags::DenyAmplificationShaderRootAccess;
        break;
      case TokenType::DENY_MESH_SHADER_ROOT_ACCESS:
        Flags |= DxilRootSignatureFlags::DenyMeshShaderRootAccess;
        break;
      case TokenType::ALLOW_STREAM_OUTPUT:
        Flags |= DxilRootSignatureFlags::AllowStreamOutput;
        break;
      case TokenType::LOCAL_ROOT_SIGNATURE:
        if ((bool)(m_CompilationFlags &
                   DxilRootSignatureCompilationFlags::GlobalRootSignature))
          return Error(
              "LOCAL_ROOT_SIGNATURE flag used in global root signature");
        Flags |= DxilRootSignatureFlags::LocalRootSignature;
        break;
      case TokenType::CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED:
        Flags |= DxilRootSignatureFlags::CBVSRVUAVHeapDirectlyIndexed;
        break;
      case TokenType::SAMPLER_HEAP_DIRECTLY_INDEXED:
        Flags |= DxilRootSignatureFlags::SamplerHeapDirectlyIndexed;
        break;
      default:
        return Error("Expected a root signature flag value, found: '%s'",
                     Token.GetStr());
      }

      Token = m_pTokenizer->PeekToken();
      if (Token.GetType() == TokenType::RParen)
        break;

      if (!GetAndMatchToken(Token, TokenType::OR))
        return false;
    }
  }

  if (!GetAndMatchToken(Token, TokenType::RParen))
    return false;

  return true;
}

bool RootSignatureParser::ParseRootConstants(DxilRootParameter1 &P) {
  //"RootConstants(num32BitConstants=3, b2 [, space=1,
  // visibility=SHADER_VISIBILITY_ALL ] ), "
  TokenType Token;
  memset(&P, 0, sizeof(P));
  DXASSERT(P.ShaderVisibility == DxilShaderVisibility::All,
           "else default isn't zero");
  P.ParameterType = DxilRootParameterType::Constants32Bit;
  bool bSeenNum32BitConstants = false;
  bool bSeenBReg = false;
  bool bSeenSpace = false;
  bool bSeenVisibility = false;

  if (!GetAndMatchToken(Token, TokenType::RootConstants))
    return false;
  if (!GetAndMatchToken(Token, TokenType::LParen))
    return false;

  for (;;) {
    Token = m_pTokenizer->PeekToken();

    switch (Token.GetType()) {
    case TokenType::num32BitConstants:
      if (!MarkParameter(bSeenNum32BitConstants, "num32BitConstants"))
        return false;
      if (!ParseNum32BitConstants(P.Constants.Num32BitValues))
        return false;
      break;
    case TokenType::BReg:
      if (!MarkParameter(bSeenBReg, "cbuffer register b#"))
        return false;
      if (!ParseRegister(TokenType::BReg, P.Constants.ShaderRegister))
        return false;
      break;
    case TokenType::space:
      if (!MarkParameter(bSeenSpace, "space"))
        return false;
      if (!ParseSpace(P.Constants.RegisterSpace))
        return false;
      break;
    case TokenType::visibility:
      if (!MarkParameter(bSeenVisibility, "visibility"))
        return false;
      if (!ParseVisibility(P.ShaderVisibility))
        return false;
      break;
    default:
      return Error("Unexpected token '%s'", Token.GetStr());
      break;
    }

    Token = m_pTokenizer->GetToken();
    if (Token.GetType() == TokenType::RParen)
      break;
    else if (Token.GetType() != TokenType::Comma)
      return Error("Unexpected token '%s'", Token.GetStr());
  }

  if (!bSeenNum32BitConstants)
    return Error("num32BitConstants must be defined for each RootConstants");

  if (!bSeenBReg)
    return Error(
        "Constant buffer register b# must be defined for each RootConstants");

  return true;
}

bool RootSignatureParser::ParseRootShaderResource(TokenType::Type TokType,
                                                  TokenType::Type RegType,
                                                  DxilRootParameterType ResType,
                                                  DxilRootParameter1 &P) {
  // CBV(b0 [, space=3, flags=0, visibility=VISIBILITY_ALL] )
  TokenType Token;
  P.ParameterType = ResType;
  P.ShaderVisibility = DxilShaderVisibility::All;
  P.Descriptor.ShaderRegister = 0;
  P.Descriptor.RegisterSpace = 0;
  P.Descriptor.Flags = DxilRootDescriptorFlags::None;
  bool bSeenReg = false;
  bool bSeenFlags = false;
  bool bSeenSpace = false;
  bool bSeenVisibility = false;

  if (!GetAndMatchToken(Token, TokType))
    return false;
  if (!GetAndMatchToken(Token, TokenType::LParen))
    return false;

  for (;;) {
    Token = m_pTokenizer->PeekToken();

    switch (Token.GetType()) {
    case TokenType::BReg:
    case TokenType::TReg:
    case TokenType::UReg:
      if (!MarkParameter(bSeenReg, "shader register"))
        return false;
      if (!ParseRegister(RegType, P.Descriptor.ShaderRegister))
        return false;
      break;
    case TokenType::flags:
      if (!MarkParameter(bSeenFlags, "flags"))
        return false;
      if (!ParseRootDescFlags(P.Descriptor.Flags))
        return false;
      break;
    case TokenType::space:
      if (!MarkParameter(bSeenSpace, "space"))
        return false;
      if (!ParseSpace(P.Descriptor.RegisterSpace))
        return false;
      break;
    case TokenType::visibility:
      if (!MarkParameter(bSeenVisibility, "visibility"))
        return false;
      if (!ParseVisibility(P.ShaderVisibility))
        return false;
      break;
    default:
      return Error("Unexpected token '%s'", Token.GetStr());
      break;
    }

    Token = m_pTokenizer->GetToken();
    if (Token.GetType() == TokenType::RParen)
      break;
    else if (Token.GetType() != TokenType::Comma)
      return Error("Unexpected token '%s'", Token.GetStr());
  }

  if (!bSeenReg)
    return Error("shader register must be defined for each CBV/SRV/UAV");

  return true;
}

bool RootSignatureParser::ParseRootDescriptorTable(DxilRootParameter1 &P) {
  // DescriptorTable( SRV(t2, numDescriptors = 6), UAV(u0, numDescriptors = 4)
  // [, visibility = SHADER_VISIBILITY_ALL ] )
  TokenType Token;
  memset(&P, 0, sizeof(P));
  DXASSERT(P.ShaderVisibility == DxilShaderVisibility::All,
           "else default isn't zero");
  P.ParameterType = DxilRootParameterType::DescriptorTable;
  bool bSeenVisibility = false;
  SmallVector<DxilDescriptorRange1, 4> Ranges;

  if (!GetAndMatchToken(Token, TokenType::DescriptorTable))
    return false;
  if (!GetAndMatchToken(Token, TokenType::LParen))
    return false;

  for (;;) {
    Token = m_pTokenizer->PeekToken();

    switch (Token.GetType()) {
    case TokenType::CBV: {
      DxilDescriptorRange1 R;
      if (!ParseDescTableResource(Token.GetType(), TokenType::BReg,
                                  DxilDescriptorRangeType::CBV, R))
        return false;
      Ranges.push_back(R);
      break;
    }
    case TokenType::SRV: {
      DxilDescriptorRange1 R;
      if (!ParseDescTableResource(Token.GetType(), TokenType::TReg,
                                  DxilDescriptorRangeType::SRV, R))
        return false;
      Ranges.push_back(R);
      break;
    }
    case TokenType::UAV: {
      DxilDescriptorRange1 R;
      if (!ParseDescTableResource(Token.GetType(), TokenType::UReg,
                                  DxilDescriptorRangeType::UAV, R))
        return false;
      Ranges.push_back(R);
      break;
    }
    case TokenType::Sampler: {
      DxilDescriptorRange1 R;
      if (!ParseDescTableResource(Token.GetType(), TokenType::SReg,
                                  DxilDescriptorRangeType::Sampler, R))
        return false;
      Ranges.push_back(R);
      break;
    }
    case TokenType::visibility:
      if (!MarkParameter(bSeenVisibility, "visibility"))
        return false;
      if (!ParseVisibility(P.ShaderVisibility))
        return false;
      break;
    default:
      return Error("Unexpected token '%s'", Token.GetStr());
      break;
    }

    Token = m_pTokenizer->GetToken();
    if (Token.GetType() == TokenType::RParen)
      break;
    else if (Token.GetType() != TokenType::Comma)
      return Error("Unexpected token '%s'", Token.GetStr());
  }

  if (Ranges.size() > 0) {
    P.DescriptorTable.pDescriptorRanges =
        new DxilDescriptorRange1[Ranges.size()];
    memcpy(P.DescriptorTable.pDescriptorRanges, Ranges.data(),
           Ranges.size() * sizeof(DxilDescriptorRange1));
    P.DescriptorTable.NumDescriptorRanges = Ranges.size();
  }

  return true;
}

bool RootSignatureParser::ParseDescTableResource(
    TokenType::Type TokType, TokenType::Type RegType,
    DxilDescriptorRangeType RangeType, DxilDescriptorRange1 &R) {
  TokenType Token;
  // CBV(b0 [, numDescriptors = 1, space=0, flags=0, offset =
  // DESCRIPTOR_RANGE_OFFSET_APPEND] )

  R.RangeType = RangeType;
  R.NumDescriptors = 1;
  R.BaseShaderRegister = 0;
  R.RegisterSpace = 0;
  R.Flags = DxilDescriptorRangeFlags::None;
  R.OffsetInDescriptorsFromTableStart = DxilDescriptorRangeOffsetAppend;
  bool bSeenReg = false;
  bool bSeenNumDescriptors = false;
  bool bSeenSpace = false;
  bool bSeenFlags = false;
  bool bSeenOffset = false;

  if (!GetAndMatchToken(Token, TokType))
    return false;
  if (!GetAndMatchToken(Token, TokenType::LParen))
    return false;

  for (;;) {
    Token = m_pTokenizer->PeekToken();

    switch (Token.GetType()) {
    case TokenType::BReg:
    case TokenType::TReg:
    case TokenType::UReg:
    case TokenType::SReg:
      if (!MarkParameter(bSeenReg, "shader register"))
        return false;
      if (!ParseRegister(RegType, R.BaseShaderRegister))
        return false;
      break;
    case TokenType::numDescriptors:
      if (!MarkParameter(bSeenNumDescriptors, "numDescriptors"))
        return false;
      if (!ParseNumDescriptors(R.NumDescriptors))
        return false;
      break;
    case TokenType::space:
      if (!MarkParameter(bSeenSpace, "space"))
        return false;
      if (!ParseSpace(R.RegisterSpace))
        return false;
      break;
    case TokenType::flags:
      if (!MarkParameter(bSeenFlags, "flags"))
        return false;
      if (!ParseDescRangeFlags(RangeType, R.Flags))
        return false;
      break;
    case TokenType::offset:
      if (!MarkParameter(bSeenOffset, "offset"))
        return false;
      if (!ParseOffset(R.OffsetInDescriptorsFromTableStart))
        return false;
      break;
    default:
      return Error("Unexpected token '%s'", Token.GetStr());
      break;
    }

    Token = m_pTokenizer->GetToken();
    if (Token.GetType() == TokenType::RParen)
      break;
    else if (Token.GetType() != TokenType::Comma)
      return Error("Unexpected token '%s'", Token.GetStr());
  }

  if (!bSeenReg)
    return Error("shader register must be defined for each CBV/SRV/UAV");

  return true;
}

bool RootSignatureParser::ParseRegister(TokenType::Type RegType,
                                        uint32_t &Reg) {
  TokenType Token = m_pTokenizer->PeekToken();

  switch (Token.GetType()) {
  case TokenType::BReg:
    if (!GetAndMatchToken(Token, TokenType::BReg))
      return false;
    break;
  case TokenType::TReg:
    if (!GetAndMatchToken(Token, TokenType::TReg))
      return false;
    break;
  case TokenType::UReg:
    if (!GetAndMatchToken(Token, TokenType::UReg))
      return false;
    break;
  case TokenType::SReg:
    if (!GetAndMatchToken(Token, TokenType::SReg))
      return false;
    break;
  default:
    return Error(
        "Expected a register token (CBV, SRV, UAV, Sampler), found: '%s'",
        Token.GetStr());
  }

  if (Token.GetType() != RegType) {
    switch (RegType) {
    case TokenType::BReg:
      return Error("Incorrect register type '%s' in CBV (expected b#)",
                   Token.GetStr());
    case TokenType::TReg:
      return Error("Incorrect register type '%s' in SRV (expected t#)",
                   Token.GetStr());
    case TokenType::UReg:
      return Error("Incorrect register type '%s' in UAV (expected u#)",
                   Token.GetStr());
    case TokenType::SReg:
      return Error(
          "Incorrect register type '%s' in Sampler/StaticSampler (expected s#)",
          Token.GetStr());
    default:
      // Only Register types are relevant.
      break;
    }
  }

  Reg = Token.GetU32Value();

  return true;
}

bool RootSignatureParser::ParseSpace(uint32_t &Space) {
  TokenType Token;

  if (!GetAndMatchToken(Token, TokenType::space))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;
  if (!GetAndMatchToken(Token, TokenType::NumberU32))
    return false;
  Space = Token.GetU32Value();

  return true;
}

bool RootSignatureParser::ParseNumDescriptors(uint32_t &NumDescriptors) {
  TokenType Token;

  if (!GetAndMatchToken(Token, TokenType::numDescriptors))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;
  Token = m_pTokenizer->PeekToken();
  if (Token.GetType() == TokenType::unbounded) {
    if (!GetAndMatchToken(Token, TokenType::unbounded))
      return false;
    NumDescriptors = UINT32_MAX;
  } else {
    if (!GetAndMatchToken(Token, TokenType::NumberU32))
      return false;
    NumDescriptors = Token.GetU32Value();
  }

  return true;
}

bool RootSignatureParser::ParseRootDescFlags(DxilRootDescriptorFlags &Flags) {
  // flags=DATA_VOLATILE | DATA_STATIC | DATA_STATIC_WHILE_SET_AT_EXECUTE
  TokenType Token;

  if (m_Version == DxilRootSignatureVersion::Version_1_0)
    return Error("Root descriptor flags cannot be specified for root_sig_1_0");

  if (!GetAndMatchToken(Token, TokenType::flags))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;

  Flags = DxilRootDescriptorFlags::None;

  Token = m_pTokenizer->PeekToken();
  if (Token.GetType() == TokenType::NumberU32) {
    if (!GetAndMatchToken(Token, TokenType::NumberU32))
      return false;
    uint32_t n = Token.GetU32Value();
    if (n != 0)
      return Error("Root descriptor flag values can only be 0 or flag enum "
                   "values, found: '%s'",
                   Token.GetStr());
  } else {
    for (;;) {
      Token = m_pTokenizer->GetToken();
      switch (Token.GetType()) {
      case TokenType::DATA_VOLATILE:
        Flags |= DxilRootDescriptorFlags::DataVolatile;
        break;
      case TokenType::DATA_STATIC:
        Flags |= DxilRootDescriptorFlags::DataStatic;
        break;
      case TokenType::DATA_STATIC_WHILE_SET_AT_EXECUTE:
        Flags |= DxilRootDescriptorFlags::DataStaticWhileSetAtExecute;
        break;
      default:
        return Error("Expected a root descriptor flag value, found: '%s'",
                     Token.GetStr());
      }

      Token = m_pTokenizer->PeekToken();
      if (Token.GetType() == TokenType::RParen ||
          Token.GetType() == TokenType::Comma)
        break;

      if (!GetAndMatchToken(Token, TokenType::OR))
        return false;
    }
  }
  return true;
}

bool RootSignatureParser::ParseDescRangeFlags(DxilDescriptorRangeType,
                                              DxilDescriptorRangeFlags &Flags) {
  // flags=DESCRIPTORS_VOLATILE | DATA_VOLATILE | DATA_STATIC |
  // DATA_STATIC_WHILE_SET_AT_EXECUTE |
  // DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS

  TokenType Token;

  if (m_Version == DxilRootSignatureVersion::Version_1_0) {
    return Error("Descriptor range flags cannot be specified for root_sig_1_0");
  }

  if (!GetAndMatchToken(Token, TokenType::flags))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;

  Flags = DxilDescriptorRangeFlags::None;

  Token = m_pTokenizer->PeekToken();
  if (Token.GetType() == TokenType::NumberU32) {
    if (!GetAndMatchToken(Token, TokenType::NumberU32))
      return false;
    uint32_t n = Token.GetU32Value();
    if (n != 0) {
      return Error("Descriptor range flag values can only be 0 or flag enum "
                   "values, found: '%s'",
                   Token.GetStr());
    }
  } else {
    for (;;) {
      Token = m_pTokenizer->GetToken();
      switch (Token.GetType()) {
      case TokenType::DESCRIPTORS_VOLATILE:
        Flags |= DxilDescriptorRangeFlags::DescriptorsVolatile;
        break;
      case TokenType::DATA_VOLATILE:
        Flags |= DxilDescriptorRangeFlags::DataVolatile;
        break;
      case TokenType::DATA_STATIC:
        Flags |= DxilDescriptorRangeFlags::DataStatic;
        break;
      case TokenType::DATA_STATIC_WHILE_SET_AT_EXECUTE:
        Flags |= DxilDescriptorRangeFlags::DataStaticWhileSetAtExecute;
        break;
      case TokenType::DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS:
        Flags |= DxilDescriptorRangeFlags::
            DescriptorsStaticKeepingBufferBoundsChecks;
        break;
      default:
        return Error("Expected a descriptor range flag value, found: '%s'",
                     Token.GetStr());
      }

      Token = m_pTokenizer->PeekToken();
      if (Token.GetType() == TokenType::RParen ||
          Token.GetType() == TokenType::Comma)
        break;

      if (!GetAndMatchToken(Token, TokenType::OR))
        return false;
    }
  }

  return true;
}

bool RootSignatureParser::ParseOffset(uint32_t &Offset) {
  TokenType Token;

  if (!GetAndMatchToken(Token, TokenType::offset))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;
  Token = m_pTokenizer->PeekToken();
  if (Token.GetType() == TokenType::DESCRIPTOR_RANGE_OFFSET_APPEND) {
    if (!GetAndMatchToken(Token, TokenType::DESCRIPTOR_RANGE_OFFSET_APPEND))
      return false;
    Offset = DxilDescriptorRangeOffsetAppend;
  } else {
    if (!GetAndMatchToken(Token, TokenType::NumberU32))
      return false;
    Offset = Token.GetU32Value();
  }

  return true;
}

bool RootSignatureParser::ParseVisibility(DxilShaderVisibility &Vis) {
  TokenType Token;

  if (!GetAndMatchToken(Token, TokenType::visibility))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;
  Token = m_pTokenizer->GetToken();

  switch (Token.GetType()) {
  case TokenType::SHADER_VISIBILITY_ALL:
    Vis = DxilShaderVisibility::All;
    break;
  case TokenType::SHADER_VISIBILITY_VERTEX:
    Vis = DxilShaderVisibility::Vertex;
    break;
  case TokenType::SHADER_VISIBILITY_HULL:
    Vis = DxilShaderVisibility::Hull;
    break;
  case TokenType::SHADER_VISIBILITY_DOMAIN:
    Vis = DxilShaderVisibility::Domain;
    break;
  case TokenType::SHADER_VISIBILITY_GEOMETRY:
    Vis = DxilShaderVisibility::Geometry;
    break;
  case TokenType::SHADER_VISIBILITY_PIXEL:
    Vis = DxilShaderVisibility::Pixel;
    break;
  case TokenType::SHADER_VISIBILITY_AMPLIFICATION:
    Vis = DxilShaderVisibility::Amplification;
    break;
  case TokenType::SHADER_VISIBILITY_MESH:
    Vis = DxilShaderVisibility::Mesh;
    break;
  default:
    return Error("Unexpected visibility value: '%s'.", Token.GetStr());
  }

  return true;
}

bool RootSignatureParser::ParseNum32BitConstants(uint32_t &NumConst) {
  TokenType Token;

  if (!GetAndMatchToken(Token, TokenType::num32BitConstants))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;
  if (!GetAndMatchToken(Token, TokenType::NumberU32))
    return false;
  NumConst = Token.GetU32Value();

  return true;
}

bool RootSignatureParser::ParseStaticSampler(DxilStaticSamplerDesc &P) {
  // StaticSampler( s0,
  //                [ Filter = FILTER_ANISOTROPIC,
  //                  AddressU = TEXTURE_ADDRESS_WRAP,
  //                  AddressV = TEXTURE_ADDRESS_WRAP,
  //                  AddressW = TEXTURE_ADDRESS_WRAP,
  //                  MipLODBias = 0,
  //                  MaxAnisotropy = 16,
  //                  ComparisonFunc = COMPARISON_LESS_EQUAL,
  //                  BorderColor = STATIC_BORDER_COLOR_OPAQUE_WHITE,
  //                  MinLOD = 0.f,
  //                  MaxLOD = 3.402823466e+38f
  //                  space = 0,
  //                  visibility = SHADER_VISIBILITY_ALL ] )
  TokenType Token;
  memset(&P, 0, sizeof(P));
  P.Filter = DxilFilter::ANISOTROPIC;
  P.AddressU = P.AddressV = P.AddressW = DxilTextureAddressMode::Wrap;
  P.MaxAnisotropy = 16;
  P.ComparisonFunc = DxilComparisonFunc::LessEqual;
  P.BorderColor = DxilStaticBorderColor::OpaqueWhite;
  P.MaxLOD = DxilFloat32Max;
  bool bSeenFilter = false;
  bool bSeenAddressU = false;
  bool bSeenAddressV = false;
  bool bSeenAddressW = false;
  bool bSeenMipLODBias = false;
  bool bSeenMaxAnisotropy = false;
  bool bSeenComparisonFunc = false;
  bool bSeenBorderColor = false;
  bool bSeenMinLOD = false;
  bool bSeenMaxLOD = false;
  bool bSeenSReg = false;
  bool bSeenSpace = false;
  bool bSeenVisibility = false;

  if (!GetAndMatchToken(Token, TokenType::StaticSampler))
    return false;
  if (!GetAndMatchToken(Token, TokenType::LParen))
    return false;

  for (;;) {
    Token = m_pTokenizer->PeekToken();

    switch (Token.GetType()) {
    case TokenType::filter:
      if (!MarkParameter(bSeenFilter, "filter"))
        return false;
      if (!ParseFilter(P.Filter))
        return false;
      break;
    case TokenType::addressU:
      if (!MarkParameter(bSeenAddressU, "addressU"))
        return false;
      if (!ParseTextureAddressMode(P.AddressU))
        return false;
      break;
    case TokenType::addressV:
      if (!MarkParameter(bSeenAddressV, "addressV"))
        return false;
      if (!ParseTextureAddressMode(P.AddressV))
        return false;
      break;
    case TokenType::addressW:
      if (!MarkParameter(bSeenAddressW, "addressW"))
        return false;
      if (!ParseTextureAddressMode(P.AddressW))
        return false;
      break;
    case TokenType::mipLODBias:
      if (!MarkParameter(bSeenMipLODBias, "mipLODBias"))
        return false;
      if (!ParseMipLODBias(P.MipLODBias))
        return false;
      break;
    case TokenType::maxAnisotropy:
      if (!MarkParameter(bSeenMaxAnisotropy, "maxAnisotropy"))
        return false;
      if (!ParseMaxAnisotropy(P.MaxAnisotropy))
        return false;
      break;
    case TokenType::comparisonFunc:
      if (!MarkParameter(bSeenComparisonFunc, "comparisonFunc"))
        return false;
      if (!ParseComparisonFunction(P.ComparisonFunc))
        return false;
      break;
    case TokenType::borderColor:
      if (!MarkParameter(bSeenBorderColor, "borderColor"))
        return false;
      if (!ParseBorderColor(P.BorderColor))
        return false;
      break;
    case TokenType::minLOD:
      if (!MarkParameter(bSeenMinLOD, "minLOD"))
        return false;
      if (!ParseMinLOD(P.MinLOD))
        return false;
      break;
    case TokenType::maxLOD:
      if (!MarkParameter(bSeenMaxLOD, "maxLOD"))
        return false;
      if (!ParseMaxLOD(P.MaxLOD))
        return false;
      break;
    case TokenType::SReg:
      if (!MarkParameter(bSeenSReg, "sampler register s#"))
        return false;
      if (!ParseRegister(TokenType::SReg, P.ShaderRegister))
        return false;
      break;
    case TokenType::space:
      if (!MarkParameter(bSeenSpace, "space"))
        return false;
      if (!ParseSpace(P.RegisterSpace))
        return false;
      break;
    case TokenType::visibility:
      if (!MarkParameter(bSeenVisibility, "visibility"))
        return false;
      if (!ParseVisibility(P.ShaderVisibility))
        return false;
      break;
    default:
      return Error("Unexpected token '%s'", Token.GetStr());
    }

    Token = m_pTokenizer->GetToken();
    if (Token.GetType() == TokenType::RParen)
      break;
    else if (Token.GetType() != TokenType::Comma)
      return Error("Unexpected token '%s'", Token.GetStr());
  }

  if (!bSeenSReg)
    return Error("Sampler register s# must be defined for each static sampler");

  return true;
}

bool RootSignatureParser::ParseFilter(DxilFilter &Filter) {
  TokenType Token;

  if (!GetAndMatchToken(Token, TokenType::filter))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;
  Token = m_pTokenizer->GetToken();

  switch (Token.GetType()) {
  case TokenType::FILTER_MIN_MAG_MIP_POINT:
    Filter = DxilFilter::MIN_MAG_MIP_POINT;
    break;
  case TokenType::FILTER_MIN_MAG_POINT_MIP_LINEAR:
    Filter = DxilFilter::MIN_MAG_POINT_MIP_LINEAR;
    break;
  case TokenType::FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT:
    Filter = DxilFilter::MIN_POINT_MAG_LINEAR_MIP_POINT;
    break;
  case TokenType::FILTER_MIN_POINT_MAG_MIP_LINEAR:
    Filter = DxilFilter::MIN_POINT_MAG_MIP_LINEAR;
    break;
  case TokenType::FILTER_MIN_LINEAR_MAG_MIP_POINT:
    Filter = DxilFilter::MIN_LINEAR_MAG_MIP_POINT;
    break;
  case TokenType::FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR:
    Filter = DxilFilter::MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    break;
  case TokenType::FILTER_MIN_MAG_LINEAR_MIP_POINT:
    Filter = DxilFilter::MIN_MAG_LINEAR_MIP_POINT;
    break;
  case TokenType::FILTER_MIN_MAG_MIP_LINEAR:
    Filter = DxilFilter::MIN_MAG_MIP_LINEAR;
    break;
  case TokenType::FILTER_ANISOTROPIC:
    Filter = DxilFilter::ANISOTROPIC;
    break;
  case TokenType::FILTER_COMPARISON_MIN_MAG_MIP_POINT:
    Filter = DxilFilter::COMPARISON_MIN_MAG_MIP_POINT;
    break;
  case TokenType::FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR:
    Filter = DxilFilter::COMPARISON_MIN_MAG_POINT_MIP_LINEAR;
    break;
  case TokenType::FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT:
    Filter = DxilFilter::COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT;
    break;
  case TokenType::FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR:
    Filter = DxilFilter::COMPARISON_MIN_POINT_MAG_MIP_LINEAR;
    break;
  case TokenType::FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT:
    Filter = DxilFilter::COMPARISON_MIN_LINEAR_MAG_MIP_POINT;
    break;
  case TokenType::FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR:
    Filter = DxilFilter::COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    break;
  case TokenType::FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT:
    Filter = DxilFilter::COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    break;
  case TokenType::FILTER_COMPARISON_MIN_MAG_MIP_LINEAR:
    Filter = DxilFilter::COMPARISON_MIN_MAG_MIP_LINEAR;
    break;
  case TokenType::FILTER_COMPARISON_ANISOTROPIC:
    Filter = DxilFilter::COMPARISON_ANISOTROPIC;
    break;
  case TokenType::FILTER_MINIMUM_MIN_MAG_MIP_POINT:
    Filter = DxilFilter::MINIMUM_MIN_MAG_MIP_POINT;
    break;
  case TokenType::FILTER_MINIMUM_MIN_MAG_POINT_MIP_LINEAR:
    Filter = DxilFilter::MINIMUM_MIN_MAG_POINT_MIP_LINEAR;
    break;
  case TokenType::FILTER_MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT:
    Filter = DxilFilter::MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT;
    break;
  case TokenType::FILTER_MINIMUM_MIN_POINT_MAG_MIP_LINEAR:
    Filter = DxilFilter::MINIMUM_MIN_POINT_MAG_MIP_LINEAR;
    break;
  case TokenType::FILTER_MINIMUM_MIN_LINEAR_MAG_MIP_POINT:
    Filter = DxilFilter::MINIMUM_MIN_LINEAR_MAG_MIP_POINT;
    break;
  case TokenType::FILTER_MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR:
    Filter = DxilFilter::MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    break;
  case TokenType::FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT:
    Filter = DxilFilter::MINIMUM_MIN_MAG_LINEAR_MIP_POINT;
    break;
  case TokenType::FILTER_MINIMUM_MIN_MAG_MIP_LINEAR:
    Filter = DxilFilter::MINIMUM_MIN_MAG_MIP_LINEAR;
    break;
  case TokenType::FILTER_MINIMUM_ANISOTROPIC:
    Filter = DxilFilter::MINIMUM_ANISOTROPIC;
    break;
  case TokenType::FILTER_MAXIMUM_MIN_MAG_MIP_POINT:
    Filter = DxilFilter::MAXIMUM_MIN_MAG_MIP_POINT;
    break;
  case TokenType::FILTER_MAXIMUM_MIN_MAG_POINT_MIP_LINEAR:
    Filter = DxilFilter::MAXIMUM_MIN_MAG_POINT_MIP_LINEAR;
    break;
  case TokenType::FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT:
    Filter = DxilFilter::MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT;
    break;
  case TokenType::FILTER_MAXIMUM_MIN_POINT_MAG_MIP_LINEAR:
    Filter = DxilFilter::MAXIMUM_MIN_POINT_MAG_MIP_LINEAR;
    break;
  case TokenType::FILTER_MAXIMUM_MIN_LINEAR_MAG_MIP_POINT:
    Filter = DxilFilter::MAXIMUM_MIN_LINEAR_MAG_MIP_POINT;
    break;
  case TokenType::FILTER_MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR:
    Filter = DxilFilter::MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    break;
  case TokenType::FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT:
    Filter = DxilFilter::MAXIMUM_MIN_MAG_LINEAR_MIP_POINT;
    break;
  case TokenType::FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR:
    Filter = DxilFilter::MAXIMUM_MIN_MAG_MIP_LINEAR;
    break;
  case TokenType::FILTER_MAXIMUM_ANISOTROPIC:
    Filter = DxilFilter::MAXIMUM_ANISOTROPIC;
    break;
  default:
    return Error("Unexpected filter value: '%s'.", Token.GetStr());
  }

  return true;
}

bool RootSignatureParser::ParseTextureAddressMode(
    DxilTextureAddressMode &AddressMode) {
  TokenType Token = m_pTokenizer->GetToken();
  DXASSERT_NOMSG(Token.GetType() == TokenType::addressU ||
                 Token.GetType() == TokenType::addressV ||
                 Token.GetType() == TokenType::addressW);
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;
  Token = m_pTokenizer->GetToken();

  switch (Token.GetType()) {
  case TokenType::TEXTURE_ADDRESS_WRAP:
    AddressMode = DxilTextureAddressMode::Wrap;
    break;
  case TokenType::TEXTURE_ADDRESS_MIRROR:
    AddressMode = DxilTextureAddressMode::Mirror;
    break;
  case TokenType::TEXTURE_ADDRESS_CLAMP:
    AddressMode = DxilTextureAddressMode::Clamp;
    break;
  case TokenType::TEXTURE_ADDRESS_BORDER:
    AddressMode = DxilTextureAddressMode::Border;
    break;
  case TokenType::TEXTURE_ADDRESS_MIRROR_ONCE:
    AddressMode = DxilTextureAddressMode::MirrorOnce;
    break;
  default:
    return Error("Unexpected texture address mode value: '%s'.",
                 Token.GetStr());
  }

  return true;
}

bool RootSignatureParser::ParseMipLODBias(float &MipLODBias) {
  TokenType Token;

  if (!GetAndMatchToken(Token, TokenType::mipLODBias))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;
  if (!ParseFloat(MipLODBias))
    return false;

  return true;
}

bool RootSignatureParser::ParseMaxAnisotropy(uint32_t &MaxAnisotropy) {
  TokenType Token;

  if (!GetAndMatchToken(Token, TokenType::maxAnisotropy))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;
  if (!GetAndMatchToken(Token, TokenType::NumberU32))
    return false;
  MaxAnisotropy = Token.GetU32Value();

  return true;
}

bool RootSignatureParser::ParseComparisonFunction(
    DxilComparisonFunc &ComparisonFunc) {
  TokenType Token;

  if (!GetAndMatchToken(Token, TokenType::comparisonFunc))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;
  Token = m_pTokenizer->GetToken();

  switch (Token.GetType()) {
  case TokenType::COMPARISON_NEVER:
    ComparisonFunc = DxilComparisonFunc::Never;
    break;
  case TokenType::COMPARISON_LESS:
    ComparisonFunc = DxilComparisonFunc::Less;
    break;
  case TokenType::COMPARISON_EQUAL:
    ComparisonFunc = DxilComparisonFunc::Equal;
    break;
  case TokenType::COMPARISON_LESS_EQUAL:
    ComparisonFunc = DxilComparisonFunc::LessEqual;
    break;
  case TokenType::COMPARISON_GREATER:
    ComparisonFunc = DxilComparisonFunc::Greater;
    break;
  case TokenType::COMPARISON_NOT_EQUAL:
    ComparisonFunc = DxilComparisonFunc::NotEqual;
    break;
  case TokenType::COMPARISON_GREATER_EQUAL:
    ComparisonFunc = DxilComparisonFunc::GreaterEqual;
    break;
  case TokenType::COMPARISON_ALWAYS:
    ComparisonFunc = DxilComparisonFunc::Always;
    break;
  default:
    return Error("Unexpected texture address mode value: '%s'.",
                 Token.GetStr());
  }
  return true;
}

bool RootSignatureParser::ParseBorderColor(DxilStaticBorderColor &BorderColor) {
  TokenType Token;

  if (!GetAndMatchToken(Token, TokenType::borderColor))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;
  Token = m_pTokenizer->GetToken();

  switch (Token.GetType()) {
  case TokenType::STATIC_BORDER_COLOR_TRANSPARENT_BLACK:
    BorderColor = DxilStaticBorderColor::TransparentBlack;
    break;
  case TokenType::STATIC_BORDER_COLOR_OPAQUE_BLACK:
    BorderColor = DxilStaticBorderColor::OpaqueBlack;
    break;
  case TokenType::STATIC_BORDER_COLOR_OPAQUE_WHITE:
    BorderColor = DxilStaticBorderColor::OpaqueWhite;
    break;
  case TokenType::STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT:
    BorderColor = DxilStaticBorderColor::OpaqueBlackUint;
    break;
  case TokenType::STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT:
    BorderColor = DxilStaticBorderColor::OpaqueWhiteUint;
    break;
  default:
    return Error("Unexpected texture address mode value: '%s'.",
                 Token.GetStr());
  }

  return true;
}

bool RootSignatureParser::ParseMinLOD(float &MinLOD) {
  TokenType Token;

  if (!GetAndMatchToken(Token, TokenType::minLOD))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;
  if (!ParseFloat(MinLOD))
    return false;

  return true;
}

bool RootSignatureParser::ParseMaxLOD(float &MaxLOD) {
  TokenType Token;

  if (!GetAndMatchToken(Token, TokenType::maxLOD))
    return false;
  if (!GetAndMatchToken(Token, TokenType::EQ))
    return false;
  if (!ParseFloat(MaxLOD))
    return false;

  return true;
}

bool RootSignatureParser::ParseFloat(float &v) {
  TokenType Token = m_pTokenizer->GetToken();
  if (Token.GetType() == TokenType::NumberU32)
    v = (float)Token.GetU32Value();
  else if (Token.GetType() == TokenType::NumberI32)
    v = (float)Token.GetI32Value();
  else if (Token.GetType() == TokenType::NumberFloat)
    v = Token.GetFloatValue();
  else
    return Error("Expected float, found token '%s'", Token.GetStr());

  return true;
}

bool RootSignatureParser::MarkParameter(bool &bSeen, LPCSTR pName) {

  if (bSeen)
    return Error("Parameter '%s' can be specified only once", pName);

  bSeen = true;

  return true;
}

_Use_decl_annotations_ bool clang::ParseHLSLRootSignature(
    const char *pData, unsigned Len, hlsl::DxilRootSignatureVersion Ver,
    hlsl::DxilRootSignatureCompilationFlags Flags,
    hlsl::DxilVersionedRootSignatureDesc **ppDesc, SourceLocation Loc,
    clang::DiagnosticsEngine &Diags) {
  *ppDesc = nullptr;
  std::string OSStr;
  llvm::raw_string_ostream OS(OSStr);
  hlsl::RootSignatureTokenizer RST(pData, Len);
  hlsl::RootSignatureParser RSP(&RST, Ver, Flags, OS);
  if (RSP.Parse(ppDesc))
    return true;
  // Create diagnostic error message.
  OS.flush();
  if (OSStr.empty())
    Diags.Report(Loc, clang::diag::err_hlsl_rootsig) << "unexpected";
  else 
    Diags.Report(Loc, clang::diag::err_hlsl_rootsig) << OSStr.c_str();
  return false;
}

_Use_decl_annotations_ void
clang::ReportHLSLRootSigError(clang::DiagnosticsEngine &Diags,
                              clang::SourceLocation Loc, const char *pData,
                              unsigned Len) {
  Diags.Report(Loc, clang::diag::err_hlsl_rootsig) << StringRef(pData, Len);
  return;
}
