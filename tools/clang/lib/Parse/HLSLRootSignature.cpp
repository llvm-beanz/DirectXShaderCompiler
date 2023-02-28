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
#include "llvm/Support/Errc.h"
#include "llvm/Support/raw_ostream.h"

#include <float.h>

using namespace llvm;
using namespace hlsl;

DEFINE_ENUM_FLAG_OPERATORS(DxilRootSignatureFlags)
DEFINE_ENUM_FLAG_OPERATORS(DxilRootDescriptorFlags)
DEFINE_ENUM_FLAG_OPERATORS(DxilDescriptorRangeFlags)
DEFINE_ENUM_FLAG_OPERATORS(DxilRootSignatureCompilationFlags)

static Error err(const Twine &S) {
  return make_error<StringError>(S, errc::invalid_argument);
}

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
                                       StringRef pKeyword, Token::Type Type) {
  // Tokens are case-insencitive to allow more flexibility for programmers
  if (pKeyword.equals_lower(StringRef(pBuf))) {
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
    DxilRootSignatureCompilationFlags CompilationFlags)
    : m_pTokenizer(pTokenizer), m_Version(DefaultVersion),
      m_CompilationFlags(CompilationFlags) {}

Error RootSignatureParser::Parse(
    DxilVersionedRootSignatureDesc **ppRootSignature) {

  DXASSERT(!((bool)(m_CompilationFlags &
                    DxilRootSignatureCompilationFlags::GlobalRootSignature) &&
             (bool)(m_CompilationFlags &
                    DxilRootSignatureCompilationFlags::LocalRootSignature)),
           "global and local cannot be both set");
  if (!ppRootSignature)
    return Error::success();
  return ParseRootSignature(ppRootSignature);
}

Error RootSignatureParser::GetAndMatchToken(TokenType &Token,
                                            TokenType::Type Type) {
  Token = m_pTokenizer->GetToken();
  if (Token.GetType() != Type)
    return err(Twine("Unexpected token '") + Token.GetStr() + "'");
  return Error::success();
}

Error RootSignatureParser::ParseRootSignature(
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
      if (bSeenFlags)
        return err("RootFlags cannot be specified more than once");
      if (auto Err = ParseRootSignatureFlags(pRS->Desc_1_1.Flags))
        return Err;
      bSeenFlags = true;

      break;

    case TokenType::RootConstants: {
      DxilRootParameter1 P;
      if (auto Err = ParseRootConstants(P))
        return Err;
      RSParameters.push_back(P);
      break;
    }

    case TokenType::CBV: {
      DxilRootParameter1 P;
      if (auto Err = ParseRootShaderResource(Token.GetType(), TokenType::BReg,
                                             DxilRootParameterType::CBV, P))
        return Err;
      RSParameters.push_back(P);
      break;
    }

    case TokenType::SRV: {
      DxilRootParameter1 P;
      if (auto Err = ParseRootShaderResource(Token.GetType(), TokenType::TReg,
                                             DxilRootParameterType::SRV, P))
        return Err;
      RSParameters.push_back(P);
      break;
    }

    case TokenType::UAV: {
      DxilRootParameter1 P;
      if (auto Err = ParseRootShaderResource(Token.GetType(), TokenType::UReg,
                                             DxilRootParameterType::UAV, P))
        return Err;
      RSParameters.push_back(P);
      break;
    }

    case TokenType::DescriptorTable: {
      DxilRootParameter1 P;
      if (auto Err = ParseRootDescriptorTable(P))
        return Err;
      RSParameters.push_back(P);
      break;
    }

    case TokenType::StaticSampler: {
      DxilStaticSamplerDesc P;
      if (auto Err = ParseStaticSampler(P))
        return Err;
      StaticSamplers.push_back(P);
      break;
    }

    default:
      return err(Twine("Unexpected token '") + Token.GetStr() +
                 "' when parsing root signature");
    }

    Token = m_pTokenizer->GetToken();
    if (Token.GetType() == TokenType::EOL)
      break;

    // Consume ','
    if (Token.GetType() != TokenType::Comma)
      return err(Twine("Expected ',', found: '") + Token.GetStr() + "'");

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
      return err("An unexpected exception occurred.");
    }
    hlsl::DeleteRootSignature(pRS);
    pRS = pRS1;
  }

  *ppRootSignature = pRS;
  ScopeCleanup.Fn = []() {};

  return Error::success();
}

