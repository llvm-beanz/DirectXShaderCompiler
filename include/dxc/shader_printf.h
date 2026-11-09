//===- shader_printf.h - Shader debug printing API ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Standalone C++ decoder for host applications reading dx::printf output.
// Copy this header into an application without linking DXC or LLVM.
//
//===----------------------------------------------------------------------===//

#ifndef DXC_SHADER_PRINTF_H
#define DXC_SHADER_PRINTF_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>

namespace dxc {

enum class ShaderPrintfError {
  Success = 0,
  TruncatedContainerHeader,
  InvalidContainerSignature,
  UnsupportedContainerVersion,
  InvalidContainerSize,
  TruncatedPartOffsetTable,
  InvalidPartOffset,
  TruncatedPartData,
  MissingStringTable,
  TruncatedByteCount,
  OutputError,
  InvalidByteCount,
  TruncatedRecordHeader,
  ArgumentBufferOverflow,
  InvalidStringOffset,
  UnterminatedFormatString,
  ArgumentLaneOverflow,
  FormattingError,
  MessageTooLarge
};

} // namespace dxc

namespace std {

template <> struct is_error_code_enum<dxc::ShaderPrintfError> : true_type {};

} // namespace std

namespace dxc {
namespace shader_printf_detail {

class ShaderPrintfCategory : public std::error_category {
public:
  const char *name() const noexcept override { return "dxc.shader_printf"; }

  std::string message(int Value) const override {
    switch (static_cast<ShaderPrintfError>(Value)) {
    case ShaderPrintfError::Success:
      return "Success";
    case ShaderPrintfError::TruncatedContainerHeader:
      return "DXIL container header is truncated";
    case ShaderPrintfError::InvalidContainerSignature:
      return "Invalid DXIL container signature";
    case ShaderPrintfError::UnsupportedContainerVersion:
      return "Unsupported DXIL container major version";
    case ShaderPrintfError::InvalidContainerSize:
      return "Invalid DXIL container size";
    case ShaderPrintfError::TruncatedPartOffsetTable:
      return "DXIL container part offset table is truncated";
    case ShaderPrintfError::InvalidPartOffset:
      return "Invalid DXIL container part offset";
    case ShaderPrintfError::TruncatedPartData:
      return "DXIL container part data is truncated";
    case ShaderPrintfError::MissingStringTable:
      return "No string table part found in DXIL container";
    case ShaderPrintfError::TruncatedByteCount:
      return "Debug stream byte count is truncated";
    case ShaderPrintfError::OutputError:
      return "Failed to write debug stream output";
    case ShaderPrintfError::InvalidByteCount:
      return "Invalid debug stream byte count";
    case ShaderPrintfError::TruncatedRecordHeader:
      return "Debug stream record header is truncated";
    case ShaderPrintfError::ArgumentBufferOverflow:
      return "Debug stream arguments exceed the buffer";
    case ShaderPrintfError::InvalidStringOffset:
      return "Debug stream string offset is out of bounds";
    case ShaderPrintfError::UnterminatedFormatString:
      return "Debug stream format string is not null-terminated";
    case ShaderPrintfError::ArgumentLaneOverflow:
      return "Debug stream arguments exceed the declared lane size";
    case ShaderPrintfError::FormattingError:
      return "Failed to format debug stream output";
    case ShaderPrintfError::MessageTooLarge:
      return "Debug stream message is too large to write";
    }
    return "Unknown shader printf error";
  }

