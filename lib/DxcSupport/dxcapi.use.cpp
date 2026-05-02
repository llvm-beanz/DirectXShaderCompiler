///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// dxcapi.use.cpp                                                            //
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
// This file is distributed under the University of Illinois Open Source     //
// License. See LICENSE.TXT for details.                                     //
//                                                                           //
// Provides support for DXC API users.                                       //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#include "dxc/Support/WinIncludes.h"

#include "dxc/Support/dxcapi.use.h"

#include "dxc/Support/FileIOHelper.h"
#include "dxc/Support/Global.h"
#include "dxc/Support/SharedLibAffix.h" // header generated during DXC build
#include "dxc/Support/Unicode.h"
#include "dxc/Support/WinFunctions.h"

namespace dxc {

const char *kDxCompilerLib =
    CMAKE_SHARED_LIBRARY_PREFIX "dxcompiler" CMAKE_SHARED_LIBRARY_SUFFIX;
const char *kDxilLib =
    CMAKE_SHARED_LIBRARY_PREFIX "dxil" CMAKE_SHARED_LIBRARY_SUFFIX;

#ifdef _WIN32
static void TrimEOL(char *pMsg) {
  char *pEnd = pMsg + strlen(pMsg);
  --pEnd;
  while (pEnd > pMsg && (*pEnd == '\r' || *pEnd == '\n')) {
    --pEnd;
  }
  pEnd[1] = '\0';
}

static std::string GetWin32ErrorMessage(DWORD err) {
  char formattedMsg[200];
  DWORD formattedMsgLen =
      FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, err, 0, formattedMsg, _countof(formattedMsg), 0);
  if (formattedMsgLen > 0 && formattedMsgLen < _countof(formattedMsg)) {
    TrimEOL(formattedMsg);
    return std::string(formattedMsg);
  }
  return std::string();
}
#else
static std::string GetWin32ErrorMessage(DWORD err) {
  // Since we use errno for handling messages, we use strerror to get the error
  // message.
  return std::string(std::strerror(err));
}
#endif // _WIN32

void IFT_Data(HRESULT hr, LPCWSTR data) {
  if (SUCCEEDED(hr))
    return;
  CW2A pData(data);
  std::string errMsg;
  if (HRESULT_IS_WIN32ERR(hr)) {
    DWORD err = HRESULT_AS_WIN32ERR(hr);
    errMsg.append(GetWin32ErrorMessage(err));
    if (data != nullptr) {
      errMsg.append(" ", 1);
    }
  }
  if (data != nullptr) {
    errMsg.append(pData);
  }
  throw ::hlsl::Exception(hr, errMsg);
}

void EnsureEnabled(DXCLibraryDllLoader &dxcSupport) {
  if (!dxcSupport.IsEnabled()) {
    IFT(dxcSupport.Initialize());
  }
}

void ReadFileIntoBlob(DllLoader &dxcSupport, LPCWSTR pFileName,
                      IDxcBlobEncoding **ppBlobEncoding) {
  CComPtr<IDxcLibrary> library;
  IFT(dxcSupport.CreateInstance(CLSID_DxcLibrary, &library));
  IFT_Data(library->CreateBlobFromFile(pFileName, nullptr, ppBlobEncoding),
           pFileName);
}

void WriteOperationErrorsToConsole(IDxcOperationResult *pResult,
                                   bool outputWarnings) {
  HRESULT status;
  IFT(pResult->GetStatus(&status));
  if (FAILED(status) || outputWarnings) {
    CComPtr<IDxcBlobEncoding> pErrors;
    IFT(pResult->GetErrorBuffer(&pErrors));
    if (pErrors.p != nullptr) {
      WriteBlobToConsole(pErrors, STD_ERROR_HANDLE);
    }
  }
}

void WriteOperationResultToConsole(IDxcOperationResult *pRewriteResult,
                                   bool outputWarnings) {
  WriteOperationErrorsToConsole(pRewriteResult, outputWarnings);

  CComPtr<IDxcBlob> pBlob;
  IFT(pRewriteResult->GetResult(&pBlob));
  WriteBlobToConsole(pBlob, STD_OUTPUT_HANDLE);
}

