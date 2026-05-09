#!/usr/bin/env python3
# Copyright (C) Microsoft Corporation. All rights reserved.
# This file is distributed under the University of Illinois Open Source License.
# See LICENSE.TXT for details.
"""Unit tests for utils/embed_header.py."""

import os
import re
import subprocess
import sys
import tempfile
import time
import unittest


THIS_DIR = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.normpath(os.path.join(THIS_DIR, "..", "embed_header.py"))


def run_script(input_bytes):
    """Run embed_header.py with input_bytes and return the generated text."""
    with tempfile.TemporaryDirectory() as d:
        ip = os.path.join(d, "in.bin")
        op = os.path.join(d, "out.inc")
        with open(ip, "wb") as f:
            f.write(input_bytes)
        subprocess.check_call([sys.executable, SCRIPT, ip, op])
        with open(op, "r", encoding="utf-8") as f:
            return f.read()


def extract_literal_value(generated):
    """Decode the C++ string literal back into raw bytes for round-trip checks."""
    m = re.search(r"llvm::StringRef Data =\s*(.*);\s*$", generated, re.DOTALL)
    assert m, "no Data declaration in:\n" + generated
    body = m.group(1).strip()
    pieces = re.findall(r'"((?:\\.|[^"\\])*)"', body)
    raw = "".join(pieces)
    out = bytearray()
    i = 0
    while i < len(raw):
        c = raw[i]
        if c != "\\":
            out.append(ord(c))
            i += 1
            continue
        nxt = raw[i + 1]
        if nxt == "n":
            out.append(0x0A); i += 2
        elif nxt == "r":
            out.append(0x0D); i += 2
        elif nxt == "t":
            out.append(0x09); i += 2
        elif nxt == "\\":
            out.append(0x5C); i += 2
        elif nxt == "\"":
            out.append(0x22); i += 2
        elif nxt.isdigit():
            j = i + 1
            digits = ""
            while j < len(raw) and len(digits) < 3 and raw[j] in "01234567":
                digits += raw[j]
                j += 1
            out.append(int(digits, 8))
            i = j
        else:
            raise AssertionError("unexpected escape: \\" + nxt)
    return bytes(out)


class EmbedHeaderTests(unittest.TestCase):
    def test_empty_file(self):
        gen = run_script(b"")
        self.assertIn("llvm::StringRef Data =", gen)
        self.assertEqual(extract_literal_value(gen), b"")

    def test_round_trip_text(self):
        content = b"hello\nworld\nline 3\n"
        gen = run_script(content)
        self.assertEqual(extract_literal_value(gen), content)

    def test_round_trip_special_chars(self):
        content = b"quote=\"\nbackslash=\\\nbell=\x07\nhigh=\xff\ntab=\t\n"
        gen = run_script(content)
        self.assertEqual(extract_literal_value(gen), content)

    def test_round_trip_binary(self):
        content = bytes(range(256))
        gen = run_script(content)
        self.assertEqual(extract_literal_value(gen), content)

    def test_one_string_per_line(self):
        content = b"a\nb\nc"
        gen = run_script(content)
        self.assertIn('"a\\n"', gen)
        self.assertIn('"b\\n"', gen)
        self.assertIn('"c"', gen)

    def test_idempotent_on_unchanged_input(self):
        with tempfile.TemporaryDirectory() as d:
            ip = os.path.join(d, "in.h")
            op = os.path.join(d, "out.inc")
            with open(ip, "wb") as f:
                f.write(b"abc\n")
            subprocess.check_call([sys.executable, SCRIPT, ip, op])
            t1 = os.path.getmtime(op)
            time.sleep(0.05)
            subprocess.check_call([sys.executable, SCRIPT, ip, op])
            t2 = os.path.getmtime(op)
            self.assertEqual(t1, t2)


if __name__ == "__main__":
    unittest.main()