Error RootSignatureParser::ParseRootSignatureFlags(
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

  if (auto Err = GetAndMatchToken(Token, TokenType::RootFlags))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::LParen))
    return Err;

  Flags = DxilRootSignatureFlags::None;

  Token = m_pTokenizer->PeekToken();
  if (Token.GetType() == TokenType::NumberU32) {
    if (auto Err = GetAndMatchToken(Token, TokenType::NumberU32))
      return Err;
    uint32_t n = Token.GetU32Value();
    if (n != 0)
      return err(Twine("Root signature flag values can only be 0 or flag enum "
                       "values, found: '") +
                 Token.GetStr() + "'");
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
          return err("LOCAL_ROOT_SIGNATURE flag used in global root signature");
        Flags |= DxilRootSignatureFlags::LocalRootSignature;
        break;
      case TokenType::CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED:
        Flags |= DxilRootSignatureFlags::CBVSRVUAVHeapDirectlyIndexed;
        break;
      case TokenType::SAMPLER_HEAP_DIRECTLY_INDEXED:
        Flags |= DxilRootSignatureFlags::SamplerHeapDirectlyIndexed;
        break;
      default:
        return err(Twine("Expected a root signature flag value, found: '") +
                   Token.GetStr() + "'");
      }

      Token = m_pTokenizer->PeekToken();
      if (Token.GetType() == TokenType::RParen)
        break;

      if (auto Err = GetAndMatchToken(Token, TokenType::OR))
        return Err;
    }
  }

  if (auto Err = GetAndMatchToken(Token, TokenType::RParen))
    return Err;

  return Error::success();
}

Error RootSignatureParser::ParseRootConstants(DxilRootParameter1 &P) {
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

  if (auto Err = GetAndMatchToken(Token, TokenType::RootConstants))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::LParen))
    return Err;

  for (;;) {
    Token = m_pTokenizer->PeekToken();

    switch (Token.GetType()) {
    case TokenType::num32BitConstants:
      if (auto Err = MarkParameter(bSeenNum32BitConstants, "num32BitConstants"))
        return Err;
      if (auto Err = ParseNum32BitConstants(P.Constants.Num32BitValues))
        return Err;
      break;
    case TokenType::BReg:
      if (auto Err = MarkParameter(bSeenBReg, "cbuffer register b#"))
        return Err;
      if (auto Err = ParseRegister(TokenType::BReg, P.Constants.ShaderRegister))
        return Err;
      break;
    case TokenType::space:
      if (auto Err = MarkParameter(bSeenSpace, "space"))
        return Err;
      if (auto Err = ParseSpace(P.Constants.RegisterSpace))
        return Err;
      break;
    case TokenType::visibility:
      if (auto Err = MarkParameter(bSeenVisibility, "visibility"))
        return Err;
      if (auto Err = ParseVisibility(P.ShaderVisibility))
        return Err;
      break;
    default:
      return err(Twine("Unexpected token '") + Token.GetStr() + "'");
      break;
    }

    Token = m_pTokenizer->GetToken();
    if (Token.GetType() == TokenType::RParen)
      break;
    else if (Token.GetType() != TokenType::Comma)
      return err(Twine("Unexpected token '") + Token.GetStr() + "'");
  }

  if (!bSeenNum32BitConstants)
    return err("num32BitConstants must be defined for each RootConstants");

  if (!bSeenBReg)
    return err(
        "Constant buffer register b# must be defined for each RootConstants");

  return Error::success();
}