// Writes UTF-8-encoded bytes to a standard handle, preserving Unicode where
// possible.  Internally DXC operates on UTF-8 throughout, and the only
// boundary that requires wide characters is the Windows console.  On Windows,
// when the destination handle refers to a console, the bytes are converted to
// UTF-16 once and written via WriteConsoleW so that no character is lost to
// the (possibly non-UTF-8) console code page.  When the handle is redirected
// to a file or pipe -- or when running on platforms with native UTF-8 streams
// -- the UTF-8 bytes are written directly without any encoding conversion.
static void WriteUtf8BytesToStream(const char *pText, size_t charCount,
                                   DWORD streamType) {
  if (charCount == 0 || pText == nullptr)
    return;
  if (streamType != STD_OUTPUT_HANDLE && streamType != STD_ERROR_HANDLE)
    throw hlsl::Exception(E_INVALIDARG);

#ifdef _WIN32
  HANDLE hStream = GetStdHandle(streamType);
  if (hStream != INVALID_HANDLE_VALUE && hStream != nullptr) {
    DWORD mode = 0;
    if (GetConsoleMode(hStream, &mode)) {
      // Console destination: convert UTF-8 -> UTF-16 once and emit via
      // WriteConsoleW so that the active console code page does not cause
      // Unicode loss.
      std::wstring wide;
      if (Unicode::UTF8ToWideString(pText, charCount, &wide) && !wide.empty()) {
        DWORD written = 0;
        WriteConsoleW(hStream, wide.data(), (DWORD)wide.size(), &written,
                      nullptr);
      }
      return;
    }
    // Redirected handle: write the original UTF-8 bytes verbatim.
    DWORD written = 0;
    WriteFile(hStream, pText, (DWORD)charCount, &written, nullptr);
    return;
  }
#endif

  FILE *stream = (streamType == STD_ERROR_HANDLE) ? stderr : stdout;
  fwrite(pText, 1, charCount, stream);
}

static void WriteWideNullTermToConsole(const wchar_t *pText, DWORD streamType) {
  if (pText == nullptr)
    return;

#ifdef _WIN32
  // On Windows, if the destination is a console we can write the wide buffer
  // directly without going through any narrowing code page.
  HANDLE hStream = GetStdHandle(streamType);
  DWORD mode = 0;
  if (hStream != INVALID_HANDLE_VALUE && hStream != nullptr &&
      GetConsoleMode(hStream, &mode)) {
    size_t len = wcslen(pText);
    DWORD written = 0;
    if (len > 0)
      WriteConsoleW(hStream, pText, (DWORD)len, &written, nullptr);
    static const wchar_t newline = L'\n';
    WriteConsoleW(hStream, &newline, 1, &written, nullptr);
    return;
  }
#endif

  // Non-console / non-Windows path: convert to UTF-8 once and write the bytes
  // directly to the stream, then append a newline (matching prior behavior).
  std::string utf8;
  Unicode::WideToUTF8String(pText, &utf8);
  utf8.push_back('\n');
  WriteUtf8BytesToStream(utf8.data(), utf8.size(), streamType);
}

static HRESULT BlobToUtf8IfText(IDxcBlob *pBlob, IDxcBlobUtf8 **ppBlobUtf8) {
  CComPtr<IDxcBlobEncoding> pBlobEncoding;
  if (SUCCEEDED(pBlob->QueryInterface(&pBlobEncoding))) {
    BOOL known;
    UINT32 cp = 0;
    IFT(pBlobEncoding->GetEncoding(&known, &cp));
    if (known) {
      return hlsl::DxcGetBlobAsUtf8(pBlob, nullptr, ppBlobUtf8);
    }
  }
  return S_OK;
}

static HRESULT BlobToWideIfText(IDxcBlob *pBlob, IDxcBlobWide **ppBlobWide) {
  CComPtr<IDxcBlobEncoding> pBlobEncoding;
  if (SUCCEEDED(pBlob->QueryInterface(&pBlobEncoding))) {
    BOOL known;
    UINT32 cp = 0;
    IFT(pBlobEncoding->GetEncoding(&known, &cp));
    if (known) {
      return hlsl::DxcGetBlobAsWide(pBlob, nullptr, ppBlobWide);
    }
  }
  return S_OK;
}

