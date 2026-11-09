//===- ShaderPrintfTest.cpp - Standalone shader printf tests
//---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dxc/shader_printf.h"
#include "gtest/gtest.h"
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;

void SetWord(Bytes &Data, std::size_t Offset, std::uint32_t Value) {
  for (unsigned I = 0; I != 4; ++I)
    Data[Offset + I] = static_cast<unsigned char>(Value >> (8 * I));
}

void AppendWord(Bytes &Data, std::uint32_t Value) {
  const std::size_t Offset = Data.size();
  Data.resize(Offset + 4);
  SetWord(Data, Offset, Value);
}

Bytes Container(const std::string &Strings) {
  Bytes Data(44, 0);
  std::memcpy(Data.data(), "DXBC", 4);
  Data[20] = 1;
  SetWord(Data, 28, 1);
  SetWord(Data, 32, 36);
  std::memcpy(Data.data() + 36, "STAB", 4);
  SetWord(Data, 40, static_cast<std::uint32_t>(Strings.size()));
  Data.insert(Data.end(), Strings.begin(), Strings.end());
  SetWord(Data, 24, static_cast<std::uint32_t>(Data.size()));
  return Data;
}

Bytes Container(const char *Format) {
  return Container(std::string(Format, std::strlen(Format) + 1));
}

Bytes Stream(std::initializer_list<std::uint32_t> Words) {
  Bytes Data(4);
  for (std::uint32_t Word : Words)
    AppendWord(Data, Word);
  SetWord(Data, 0, static_cast<std::uint32_t>(Data.size()));
  return Data;
}

std::error_code Print(const Bytes &Buffer, const Bytes &Shader,
                      std::ostream &Out) {
  return dxc::PrintDebugStream(Buffer.data(), Buffer.size(), Shader.data(),
                               Shader.size(), Out);
}

void ExpectError(const Bytes &Buffer, const Bytes &Shader,
                 const char *ErrorMessage) {
  std::ostringstream Out;
  const auto Result = Print(Buffer, Shader, Out);
  EXPECT_TRUE(static_cast<bool>(Result));
  EXPECT_EQ(ErrorMessage, Result.message());
  EXPECT_EQ(&dxc::ShaderPrintfErrorCategory(), &Result.category());
}

TEST(ShaderPrintf, FormatsEverySupportedConversion) {
  const Bytes Shader = Container("%d %u %f %x %% %q end%");
  const Bytes Buffer =
      Stream({1, 20, 0, 0x80000000u, 0xffffffffu, 0x3fc00000u, 0xab});
  std::ostringstream Out;
  const auto Result = Print(Buffer, Shader, Out);
  ASSERT_FALSE(static_cast<bool>(Result)) << Result.message();
  EXPECT_EQ("-2147483648 4294967295 1.500000e+00 0x0000ab % %q end%",
            Out.str());
}

TEST(ShaderPrintf, PreservesNumericFormatting) {
  const Bytes Shader = Container("%d %d %f %f %x %u");
  const Bytes Buffer = Stream(
      {1, 28, 0, 0xffffffffu, 0x7fffffffu, 0x80000000u, 0, 0xffffffffu, 10});
  std::ostringstream Out;
  ASSERT_FALSE(static_cast<bool>(Print(Buffer, Shader, Out)));
  EXPECT_EQ("-1 2147483647 -0.000000e+00 0.000000e+00 0xffffffff 10",
            Out.str());
}

TEST(ShaderPrintf, ReadsLanesRecordsOffsetsAndUnusedArguments) {
  const Bytes Shader = Container(std::string("\0done\0\0%u;\0\0", 12));
  const Bytes Buffer = Stream({2, 12, 7, 10, 999, 7, 20, 999, 2, 4, 1, 1});
  std::ostringstream Out;
  ASSERT_FALSE(static_cast<bool>(Print(Buffer, Shader, Out)));
  EXPECT_EQ("10;20;donedone", Out.str());
}

TEST(ShaderPrintf, ReadsDifferentFormatsWithinOneRecord) {
  const Bytes Shader = Container(std::string("u=%u;\0x=%x;\0", 12));
  const Bytes Buffer = Stream({2, 8, 0, 10, 6, 0xab});
  std::ostringstream Out;
  const auto Result = Print(Buffer, Shader, Out);
  ASSERT_FALSE(static_cast<bool>(Result)) << Result.message();
  EXPECT_EQ("u=10;x=0x0000ab;", Out.str());
}