Error RootSignatureParser::ParseRootShaderResource(
    TokenType::Type TokType, TokenType::Type RegType,
    DxilRootParameterType ResType, DxilRootParameter1 &P) {
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

  if (auto Err = GetAndMatchToken(Token, TokType))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::LParen))
    return Err;

  for (;;) {
    Token = m_pTokenizer->PeekToken();

    switch (Token.GetType()) {
    case TokenType::BReg:
    case TokenType::TReg:
    case TokenType::UReg:
      if (auto Err = MarkParameter(bSeenReg, "shader register"))
        return Err;
      if (auto Err = ParseRegister(RegType, P.Descriptor.ShaderRegister))
        return Err;
      break;
    case TokenType::flags:
      if (auto Err = MarkParameter(bSeenFlags, "flags"))
        return Err;
      if (auto Err = ParseRootDescFlags(P.Descriptor.Flags))
        return Err;
      break;
    case TokenType::space:
      if (auto Err = MarkParameter(bSeenSpace, "space"))
        return Err;
      if (auto Err = ParseSpace(P.Descriptor.RegisterSpace))
        return Err;
      break;
    case TokenType::visibility:
      if (auto Err = MarkParameter(bSeenVisibility, "visibility"))
        return Err;
      if (auto Err = ParseVisibility(P.ShaderVisibility))
        return Err;
      break;
    default:
      return err(Twine("Unexpected token '") + Token.GetStr() + "'");
      break;
    }

    Token = m_pTokenizer->GetToken();
    if (Token.GetType() == TokenType::RParen)
      break;
    else if (Token.GetType() != TokenType::Comma)
      return err(Twine("Unexpected token '") + Token.GetStr() + "'");
  }

  if (!bSeenReg)
    return err("shader register must be defined for each CBV/SRV/UAV");

  return Error::success();
}

Error RootSignatureParser::ParseRootDescriptorTable(DxilRootParameter1 &P) {
  // DescriptorTable( SRV(t2, numDescriptors = 6), UAV(u0, numDescriptors = 4)
  // [, visibility = SHADER_VISIBILITY_ALL ] )
  TokenType Token;
  memset(&P, 0, sizeof(P));
  DXASSERT(P.ShaderVisibility == DxilShaderVisibility::All,
           "else default isn't zero");
  P.ParameterType = DxilRootParameterType::DescriptorTable;
  bool bSeenVisibility = false;
  SmallVector<DxilDescriptorRange1, 4> Ranges;

  if (auto Err = GetAndMatchToken(Token, TokenType::DescriptorTable))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::LParen))
    return Err;

  for (;;) {
    Token = m_pTokenizer->PeekToken();

    switch (Token.GetType()) {
    case TokenType::CBV: {
      DxilDescriptorRange1 R;
      if (auto Err = ParseDescTableResource(Token.GetType(), TokenType::BReg,
                                            DxilDescriptorRangeType::CBV, R))
        return Err;
      Ranges.push_back(R);
      break;
    }
    case TokenType::SRV: {
      DxilDescriptorRange1 R;
      if (auto Err = ParseDescTableResource(Token.GetType(), TokenType::TReg,
                                            DxilDescriptorRangeType::SRV, R))
        return Err;
      Ranges.push_back(R);
      break;
    }
    case TokenType::UAV: {
      DxilDescriptorRange1 R;
      if (auto Err = ParseDescTableResource(Token.GetType(), TokenType::UReg,
                                            DxilDescriptorRangeType::UAV, R))
        return Err;
      Ranges.push_back(R);
      break;
    }
    case TokenType::Sampler: {
      DxilDescriptorRange1 R;
      if (auto Err =
              ParseDescTableResource(Token.GetType(), TokenType::SReg,
                                     DxilDescriptorRangeType::Sampler, R))
        return Err;
      Ranges.push_back(R);
      break;
    }
    case TokenType::visibility:
      if (auto Err = MarkParameter(bSeenVisibility, "visibility"))
        return Err;
      if (auto Err = ParseVisibility(P.ShaderVisibility))
        return Err;
      break;
    default:
      return err(Twine("Unexpected token '") + Token.GetStr() + "'");
      break;
    }

    Token = m_pTokenizer->GetToken();
    if (Token.GetType() == TokenType::RParen)
      break;
    else if (Token.GetType() != TokenType::Comma)
      return err(Twine("Unexpected token '") + Token.GetStr() + "'");
  }

  if (Ranges.size() > 0) {
    P.DescriptorTable.pDescriptorRanges =
        new DxilDescriptorRange1[Ranges.size()];
    memcpy(P.DescriptorTable.pDescriptorRanges, Ranges.data(),
           Ranges.size() * sizeof(DxilDescriptorRange1));
    P.DescriptorTable.NumDescriptorRanges = Ranges.size();
  }

  return Error::success();
}

