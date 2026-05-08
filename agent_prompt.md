# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LVLM Coding Standards](llvm/docs/CodingStandards.rst).

Verify your changes by building and testing using the
cmake/caches/PredefinedParams.cmake file with CMake's -C flag to configure the
build. Test the compiler and runtime support with the targets: check-all.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository and commit it in its own commit when you're done.

# Request

DXC has a set of headers included in the tools/clang/lib/Headers/hlsl folder. We
need a portable way to include those files as data into the dxcompiler library.

First, generate a python script, utils/embed_header.py, which takes in the path to
an input file, and a path to an output file. The script should read the input
file and generate a file at the output which contains a single variable
declaration: `llvm::StringRef Data = ...` where the `...` is a properly escaped
multi-line string literal containing the contents of the input file.

Second, update the CMake build system to add build steps so that whenever any of
the headers in the tools/clang/lib/Headers/hlsl folder are updated, a
cooresponding header is re-generated using the embed_header script in the build
directory.

Third, generate a python script that generates a source file that includes
all the newly generated headers, wrapping each include in a namespace
declaration so that each Data variable ends up in a namespace named for the
include path starting with the subdirectory of `hlsl` where slashes and dots are
repalced with underscores (for example tools/clang/lib/Headers/hlsl/dx/linalg.h
would convert to the namespace dx_linalg_h). Add a method in this source file
that generates an llvm::StringMap mapping the relative path under
tools/clang/lib/Headers/hlsl (without a preceding . or path separator) to the
cooresponding StringRef.

Fourth, implement modify the preprocessor's header resolution so that when
including headers with angle brackets (<>) if the header name exactly matches
the header's relative path (without a preceding . or path separator) the
preprocessor should use the header from the global data inside the compiler
instead of searching the filesystem.