TEST(ShaderPrintf, ReadsDifferentZeroArgumentFormatsWithinOneRecord) {
  const Bytes Shader = Container(std::string("\0first\0second\0", 14));
  const Bytes Buffer = Stream({3, 4, 1, 0, 7});
  std::ostringstream Out;
  ASSERT_FALSE(static_cast<bool>(Print(Buffer, Shader, Out)));
  EXPECT_EQ("firstsecond", Out.str());
}

TEST(ShaderPrintf, DecodesDocumentedRecordLayout) {
  const Bytes Shader = Container(std::string("\0%u:%f\n\0\0%u=%f\n\0\0", 17));
  const Bytes Buffer = Stream({2, 12, 1, 7, 0x3fc00000u, 9, 9, 0xc0000000u});
  EXPECT_EQ(36u, Buffer.size());
  std::ostringstream Out;
  const auto Result = Print(Buffer, Shader, Out);
  ASSERT_FALSE(static_cast<bool>(Result)) << Result.message();
  EXPECT_EQ("7:1.500000e+00\n9=-2.000000e+00\n", Out.str());
}

TEST(ShaderPrintf, HandlesEmptyStreamsAndZeroLaneTerminator) {
  const Bytes Shader = Container("hello");
  for (const Bytes &Buffer :
       {Bytes(4, 0), Stream({}), Stream({0}), Stream({0, 0xffffffffu})}) {
    std::ostringstream Out;
    ASSERT_FALSE(static_cast<bool>(Print(Buffer, Shader, Out)));
    EXPECT_EQ("", Out.str());
  }
}

TEST(ShaderPrintf, PrintsExactFitZeroArgumentAndEmptyMessages) {
  for (const char *Format : {"hello", ""}) {
    std::ostringstream Out;
    const auto Result = Print(Stream({1, 4, 0}), Container(Format), Out);
    ASSERT_FALSE(static_cast<bool>(Result)) << Result.message();
    EXPECT_EQ(Format, Out.str());
  }
}

TEST(ShaderPrintf, SupportsUnalignedInputs) {
  Bytes Shader = Container("%u");
  Bytes Buffer = Stream({1, 8, 0, 42});
  Shader.insert(Shader.begin(), 0xff);
  Buffer.insert(Buffer.begin(), 0xff);
  std::ostringstream Out;
  const auto Result =
      dxc::PrintDebugStream(Buffer.data() + 1, Buffer.size() - 1,
                            Shader.data() + 1, Shader.size() - 1, Out);
  ASSERT_FALSE(static_cast<bool>(Result)) << Result.message();
  EXPECT_EQ("42", Out.str());
}

TEST(ShaderPrintf, SkipsOtherContainerPartsAndValidatesAllParts) {
  Bytes Shader = Container("hello");
  Shader.insert(Shader.begin() + 36, 4, 0);
  SetWord(Shader, 28, 2);
  SetWord(Shader, 32, 40);
  SetWord(Shader, 36, static_cast<std::uint32_t>(Shader.size()));
  const std::size_t SecondPart = Shader.size();
  Shader.insert(Shader.end(), {'J', 'U', 'N', 'K'});
  AppendWord(Shader, 4);
  AppendWord(Shader, 0);
  SetWord(Shader, 24, static_cast<std::uint32_t>(Shader.size()));
  std::ostringstream Out;
  ASSERT_FALSE(static_cast<bool>(Print(Stream({1, 4, 0}), Shader, Out)));
  EXPECT_EQ("hello", Out.str());
  SetWord(Shader, SecondPart + 4, 0xffffffffu);
  ExpectError(Stream({}), Shader, "DXIL container part data is truncated");
}

TEST(ShaderPrintf, RejectsInvalidContainerHeaders) {
  const Bytes Buffer = Stream({});
  Bytes Shader = Container("x");
  Shader.resize(31);
  ExpectError(Buffer, Shader, "DXIL container header is truncated");
  Shader = Container("x");
  Shader[0] = 'X';
  ExpectError(Buffer, Shader, "Invalid DXIL container signature");
  Shader = Container("x");
  Shader[20] = 2;
  ExpectError(Buffer, Shader, "Unsupported DXIL container major version");
  for (std::uint32_t Size : {0u, 31u, 0xffffffffu}) {
    Shader = Container("x");
    SetWord(Shader, 24, Size);
    ExpectError(Buffer, Shader, "Invalid DXIL container size");
  }
  Shader = Container("x");
  SetWord(Shader, 28, 0xffffffffu);
  ExpectError(Buffer, Shader, "DXIL container part offset table is truncated");
}