Error RootSignatureParser::ParseDescTableResource(
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

  if (auto Err = GetAndMatchToken(Token, TokType))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::LParen))
    return Err;

  for (;;) {
    Token = m_pTokenizer->PeekToken();

    switch (Token.GetType()) {
    case TokenType::BReg:
    case TokenType::TReg:
    case TokenType::UReg:
    case TokenType::SReg:
      if (auto Err = MarkParameter(bSeenReg, "shader register"))
        return Err;
      if (auto Err = ParseRegister(RegType, R.BaseShaderRegister))
        return Err;
      break;
    case TokenType::numDescriptors:
      if (auto Err = MarkParameter(bSeenNumDescriptors, "numDescriptors"))
        return Err;
      if (auto Err = ParseNumDescriptors(R.NumDescriptors))
        return Err;
      break;
    case TokenType::space:
      if (auto Err = MarkParameter(bSeenSpace, "space"))
        return Err;
      if (auto Err = ParseSpace(R.RegisterSpace))
        return Err;
      break;
    case TokenType::flags:
      if (auto Err = MarkParameter(bSeenFlags, "flags"))
        return Err;
      if (auto Err = ParseDescRangeFlags(RangeType, R.Flags))
        return Err;
      break;
    case TokenType::offset:
      if (auto Err = MarkParameter(bSeenOffset, "offset"))
        return Err;
      if (auto Err = ParseOffset(R.OffsetInDescriptorsFromTableStart))
        return Err;
      break;
    default:
      return err(Twine("Unexpected token '") + Token.GetStr() + "'");
      break;
    }

    Token = m_pTokenizer->GetToken();
    if (Token.GetType() == TokenType::RParen)
      break;
    else if (Token.GetType() != TokenType::Comma)
      return err(Twine("Unexpected token '") + Token.GetStr() + "'");
  }

  if (!bSeenReg)
    return err("shader register must be defined for each CBV/SRV/UAV");

  return Error::success();
}

Error RootSignatureParser::ParseRegister(TokenType::Type RegType,
                                         uint32_t &Reg) {
  TokenType Token = m_pTokenizer->PeekToken();

  switch (Token.GetType()) {
  case TokenType::BReg:
    if (auto Err = GetAndMatchToken(Token, TokenType::BReg))
      return Err;
    break;
  case TokenType::TReg:
    if (auto Err = GetAndMatchToken(Token, TokenType::TReg))
      return Err;
    break;
  case TokenType::UReg:
    if (auto Err = GetAndMatchToken(Token, TokenType::UReg))
      return Err;
    break;
  case TokenType::SReg:
    if (auto Err = GetAndMatchToken(Token, TokenType::SReg))
      return Err;
    break;
  default:
    return err(
        Twine("Expected a register token (CBV, SRV, UAV, Sampler), found: '") +
        Token.GetStr() + "'");
  }

  if (Token.GetType() != RegType) {
    switch (RegType) {
    case TokenType::BReg:
      return err(Twine("Incorrect register type '") + Token.GetStr() +
                 "' in CBV (expected b#)");
    case TokenType::TReg:
      return err(Twine("Incorrect register type '") + Token.GetStr() +
                 "' in SRV (expected t#)");
    case TokenType::UReg:
      return err(Twine("Incorrect register type '") + Token.GetStr() +
                 "' in UAV (expected u#)");
    case TokenType::SReg:
      return err(Twine("Incorrect register type '") + Token.GetStr() +
                 "' in Sampler/StaticSampler (expected s#)");
    default:
      // Only Register types are relevant.
      break;
    }
  }

  Reg = Token.GetU32Value();

  return Error::success();
}

Error RootSignatureParser::ParseSpace(uint32_t &Space) {
  TokenType Token;

  if (auto Err = GetAndMatchToken(Token, TokenType::space))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::NumberU32))
    return Err;
  Space = Token.GetU32Value();

  return Error::success();
}

Error RootSignatureParser::ParseNumDescriptors(uint32_t &NumDescriptors) {
  TokenType Token;

  if (auto Err = GetAndMatchToken(Token, TokenType::numDescriptors))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;
  Token = m_pTokenizer->PeekToken();
  if (Token.GetType() == TokenType::unbounded) {
    if (auto Err = GetAndMatchToken(Token, TokenType::unbounded))
      return Err;
    NumDescriptors = UINT32_MAX;
  } else {
    if (auto Err = GetAndMatchToken(Token, TokenType::NumberU32))
      return Err;
    NumDescriptors = Token.GetU32Value();
  }

  return Error::success();
}

