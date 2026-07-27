#!/usr/bin/env python3
# Copyright (C) Microsoft Corporation. All rights reserved.
# This file is distributed under the University of Illinois Open Source License. See LICENSE.TXT for details.
#
# Parse gen_intrin_main.txt and print all HLSL intrinsic function names to
# stdout, scoped by class or namespace where applicable.
#
# Usage: python3 gen_intrin_names.py [path/to/gen_intrin_main.txt]

import re
import sys
import os


def namespace_to_scope(ns_name):
    """Convert a namespace name to an HLSL scope prefix.

    Mapping rules:
      'Intrinsics'    -> '' (global built-in functions, no qualifier)
      'XxxMethods'    -> 'Xxx' (object method namespace, strip 'Methods')
      anything else   -> keep as-is (e.g. 'VkIntrinsics', 'DxIntrinsics')
    """
    if ns_name == "Intrinsics":
        return ""
    if ns_name.endswith("Methods"):
        return ns_name[: -len("Methods")]
    return ns_name


# Tokens that look like identifiers but are parameter keywords, not function names.
_PARAM_KEYWORDS = frozenset(
    {"in", "out", "inout", "ref", "groupshared", "col_major", "row_major"}
)


def extract_func_name(line):
    """Extract the function name from a single intrinsic declaration line.

    Declaration format:
        <return_type> [[attrs]] <name>(<params>) [: <op>];

    Strategy:
      1. Remove the [[...]] attribute block.
      2. Take all text before the first '('.
      3. The function name is the last C++ identifier in that text.
    """
    # Remove the [[...]] attribute block (non-greedy, single-line).
    line_no_attrs = re.sub(r"\[\[.*?\]\]", "", line)

    paren_idx = line_no_attrs.find("(")
    if paren_idx == -1:
        return None

    before_paren = line_no_attrs[:paren_idx]

    # Collect all identifiers (including $ prefixed meta-types like $type1).
    tokens = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", before_paren)

    # Walk backwards to find the first token that is a real function name.
    for token in reversed(tokens):
        if token not in _PARAM_KEYWORDS:
            return token

    return None


def parse(filepath):
    """Parse gen_intrin_main.txt and yield (scope, func_name) pairs."""
    current_namespace = None

    with open(filepath, encoding="utf-8") as f:
        for line in f:
            line = line.strip()

            # Skip blank lines and line comments.
            if not line or line.startswith("//"):
                continue

            # Namespace open:  namespace Foo {
            m = re.match(r"^namespace\s+(\w+)\s*\{", line)
            if m:
                current_namespace = m.group(1)
                continue

            # Namespace close:  } namespace
            if re.match(r"^}\s*namespace", line):
                current_namespace = None
                continue

            # Only process lines that look like declarations (must contain '(').
            if "(" not in line:
                continue

            func_name = extract_func_name(line)
            if not func_name:
                continue

            scope = namespace_to_scope(current_namespace) if current_namespace else ""
            yield scope, func_name


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_path = os.path.join(script_dir, "gen_intrin_main.txt")
    filepath = sys.argv[1] if len(sys.argv) > 1 else default_path

    seen = set()
    for scope, func_name in parse(filepath):
        scoped_name = f"{scope}::{func_name}" if scope else func_name
        if scoped_name not in seen:
            seen.add(scoped_name)
            print(scoped_name)


if __name__ == "__main__":
    main()
