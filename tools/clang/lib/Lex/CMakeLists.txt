# TODO: Add -maltivec when ARCH is PowerPC.

set(LLVM_LINK_COMPONENTS support)

# ----------------------------------------------------------------------------
# Embedded HLSL headers.
#
# Every file under tools/clang/lib/Headers/hlsl/ is converted at build time
# to a small C++ snippet (an llvm::StringRef "Data" definition) by
# utils/embed_header.py.  A second script then generates a single C++ source
# file that wraps each snippet in a uniquely-named namespace and exposes the
# whole collection through clang::hlsl::getEmbeddedHeaders() as a
# StringMap.  This compiled-in data lets the preprocessor satisfy angled
# #includes of the bundled headers without consulting the filesystem.
# ----------------------------------------------------------------------------

set(hlsl_headers_src_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/../Headers/hlsl")
set(hlsl_embedded_dir
    "${CMAKE_CURRENT_BINARY_DIR}/HLSLEmbeddedHeaders")
set(embed_header_script
    "${LLVM_MAIN_SRC_DIR}/utils/embed_header.py")
set(generate_embedded_headers_script
    "${LLVM_MAIN_SRC_DIR}/utils/generate_hlsl_embedded_headers.py")

# Recursively find every regular file under hlsl/.  Restrict to extensions
# that are actually used as headers; this avoids embedding LICENSE.txt,
# README.txt, and editor configuration files like .clang-format.
file(GLOB_RECURSE hlsl_embedded_inputs
     RELATIVE "${hlsl_headers_src_dir}"
     "${hlsl_headers_src_dir}/*.h"
     "${hlsl_headers_src_dir}/*.impl")

set(hlsl_embedded_inc_files)
set(hlsl_embedded_entries)
foreach(rel_path ${hlsl_embedded_inputs})
  set(src "${hlsl_headers_src_dir}/${rel_path}")
  set(inc "${hlsl_embedded_dir}/${rel_path}.inc")
  add_custom_command(
    OUTPUT "${inc}"
    DEPENDS "${src}" "${embed_header_script}"
    COMMAND "${Python3_EXECUTABLE}" "${embed_header_script}" "${src}" "${inc}"
    COMMENT "Embedding HLSL header ${rel_path}")
  list(APPEND hlsl_embedded_inc_files "${inc}")
  list(APPEND hlsl_embedded_entries "--entry" "${rel_path}=${inc}")
endforeach()

set(hlsl_embedded_source "${hlsl_embedded_dir}/HLSLEmbeddedHeaders.cpp")
add_custom_command(
  OUTPUT "${hlsl_embedded_source}"
  DEPENDS ${hlsl_embedded_inc_files} "${generate_embedded_headers_script}"
  COMMAND "${Python3_EXECUTABLE}" "${generate_embedded_headers_script}"
          --output "${hlsl_embedded_source}"
          ${hlsl_embedded_entries}
  COMMENT "Generating HLSL embedded headers source")

set_source_files_properties("${hlsl_embedded_source}"
                            PROPERTIES GENERATED TRUE)

add_clang_library(clangLex
  HeaderMap.cpp
  HeaderSearch.cpp
  HLSLMacroExpander.cpp
  Lexer.cpp
  LiteralSupport.cpp
  MacroArgs.cpp
  MacroInfo.cpp
  ModuleMap.cpp
  PPCaching.cpp
  PPCallbacks.cpp
  PPConditionalDirectiveRecord.cpp
  PPDirectives.cpp
  PPExpressions.cpp
  PPLexerChange.cpp
  PPMacroExpansion.cpp
  PTHLexer.cpp
  Pragma.cpp
  PreprocessingRecord.cpp
  Preprocessor.cpp
  PreprocessorLexer.cpp
  ScratchBuffer.cpp
  TokenConcatenation.cpp
  TokenLexer.cpp
  ${hlsl_embedded_source}

  LINK_LIBS
  clangBasic
  )