Error RootSignatureParser::ParseRootDescFlags(DxilRootDescriptorFlags &Flags) {
  // flags=DATA_VOLATILE | DATA_STATIC | DATA_STATIC_WHILE_SET_AT_EXECUTE
  TokenType Token;

  if (m_Version == DxilRootSignatureVersion::Version_1_0)
    return err("Root descriptor flags cannot be specified for root_sig_1_0");

  if (auto Err = GetAndMatchToken(Token, TokenType::flags))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;

  Flags = DxilRootDescriptorFlags::None;

  Token = m_pTokenizer->PeekToken();
  if (Token.GetType() == TokenType::NumberU32) {
    if (auto Err = GetAndMatchToken(Token, TokenType::NumberU32))
      return Err;
    uint32_t n = Token.GetU32Value();
    if (n != 0)
      return err(Twine("Root descriptor flag values can only be 0 or flag enum "
                       "values, found: '") +
                 Token.GetStr() + "'");
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
        return err(Twine("Expected a root descriptor flag value, found: '") +
                   Token.GetStr() + "'");
      }

      Token = m_pTokenizer->PeekToken();
      if (Token.GetType() == TokenType::RParen ||
          Token.GetType() == TokenType::Comma)
        break;

      if (auto Err = GetAndMatchToken(Token, TokenType::OR))
        return Err;
    }
  }
  return Error::success();
}

Error RootSignatureParser::ParseDescRangeFlags(
    DxilDescriptorRangeType, DxilDescriptorRangeFlags &Flags) {
  // flags=DESCRIPTORS_VOLATILE | DATA_VOLATILE | DATA_STATIC |
  // DATA_STATIC_WHILE_SET_AT_EXECUTE |
  // DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS

  TokenType Token;

  if (m_Version == DxilRootSignatureVersion::Version_1_0) {
    return err("Descriptor range flags cannot be specified for root_sig_1_0");
  }

  if (auto Err = GetAndMatchToken(Token, TokenType::flags))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;

  Flags = DxilDescriptorRangeFlags::None;

  Token = m_pTokenizer->PeekToken();
  if (Token.GetType() == TokenType::NumberU32) {
    if (auto Err = GetAndMatchToken(Token, TokenType::NumberU32))
      return Err;
    uint32_t n = Token.GetU32Value();
    if (n != 0) {
      return err(
          Twine("Descriptor range flag values can only be 0 or flag enum "
                "values, found: '") +
          Token.GetStr() + "'");
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
        return err(Twine("Expected a descriptor range flag value, found: '") +
                   Token.GetStr() + "'");
      }

      Token = m_pTokenizer->PeekToken();
      if (Token.GetType() == TokenType::RParen ||
          Token.GetType() == TokenType::Comma)
        break;

      if (auto Err = GetAndMatchToken(Token, TokenType::OR))
        return Err;
    }
  }

  return Error::success();
}

Error RootSignatureParser::ParseOffset(uint32_t &Offset) {
  TokenType Token;

  if (auto Err = GetAndMatchToken(Token, TokenType::offset))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;
  Token = m_pTokenizer->PeekToken();
  if (Token.GetType() == TokenType::DESCRIPTOR_RANGE_OFFSET_APPEND) {
    if (auto Err =
            GetAndMatchToken(Token, TokenType::DESCRIPTOR_RANGE_OFFSET_APPEND))
      return Err;
    Offset = DxilDescriptorRangeOffsetAppend;
  } else {
    if (auto Err = GetAndMatchToken(Token, TokenType::NumberU32))
      return Err;
    Offset = Token.GetU32Value();
  }

  return Error::success();
}

Error RootSignatureParser::ParseVisibility(DxilShaderVisibility &Vis) {
  TokenType Token;

  if (auto Err = GetAndMatchToken(Token, TokenType::visibility))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;
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
    return err(Twine("Unexpected visibility value: '") + Token.GetStr() + "'.");
  }

  return Error::success();
}

Error RootSignatureParser::ParseNum32BitConstants(uint32_t &NumConst) {
  TokenType Token;

  if (auto Err = GetAndMatchToken(Token, TokenType::num32BitConstants))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::NumberU32))
    return Err;
  NumConst = Token.GetU32Value();

  return Error::success();
}