  std::error_condition
  default_error_condition(int Value) const noexcept override {
    switch (static_cast<ShaderPrintfError>(Value)) {
    case ShaderPrintfError::Success:
      return {};
    case ShaderPrintfError::UnsupportedContainerVersion:
      return std::make_error_condition(std::errc::not_supported);
    case ShaderPrintfError::OutputError:
    case ShaderPrintfError::FormattingError:
      return std::make_error_condition(std::errc::io_error);
    case ShaderPrintfError::MessageTooLarge:
      return std::make_error_condition(std::errc::value_too_large);
    case ShaderPrintfError::TruncatedContainerHeader:
    case ShaderPrintfError::InvalidContainerSignature:
    case ShaderPrintfError::InvalidContainerSize:
    case ShaderPrintfError::TruncatedPartOffsetTable:
    case ShaderPrintfError::InvalidPartOffset:
    case ShaderPrintfError::TruncatedPartData:
    case ShaderPrintfError::MissingStringTable:
    case ShaderPrintfError::TruncatedByteCount:
    case ShaderPrintfError::InvalidByteCount:
    case ShaderPrintfError::TruncatedRecordHeader:
    case ShaderPrintfError::ArgumentBufferOverflow:
    case ShaderPrintfError::InvalidStringOffset:
    case ShaderPrintfError::UnterminatedFormatString:
    case ShaderPrintfError::ArgumentLaneOverflow:
      return std::make_error_condition(std::errc::invalid_argument);
    }
    return std::error_condition(Value, *this);
  }
};

} // namespace shader_printf_detail

inline const std::error_category &ShaderPrintfErrorCategory() noexcept {
  static const shader_printf_detail::ShaderPrintfCategory Category;
  return Category;
}

inline std::error_code make_error_code(ShaderPrintfError Error) noexcept {
  return {static_cast<int>(Error), ShaderPrintfErrorCategory()};
}

namespace shader_printf_detail {

// Both the container and the debug stream use little-endian byte order.
inline std::uint32_t ReadUInt32(const unsigned char *Data) {
  return std::uint32_t(Data[0]) | (std::uint32_t(Data[1]) << 8) |
         (std::uint32_t(Data[2]) << 16) | (std::uint32_t(Data[3]) << 24);
}

inline std::error_code FindStringTable(const unsigned char *Shader,
                                       std::size_t ShaderSize,
                                       const char *&StringTable,
                                       std::size_t &StringTableSize) {
  if (!Shader || ShaderSize < 32)
    return ShaderPrintfError::TruncatedContainerHeader;
  if (std::memcmp(Shader, "DXBC", 4) != 0)
    return ShaderPrintfError::InvalidContainerSignature;
  if (Shader[20] != 1 || Shader[21] != 0)
    return ShaderPrintfError::UnsupportedContainerVersion;

  const std::size_t ContainerSize = ReadUInt32(Shader + 24);
  if (ContainerSize < 32 || ContainerSize > ShaderSize ||
      ContainerSize > 0x80000000u)
    return ShaderPrintfError::InvalidContainerSize;
  const std::uint32_t PartCount = ReadUInt32(Shader + 28);
  if (PartCount > (ContainerSize - 32) / 4)
    return ShaderPrintfError::TruncatedPartOffsetTable;

  std::size_t PreviousEnd = 32 + std::size_t(PartCount) * 4;
  for (std::uint32_t I = 0; I < PartCount; ++I) {
    const std::size_t Offset = ReadUInt32(Shader + 32 + std::size_t(I) * 4);
    if (Offset < PreviousEnd || Offset > ContainerSize ||
        ContainerSize - Offset < 8)
      return ShaderPrintfError::InvalidPartOffset;
    const std::size_t PartSize = ReadUInt32(Shader + Offset + 4);
    if (PartSize > ContainerSize - Offset - 8)
      return ShaderPrintfError::TruncatedPartData;
    if (std::memcmp(Shader + Offset, "STAB", 4) == 0) {
      StringTable = reinterpret_cast<const char *>(Shader + Offset + 8);
      StringTableSize = PartSize;
    }
    PreviousEnd = Offset + 8 + PartSize;
  }
  if (!StringTable || StringTableSize == 0)
    return ShaderPrintfError::MissingStringTable;
  return {};
}

} // namespace shader_printf_detail

// PrintDebugStream
// /brief Prints the debug stream from a shader printf buffer to the provided
// std::ostream.
//
// /param Buffer Pointer to the shader printf buffer.
// /param BufferSize Size of the shader printf buffer in bytes.
// /param Shader Pointer to the shader containing the debug stream.
// /param ShaderSize Size of the shader in bytes.
// /param Out Output stream to print the debug stream to.
// /return std::error_code indicating success or failure.
// 
// Example usage:
//   auto Result = dxc::PrintDebugStream(Buffer, BufferSize, Shader, ShaderSize,
//                                      std::cout);
//   if (Result)
//     std::cerr << Result.message() << '\n';
inline std::error_code
PrintDebugStream(const void *Buffer, std::size_t BufferSize, const void *Shader,
                 std::size_t ShaderSize, std::ostream &Out) {
  static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                "Shader printf requires IEEE-754 binary32 floats");