void WriteBlobToConsole(IDxcBlob *pBlob, DWORD streamType) {
  if (pBlob == nullptr) {
    return;
  }

  // Try to get as UTF-16 or UTF-8
  BOOL known;
  UINT32 cp = 0;
  CComPtr<IDxcBlobEncoding> pBlobEncoding;
  IFT(pBlob->QueryInterface(&pBlobEncoding));
  IFT(pBlobEncoding->GetEncoding(&known, &cp));

  if (cp == DXC_CP_WIDE) {
    CComPtr<IDxcBlobWide> pWide;
    IFT(hlsl::DxcGetBlobAsWide(pBlob, nullptr, &pWide));
    WriteWideNullTermToConsole(pWide->GetStringPointer(), streamType);
  } else if (cp == CP_UTF8) {
    CComPtr<IDxcBlobUtf8> pUtf8;
    IFT(hlsl::DxcGetBlobAsUtf8(pBlob, nullptr, &pUtf8));
    WriteUtf8ToConsoleSizeT(pUtf8->GetStringPointer(), pUtf8->GetStringLength(),
                            streamType);
  }
}

void WriteBlobToFile(IDxcBlob *pBlob, LPCWSTR pFileName, UINT32 textCodePage) {
  if (pBlob == nullptr) {
    return;
  }

  CHandle file(CreateFileW(pFileName, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (file == INVALID_HANDLE_VALUE) {
    IFT_Data(HRESULT_FROM_WIN32(GetLastError()), pFileName);
  }

  WriteBlobToHandle(pBlob, file, pFileName, textCodePage);
}

void WriteBlobToHandle(IDxcBlob *pBlob, HANDLE hFile, LPCWSTR pFileName,
                       UINT32 textCodePage) {
  if (pBlob == nullptr) {
    return;
  }

  LPCVOID pPtr = pBlob->GetBufferPointer();
  SIZE_T size = pBlob->GetBufferSize();

  std::string BOM;
  CComPtr<IDxcBlobUtf8> pBlobUtf8;
  CComPtr<IDxcBlobWide> pBlobWide;
  if (textCodePage == DXC_CP_UTF8) {
    IFT_Data(BlobToUtf8IfText(pBlob, &pBlobUtf8), pFileName);
    if (pBlobUtf8) {
      pPtr = pBlobUtf8->GetStringPointer();
      size = pBlobUtf8->GetStringLength();
      // TBD: Should we write UTF-8 BOM?
      // BOM = "\xef\xbb\xbf"; // UTF-8
    }
  } else if (textCodePage == DXC_CP_WIDE) {
    IFT_Data(BlobToWideIfText(pBlob, &pBlobWide), pFileName);
    if (pBlobWide) {
      pPtr = pBlobWide->GetStringPointer();
      size = pBlobWide->GetStringLength() * sizeof(wchar_t);
      BOM = "\xff\xfe"; // UTF-16 LE
    }
  }

  IFT_Data(size > (SIZE_T)UINT32_MAX ? E_OUTOFMEMORY : S_OK, pFileName);

  DWORD written;

  if (!BOM.empty()) {
    if (FALSE ==
        WriteFile(hFile, BOM.data(), BOM.length(), &written, nullptr)) {
      IFT_Data(HRESULT_FROM_WIN32(GetLastError()), pFileName);
    }
  }

  if (FALSE == WriteFile(hFile, pPtr, (DWORD)size, &written, nullptr)) {
    IFT_Data(HRESULT_FROM_WIN32(GetLastError()), pFileName);
  }
}

void WriteUtf8ToConsole(const char *pText, int charCount, DWORD streamType) {
  if (charCount <= 0 || pText == nullptr) {
    return;
  }

  // The internal compiler representation is UTF-8.  Stream the bytes straight
  // through, performing a UTF-16 conversion only when the destination is a
  // Windows console (handled inside WriteUtf8BytesToStream).  This avoids the
  // historical UTF-8 -> UTF-16 -> narrow-codepage round trip that could
  // silently lose characters.
  WriteUtf8BytesToStream(pText, static_cast<size_t>(charCount), streamType);
  WriteUtf8BytesToStream("\n", 1, streamType);
}

void WriteUtf8ToConsoleSizeT(const char *pText, size_t charCount,
                             DWORD streamType) {
  if (charCount == 0 || pText == nullptr) {
    return;
  }

  WriteUtf8BytesToStream(pText, charCount, streamType);
  WriteUtf8BytesToStream("\n", 1, streamType);
}

} // namespace dxc