Error RootSignatureParser::ParseStaticSampler(DxilStaticSamplerDesc &P) {
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

  if (auto Err = GetAndMatchToken(Token, TokenType::StaticSampler))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::LParen))
    return Err;

  for (;;) {
    Token = m_pTokenizer->PeekToken();

    switch (Token.GetType()) {
    case TokenType::filter:
      if (auto Err = MarkParameter(bSeenFilter, "filter"))
        return Err;
      if (auto Err = ParseFilter(P.Filter))
        return Err;
      break;
    case TokenType::addressU:
      if (auto Err = MarkParameter(bSeenAddressU, "addressU"))
        return Err;
      if (auto Err = ParseTextureAddressMode(P.AddressU))
        return Err;
      break;
    case TokenType::addressV:
      if (auto Err = MarkParameter(bSeenAddressV, "addressV"))
        return Err;
      if (auto Err = ParseTextureAddressMode(P.AddressV))
        return Err;
      break;
    case TokenType::addressW:
      if (auto Err = MarkParameter(bSeenAddressW, "addressW"))
        return Err;
      if (auto Err = ParseTextureAddressMode(P.AddressW))
        return Err;
      break;
    case TokenType::mipLODBias:
      if (auto Err = MarkParameter(bSeenMipLODBias, "mipLODBias"))
        return Err;
      if (auto Err = ParseMipLODBias(P.MipLODBias))
        return Err;
      break;
    case TokenType::maxAnisotropy:
      if (auto Err = MarkParameter(bSeenMaxAnisotropy, "maxAnisotropy"))
        return Err;
      if (auto Err = ParseMaxAnisotropy(P.MaxAnisotropy))
        return Err;
      break;
    case TokenType::comparisonFunc:
      if (auto Err = MarkParameter(bSeenComparisonFunc, "comparisonFunc"))
        return Err;
      if (auto Err = ParseComparisonFunction(P.ComparisonFunc))
        return Err;
      break;
    case TokenType::borderColor:
      if (auto Err = MarkParameter(bSeenBorderColor, "borderColor"))
        return Err;
      if (auto Err = ParseBorderColor(P.BorderColor))
        return Err;
      break;
    case TokenType::minLOD:
      if (auto Err = MarkParameter(bSeenMinLOD, "minLOD"))
        return Err;
      if (auto Err = ParseMinLOD(P.MinLOD))
        return Err;
      break;
    case TokenType::maxLOD:
      if (auto Err = MarkParameter(bSeenMaxLOD, "maxLOD"))
        return Err;
      if (auto Err = ParseMaxLOD(P.MaxLOD))
        return Err;
      break;
    case TokenType::SReg:
      if (auto Err = MarkParameter(bSeenSReg, "sampler register s#"))
        return Err;
      if (auto Err = ParseRegister(TokenType::SReg, P.ShaderRegister))
        return Err;
      break;
    case TokenType::space:
      if (auto Err = MarkParameter(bSeenSpace, "space"))
        return Err;
      if (auto Err = ParseSpace(P.RegisterSpace))
        return Err;
      break;
    case TokenType::visibility:
      if (auto Err = MarkParameter(bSeenVisibility, "visibility"))
        return Err;
      if (auto Err = ParseVisibility(P.ShaderVisibility))
        return Err;
      break;
    default:
      return err(Twine("Unexpected token '") + Token.GetStr() + "'");
    }

    Token = m_pTokenizer->GetToken();
    if (Token.GetType() == TokenType::RParen)
      break;
    else if (Token.GetType() != TokenType::Comma)
      return err(Twine("Unexpected token '") + Token.GetStr() + "'");
  }

  if (!bSeenSReg)
    return err("Sampler register s# must be defined for each static sampler");

  return Error::success();
}

Error RootSignatureParser::ParseFilter(DxilFilter &Filter) {
  TokenType Token;

  if (auto Err = GetAndMatchToken(Token, TokenType::filter))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;
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
    return err(Twine("Unexpected filter value: '") + Token.GetStr() + "'.");
  }

  return Error::success();
}

Error RootSignatureParser::ParseTextureAddressMode(
    DxilTextureAddressMode &AddressMode) {
  TokenType Token = m_pTokenizer->GetToken();
  DXASSERT_NOMSG(Token.GetType() == TokenType::addressU ||
                 Token.GetType() == TokenType::addressV ||
                 Token.GetType() == TokenType::addressW);
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;
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
    return err(Twine("Unexpected texture address mode value: '") +
               Token.GetStr() + "'.");
  }

  return Error::success();
}

