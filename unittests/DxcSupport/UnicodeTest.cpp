//===- unittests/DxcSupport/UnicodeTest.cpp - UTF-8 helper tests ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Tests for the Unicode/console helpers in DxcSupport.  These guard the
// invariant that DXC keeps strings in UTF-8 right up to the platform
// boundary, and that round-tripping UTF-8 -> wide -> UTF-8 reproduces the
// original byte sequence (so console output cannot lose characters).
//
//===----------------------------------------------------------------------===//

#include "dxc/Support/Unicode.h"
#include "dxc/Support/WinIncludes.h"
#include "dxc/Support/dxcapi.use.h"
#include "gtest/gtest.h"

#include <cstdio>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace {

// "héllo wörld 🌍" — mixes 1, 2, and 4-byte UTF-8 sequences.
static const char kSampleUtf8[] =
    "h\xC3\xA9llo w\xC3\xB6rld \xF0\x9F\x8C\x8D";

TEST(UnicodeTest, Utf8WideRoundTrip) {
  std::wstring wide;
  ASSERT_TRUE(Unicode::UTF8ToWideString(kSampleUtf8, &wide));

  std::string utf8;
  ASSERT_TRUE(Unicode::WideToUTF8String(wide.c_str(), &utf8));
  EXPECT_EQ(std::string(kSampleUtf8), utf8);
}

TEST(UnicodeTest, Utf8WideRoundTripWithLength) {
  const size_t len = sizeof(kSampleUtf8) - 1;
  std::wstring wide;
  ASSERT_TRUE(Unicode::UTF8ToWideString(kSampleUtf8, len, &wide));

  std::string utf8;
  ASSERT_TRUE(Unicode::WideToUTF8String(wide.c_str(), wide.size(), &utf8));
  EXPECT_EQ(std::string(kSampleUtf8, len), utf8);
}

TEST(UnicodeTest, Utf8BufferToWideBufferRoundTrip) {
  wchar_t *wide = nullptr;
  size_t cWide = 0;
  ASSERT_TRUE(Unicode::UTF8BufferToWideBuffer(
      kSampleUtf8, /*cbUTF8=*/-1, &wide, &cWide));
  ASSERT_NE(wide, nullptr);
  ASSERT_GT(cWide, 0u);

  char *utf8 = nullptr;
  size_t cbUtf8 = 0;
  ASSERT_TRUE(
      Unicode::WideBufferToUTF8Buffer(wide, /*cWide=*/-1, &utf8, &cbUtf8));
  ASSERT_NE(utf8, nullptr);
  EXPECT_STREQ(kSampleUtf8, utf8);

  delete[] wide;
  delete[] utf8;
}

#ifndef _WIN32
// On non-Windows, the dxc::Write* helpers must produce the exact UTF-8 byte
// sequence on stdout/stderr — no narrowing through the C locale.  This guards
// the regression where dxc::WriteUtf8ToConsole performed a UTF-8 -> wide ->
// narrow round trip via the active locale, which silently dropped characters
// the locale could not encode.

class StdStreamRedirect {
public:
  StdStreamRedirect(FILE *stream) : Stream(stream), SavedFd(-1) {
    fflush(Stream);
    SavedFd = dup(fileno(Stream));
    int pipeFds[2];
    if (pipe(pipeFds) != 0) {
      SavedFd = -1;
      return;
    }
    ReadFd = pipeFds[0];
    WriteFd = pipeFds[1];
    dup2(WriteFd, fileno(Stream));
    close(WriteFd);
  }

  ~StdStreamRedirect() {
    if (SavedFd != -1) {
      fflush(Stream);
      dup2(SavedFd, fileno(Stream));
      close(SavedFd);
    }
    if (ReadFd != -1)
      close(ReadFd);
  }

  std::string Read() {
    fflush(Stream);
    std::string buf;
    char tmp[256];
    ssize_t n;
    // Non-blocking: we expect the writer side to have flushed already.
    int flags = fcntl(ReadFd, F_GETFL, 0);
    fcntl(ReadFd, F_SETFL, flags | O_NONBLOCK);
    while ((n = read(ReadFd, tmp, sizeof(tmp))) > 0)
      buf.append(tmp, tmp + n);
    return buf;
  }

private:
  FILE *Stream;
  int SavedFd = -1;
  int ReadFd = -1;
  int WriteFd = -1;
};

TEST(UnicodeTest, WriteUtf8ToConsolePreservesBytes) {
  StdStreamRedirect redirect(stdout);
  dxc::WriteUtf8ToConsoleSizeT(kSampleUtf8, sizeof(kSampleUtf8) - 1,
                               STD_OUTPUT_HANDLE);
  std::string captured = redirect.Read();
  // The helper appends a trailing newline.
  EXPECT_EQ(std::string(kSampleUtf8) + "\n", captured);
}

TEST(UnicodeTest, WriteUtf8ToConsoleErrorPath) {
  StdStreamRedirect redirect(stderr);
  dxc::WriteUtf8ToConsoleSizeT(kSampleUtf8, sizeof(kSampleUtf8) - 1,
                               STD_ERROR_HANDLE);
  std::string captured = redirect.Read();
  EXPECT_EQ(std::string(kSampleUtf8) + "\n", captured);
}
#endif // _WIN32

} // namespace