TEST(ShaderPrintf, RejectsInvalidContainerParts) {
  const Bytes Buffer = Stream({});
  for (std::uint32_t Offset : {0u, 32u, 40u, 0xffffffffu}) {
    Bytes Shader = Container("x");
    SetWord(Shader, 32, Offset);
    ExpectError(Buffer, Shader, "Invalid DXIL container part offset");
  }
  Bytes Shader = Container("x");
  SetWord(Shader, 40, 0xffffffffu);
  ExpectError(Buffer, Shader, "DXIL container part data is truncated");
  Shader = Container("x");
  Shader[36] = 'X';
  ExpectError(Buffer, Shader, "No string table part found in DXIL container");
  ExpectError(Buffer, Container(std::string()),
              "No string table part found in DXIL container");
}

TEST(ShaderPrintf, RejectsInvalidStreamSizesAndHeaders) {
  const Bytes Shader = Container("x");
  for (std::size_t Size = 0; Size < 4; ++Size)
    ExpectError(Bytes(Size), Shader, "Debug stream byte count is truncated");
  for (std::uint32_t Size : {1u, 2u, 3u, 5u, 8u, 0xffffffffu}) {
    Bytes Buffer(4);
    SetWord(Buffer, 0, Size);
    ExpectError(Buffer, Shader, "Invalid debug stream byte count");
  }
  ExpectError(Stream({1}), Shader, "Debug stream record header is truncated");
}

TEST(ShaderPrintf, RejectsInvalidStringsAndArgumentSizes) {
  ExpectError(Stream({1, 4, 2}), Container("x"),
              "Debug stream string offset is out of bounds");
  ExpectError(Stream({1, 4, 0xffffffffu}), Container("x"),
              "Debug stream string offset is out of bounds");
  ExpectError(Stream({1, 4, 0}), Container(std::string("x")),
              "Debug stream format string is not null-terminated");
  for (const Bytes &Buffer :
       {Stream({1, 0}), Stream({1, 1, 0}), Stream({1, 3, 0}),
        Stream({1, 5, 0, 0}), Stream({1, 4}), Stream({1, 8, 0}),
        Stream({2, 8, 0, 1, 0}), Stream({0xffffffffu, 0xfffffffcu})})
    ExpectError(Buffer, Container("x"),
                "Debug stream arguments exceed the buffer");
  ExpectError(Stream({1, 4, 0}), Container("%u"),
              "Debug stream arguments exceed the declared lane size");
  ExpectError(Stream({2, 8, 0, 1, 0, 2}), Container("%u %u"),
              "Debug stream arguments exceed the declared lane size");
}

TEST(ShaderPrintf, ValidatesEachLanesStringAndArguments) {
  const Bytes Shader = Container(std::string("ok\0%u\0bad", 9));
  for (const auto &Test :
       {std::make_pair(0xffffffffu,
                       "Debug stream string offset is out of bounds"),
        std::make_pair(6u, "Debug stream format string is not null-terminated"),
        std::make_pair(
            3u, "Debug stream arguments exceed the declared lane size")}) {
    std::ostringstream Out;
    const auto Result = Print(Stream({2, 4, 0, Test.first}), Shader, Out);
    EXPECT_EQ(Test.second, Result.message());
    EXPECT_EQ("ok", Out.str());
  }
}

class GroupedNumbers : public std::numpunct<char> {
  char do_thousands_sep() const override { return ','; }
  std::string do_grouping() const override { return "\3"; }
  char do_decimal_point() const override { return ','; }
};

TEST(ShaderPrintf, DoesNotUseOrChangeOutputFormatting) {
  std::ostringstream Out;
  Out.imbue(std::locale(std::locale::classic(), new GroupedNumbers));
  Out << std::hex << std::showbase << std::uppercase << std::setprecision(2);
  Out.fill('*');
  Out.width(50);
  const auto Flags = Out.flags();
  const auto Locale = Out.getloc();
  ASSERT_FALSE(static_cast<bool>(
      Print(Stream({1, 12, 0, 1000, 0x3fc00000u}), Container("%u %f"), Out)));
  EXPECT_EQ("1000 1.500000e+00", Out.str());
  EXPECT_EQ(Flags, Out.flags());
  EXPECT_EQ(Locale, Out.getloc());
  EXPECT_EQ(2, Out.precision());
  EXPECT_EQ('*', Out.fill());
  EXPECT_EQ(50, Out.width());
}