Error RootSignatureParser::ParseMipLODBias(float &MipLODBias) {
  TokenType Token;

  if (auto Err = GetAndMatchToken(Token, TokenType::mipLODBias))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;
  if (auto Err = ParseFloat(MipLODBias))
    return Err;

  return Error::success();
}

Error RootSignatureParser::ParseMaxAnisotropy(uint32_t &MaxAnisotropy) {
  TokenType Token;

  if (auto Err = GetAndMatchToken(Token, TokenType::maxAnisotropy))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::NumberU32))
    return Err;
  MaxAnisotropy = Token.GetU32Value();

  return Error::success();
}

Error RootSignatureParser::ParseComparisonFunction(
    DxilComparisonFunc &ComparisonFunc) {
  TokenType Token;

  if (auto Err = GetAndMatchToken(Token, TokenType::comparisonFunc))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;
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
    return err(Twine("Unexpected texture address mode value: '") +
               Token.GetStr() + "'.");
  }
  return Error::success();
}

Error RootSignatureParser::ParseBorderColor(
    DxilStaticBorderColor &BorderColor) {
  TokenType Token;

  if (auto Err = GetAndMatchToken(Token, TokenType::borderColor))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;
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
    return err(Twine("Unexpected texture address mode value: '") +
               Token.GetStr() + "'.");
  }

  return Error::success();
}

Error RootSignatureParser::ParseMinLOD(float &MinLOD) {
  TokenType Token;

  if (auto Err = GetAndMatchToken(Token, TokenType::minLOD))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;
  if (auto Err = ParseFloat(MinLOD))
    return Err;

  return Error::success();
}

Error RootSignatureParser::ParseMaxLOD(float &MaxLOD) {
  TokenType Token;

  if (auto Err = GetAndMatchToken(Token, TokenType::maxLOD))
    return Err;
  if (auto Err = GetAndMatchToken(Token, TokenType::EQ))
    return Err;
  if (auto Err = ParseFloat(MaxLOD))
    return Err;

  return Error::success();
}

Error RootSignatureParser::ParseFloat(float &v) {
  TokenType Token = m_pTokenizer->GetToken();
  if (Token.GetType() == TokenType::NumberU32)
    v = (float)Token.GetU32Value();
  else if (Token.GetType() == TokenType::NumberI32)
    v = (float)Token.GetI32Value();
  else if (Token.GetType() == TokenType::NumberFloat)
    v = Token.GetFloatValue();
  else
    return err(Twine("Expected float, found token '") + Token.GetStr() + "'");

  return Error::success();
}

Error RootSignatureParser::MarkParameter(bool &bSeen, const char *pName) {

  if (bSeen)
    return err(Twine("Parameter '") + pName + "' can be specified only once");

  bSeen = true;

  return Error::success();
}

_Use_decl_annotations_ bool clang::ParseHLSLRootSignature(
    const char *pData, unsigned Len, hlsl::DxilRootSignatureVersion Ver,
    hlsl::DxilRootSignatureCompilationFlags Flags,
    hlsl::DxilVersionedRootSignatureDesc **ppDesc, SourceLocation Loc,
    clang::DiagnosticsEngine &Diags) {
  *ppDesc = nullptr;
  hlsl::RootSignatureTokenizer RST(pData, Len);
  hlsl::RootSignatureParser RSP(&RST, Ver, Flags);
  if (auto Err = RSP.Parse(ppDesc)) {
    std::string OSStr;
    raw_string_ostream OS(OSStr);
    logAllUnhandledErrors(std::move(Err), OS, "");

    if (OSStr.empty())
      Diags.Report(Loc, clang::diag::err_hlsl_rootsig) << "unexpected";
    else
      Diags.Report(Loc, clang::diag::err_hlsl_rootsig) << OSStr.c_str();
    return false;
  }
  return true;
}

_Use_decl_annotations_ void
clang::ReportHLSLRootSigError(clang::DiagnosticsEngine &Diags,
                              clang::SourceLocation Loc, const char *pData,
                              unsigned Len) {
  Diags.Report(Loc, clang::diag::err_hlsl_rootsig) << StringRef(pData, Len);
  return;
}