  const char *StringTable = nullptr;
  std::size_t StringTableSize = 0;
  const auto ContainerResult = shader_printf_detail::FindStringTable(
      static_cast<const unsigned char *>(Shader), ShaderSize, StringTable,
      StringTableSize);
  if (ContainerResult)
    return ContainerResult;
  if (!Buffer || BufferSize < 4)
    return ShaderPrintfError::TruncatedByteCount;
  if (!Out)
    return ShaderPrintfError::OutputError;

  const auto *Bytes = static_cast<const unsigned char *>(Buffer);
  const std::size_t StreamSize = shader_printf_detail::ReadUInt32(Bytes);
  if (StreamSize == 0)
    return {};
  if (StreamSize < 4 || StreamSize > BufferSize || StreamSize % 4 != 0)
    return ShaderPrintfError::InvalidByteCount;

  std::size_t Position = 4;
  while (Position < StreamSize) {
    const std::uint32_t LaneCount =
        shader_printf_detail::ReadUInt32(Bytes + Position);
    if (LaneCount == 0)
      break;
    if (StreamSize - Position < 8)
      return ShaderPrintfError::TruncatedRecordHeader;
    const std::size_t LaneSize =
        shader_printf_detail::ReadUInt32(Bytes + Position + 4);
    Position += 8;
    if (LaneSize < 4 || LaneSize % 4 != 0 ||
        LaneSize > (StreamSize - Position) / LaneCount)
      return ShaderPrintfError::ArgumentBufferOverflow;

    for (std::uint32_t I = 0; I < LaneCount; ++I) {
      const std::size_t StringOffset =
          shader_printf_detail::ReadUInt32(Bytes + Position);
      if (StringOffset >= StringTableSize)
        return ShaderPrintfError::InvalidStringOffset;
      const char *Format = StringTable + StringOffset;
      const auto *Terminator = static_cast<const char *>(
          std::memchr(Format, '\0', StringTableSize - StringOffset));
      if (!Terminator)
        return ShaderPrintfError::UnterminatedFormatString;
      const std::size_t FormatSize =
          static_cast<std::size_t>(Terminator - Format);
      std::ostringstream Message;
      Message.imbue(std::locale::classic());
      Message << std::scientific << std::setprecision(6);
      std::size_t Argument = Position + 4;
      const std::size_t ArgumentEnd = Position + LaneSize;
      for (std::size_t S = 0; S < FormatSize; ++S) {
        if (Format[S] != '%' || S + 1 == FormatSize) {
          Message.put(Format[S]);
          continue;
        }
        const char Specifier = Format[++S];
        if (Specifier == '%') {
          Message.put('%');
          continue;
        }
        if (Specifier != 'd' && Specifier != 'u' && Specifier != 'f' &&
            Specifier != 'x') {
          Message.put('%');
          Message.put(Specifier);
          continue;
        }
        if (ArgumentEnd - Argument < 4)
          return ShaderPrintfError::ArgumentLaneOverflow;
        const std::uint32_t Value =
            shader_printf_detail::ReadUInt32(Bytes + Argument);
        Argument += 4;
        switch (Specifier) {
        case 'd':
          Message << (std::int64_t(Value) -
                      ((Value & 0x80000000u) ? 0x100000000LL : 0));
          break;
        case 'u':
          Message << Value;
          break;
        case 'f': {
          float FloatValue;
          std::memcpy(&FloatValue, &Value, sizeof(FloatValue));
          Message << FloatValue;
          break;
        }
        case 'x':
          Message << "0x" << std::hex << std::setw(6) << std::setfill('0')
                  << Value << std::dec;
          break;
        }
      }
      if (!Message)
        return ShaderPrintfError::FormattingError;
      const std::string Text = Message.str();
      if (Text.size() > static_cast<std::size_t>(
                            (std::numeric_limits<std::streamsize>::max)()))
        return ShaderPrintfError::MessageTooLarge;
      Out.write(Text.data(), static_cast<std::streamsize>(Text.size()));
      if (!Out)
        return ShaderPrintfError::OutputError;
      Position = ArgumentEnd;
    }
  }
  return {};
}

} // namespace dxc

#endif // DXC_SHADER_PRINTF_H
