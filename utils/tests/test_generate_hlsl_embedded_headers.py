#!/usr/bin/env python3
# Copyright (C) Microsoft Corporation. All rights reserved.
# This file is distributed under the University of Illinois Open Source License.
# See LICENSE.TXT for details.
"""Unit tests for utils/generate_hlsl_embedded_headers.py."""

import os
import re
import subprocess
import sys
import tempfile
import unittest

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.normpath(
    os.path.join(THIS_DIR, "..", "generate_hlsl_embedded_headers.py"))
EMBED = os.path.normpath(os.path.join(THIS_DIR, "..", "embed_header.py"))

# Import the module so we can exercise its helpers directly.
sys.path.insert(0, os.path.dirname(GEN))
import generate_hlsl_embedded_headers as gen_mod  # noqa: E402


class HelperTests(unittest.TestCase):
    def test_path_to_namespace_simple(self):
        self.assertEqual(gen_mod.path_to_namespace("enable_if.h"),
                         "enable_if_h")

    def test_path_to_namespace_subdir(self):
        self.assertEqual(gen_mod.path_to_namespace("dx/linalg.h"),
                         "dx_linalg_h")

    def test_path_to_namespace_deep(self):
        self.assertEqual(
            gen_mod.path_to_namespace("vk/khr/cooperative_matrix.h"),
            "vk_khr_cooperative_matrix_h")

    def test_normalize_rel_path_strips_dot_slash(self):
        self.assertEqual(gen_mod.normalize_rel_path("./dx/linalg.h"),
                         "dx/linalg.h")

    def test_normalize_rel_path_converts_backslashes(self):
        self.assertEqual(gen_mod.normalize_rel_path("dx\\linalg.h"),
                         "dx/linalg.h")


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmp)

    def _embed(self, rel, content):
        inc_path = os.path.join(self.tmp, rel + ".inc")
        os.makedirs(os.path.dirname(inc_path), exist_ok=True)
        src_path = os.path.join(self.tmp, "src", rel)
        os.makedirs(os.path.dirname(src_path), exist_ok=True)
        with open(src_path, "wb") as f:
            f.write(content)
        subprocess.check_call([sys.executable, EMBED, src_path, inc_path])
        return inc_path

    def test_generates_namespaces_and_map(self):
        inc1 = self._embed("enable_if.h", b"// enable_if header\n")
        inc2 = self._embed("dx/linalg.h", b"// linalg header\n")
        out = os.path.join(self.tmp, "Embedded.cpp")
        subprocess.check_call([
            sys.executable, GEN,
            "--output", out,
            "--entry", "enable_if.h=" + inc1,
            "--entry", "dx/linalg.h=" + inc2,
        ])
        with open(out) as f:
            text = f.read()

        # Each header gets its own namespace.
        self.assertIn("namespace enable_if_h {", text)
        self.assertIn("namespace dx_linalg_h {", text)

        # Each embedded .inc is included inside its namespace.
        self.assertIn(inc1.replace("\\", "/"), text)
        self.assertIn(inc2.replace("\\", "/"), text)

        # The map function is defined in clang::hlsl with proper keys.
        self.assertIn(
            "const llvm::StringMap<llvm::StringRef> &getEmbeddedHeaders()",
            text)
        self.assertIn("\"enable_if.h\"", text)
        self.assertIn("enable_if_h::Data", text)
        self.assertIn("\"dx/linalg.h\"", text)
        self.assertIn("dx_linalg_h::Data", text)
        self.assertIn("namespace clang", text)
        self.assertIn("namespace hlsl", text)

    def test_entries_are_sorted_and_normalized(self):
        inc1 = self._embed("a.h", b"a")
        inc2 = self._embed("z/y.h", b"z")
        out = os.path.join(self.tmp, "Embedded.cpp")
        # Pass them out of order and with a leading "./".
        subprocess.check_call([
            sys.executable, GEN,
            "--output", out,
            "--entry", "z/y.h=" + inc2,
            "--entry", "./a.h=" + inc1,
        ])
        with open(out) as f:
            text = f.read()
        # "a.h" entry should appear before "z/y.h" in the map literal.
        a_pos = text.index("\"a.h\"")
        z_pos = text.index("\"z/y.h\"")
        self.assertLess(a_pos, z_pos)
        # Leading "./" must be stripped.
        self.assertNotIn("\"./a.h\"", text)


if __name__ == "__main__":
    unittest.main()