class RejectWrites : public std::streambuf {
  std::streamsize xsputn(const char *, std::streamsize) override { return 0; }
};

TEST(ShaderPrintf, ReportsOutputErrors) {
  RejectWrites Sink;
  std::ostream Out(&Sink);
  const auto Result = Print(Stream({1, 4, 0}), Container("hello"), Out);
  EXPECT_TRUE(static_cast<bool>(Result));
  EXPECT_EQ(dxc::ShaderPrintfError::OutputError, Result);
  EXPECT_EQ(std::errc::io_error, Result);
  EXPECT_EQ("Failed to write debug stream output", Result.message());
  const auto EmptyResult = Print(Stream({}), Container("hello"), Out);
  EXPECT_TRUE(static_cast<bool>(EmptyResult));
}

TEST(ShaderPrintf, RejectsNullPointers) {
  const Bytes Shader = Container("x");
  const Bytes Buffer = Stream({});
  std::ostringstream Out;
  EXPECT_TRUE(static_cast<bool>(
      dxc::PrintDebugStream(nullptr, 4, Shader.data(), Shader.size(), Out)));
  EXPECT_TRUE(static_cast<bool>(
      dxc::PrintDebugStream(Buffer.data(), Buffer.size(), nullptr, 32, Out)));
}

TEST(ShaderPrintf, IntegratesWithStandardErrorCodes) {
  const std::error_code Error = dxc::ShaderPrintfError::InvalidStringOffset;
  EXPECT_TRUE(static_cast<bool>(Error));
  EXPECT_EQ(dxc::ShaderPrintfError::InvalidStringOffset, Error);
  EXPECT_EQ(Error, dxc::ShaderPrintfError::InvalidStringOffset);
  EXPECT_EQ(std::errc::invalid_argument, Error);
  EXPECT_EQ(Error, std::errc::invalid_argument);
  EXPECT_NE(dxc::ShaderPrintfError::InvalidByteCount, Error);
  EXPECT_NE(std::errc::io_error, Error);
  EXPECT_EQ("dxc.shader_printf", std::string(Error.category().name()));
  EXPECT_EQ("Debug stream string offset is out of bounds", Error.message());

  const std::system_error Exception(Error);
  EXPECT_EQ(Error, Exception.code());
  EXPECT_NE(std::string::npos,
            std::string(Exception.what()).find(Error.message()));
}

TEST(ShaderPrintf, MapsStandardErrorConditions) {
  using Error = dxc::ShaderPrintfError;
  for (Error Code :
       {Error::TruncatedContainerHeader, Error::InvalidContainerSignature,
        Error::InvalidContainerSize, Error::TruncatedPartOffsetTable,
        Error::InvalidPartOffset, Error::TruncatedPartData,
        Error::MissingStringTable, Error::TruncatedByteCount,
        Error::InvalidByteCount, Error::TruncatedRecordHeader,
        Error::ArgumentBufferOverflow, Error::InvalidStringOffset,
        Error::UnterminatedFormatString, Error::ArgumentLaneOverflow}) {
    const std::error_code CodeValue = Code;
    EXPECT_EQ(std::errc::invalid_argument, CodeValue);
  }
  const std::error_code Unsupported = Error::UnsupportedContainerVersion;
  EXPECT_EQ(std::errc::not_supported, Unsupported);
  const std::error_code Formatting = Error::FormattingError;
  EXPECT_EQ(std::errc::io_error, Formatting);
  const std::error_code TooLarge = Error::MessageTooLarge;
  EXPECT_EQ(std::errc::value_too_large, TooLarge);
}

TEST(ShaderPrintf, UsesStandardSuccessSemantics) {
  std::ostringstream Out;
  const auto Result = Print(Stream({}), Container("hello"), Out);
  EXPECT_FALSE(static_cast<bool>(Result));
  EXPECT_EQ(std::error_code(), Result);

  const std::error_code Success = dxc::ShaderPrintfError::Success;
  EXPECT_FALSE(static_cast<bool>(Success));
  EXPECT_EQ(std::error_condition(), Success.default_error_condition());
  EXPECT_EQ("Success", Success.message());
}

TEST(ShaderPrintf, HandlesUnknownErrorCodes) {
  const std::error_code Unknown(999, dxc::ShaderPrintfErrorCategory());
  EXPECT_EQ("Unknown shader printf error", Unknown.message());
  EXPECT_EQ(std::error_condition(999, dxc::ShaderPrintfErrorCategory()),
            Unknown.default_error_condition());
}

} // namespace
