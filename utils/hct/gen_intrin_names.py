#!/usr/bin/env python3
# Copyright (C) Microsoft Corporation. All rights reserved.
# This file is distributed under the University of Illinois Open Source License. See LICENSE.TXT for details.
#
# Parse gen_intrin_main.txt and print all HLSL intrinsic function signatures
# with all allowed parameter type permutations to stdout.
#
# Usage: python3 gen_intrin_names.py [path/to/gen_intrin_main.txt]

import argparse
import json
import re
import sys
import os
import itertools

# ---------------------------------------------------------------------------
# Base type name (as used in gen_intrin_main.txt) -> LICOMPTYPE constant.
# Mirrors db_hlsl.__init__'s base_types dict in hctdb.py.
# ---------------------------------------------------------------------------
BASE_TYPE_TO_LICOMPTYPE = {
    "bool":                     "LICOMPTYPE_BOOL",
    "int":                      "LICOMPTYPE_INT",
    "int32_only":               "LICOMPTYPE_INT32_ONLY",
    "int64_only":               "LICOMPTYPE_INT64_ONLY",
    "int16_t":                  "LICOMPTYPE_INT16",
    "uint":                     "LICOMPTYPE_UINT",
    "uint16_t":                 "LICOMPTYPE_UINT16",
    "u64":                      "LICOMPTYPE_UINT64",
    "any_int":                  "LICOMPTYPE_ANY_INT",
    "any_int32":                "LICOMPTYPE_ANY_INT32",
    "any_int64":                "LICOMPTYPE_ANY_INT64",
    "uint_only":                "LICOMPTYPE_UINT_ONLY",
    "int8_t4_packed":           "LICOMPTYPE_INT8_4PACKED",
    "uint8_t4_packed":          "LICOMPTYPE_UINT8_4PACKED",
    "float16_t":                "LICOMPTYPE_FLOAT16",
    "float":                    "LICOMPTYPE_FLOAT",
    "float32_only":             "LICOMPTYPE_FLOAT32_ONLY",
    "fldbl":                    "LICOMPTYPE_FLOAT_DOUBLE",
    "any_float":                "LICOMPTYPE_ANY_FLOAT",
    "float_like":               "LICOMPTYPE_FLOAT_LIKE",
    "double":                   "LICOMPTYPE_DOUBLE",
    "double_only":              "LICOMPTYPE_DOUBLE_ONLY",
    "numeric":                  "LICOMPTYPE_NUMERIC",
    "numeric16_only":           "LICOMPTYPE_NUMERIC16_ONLY",
    "numeric32":                "LICOMPTYPE_NUMERIC32",
    "numeric32_only":           "LICOMPTYPE_NUMERIC32_ONLY",
    "any":                      "LICOMPTYPE_ANY",
    "sampler1d":                "LICOMPTYPE_SAMPLER1D",
    "sampler2d":                "LICOMPTYPE_SAMPLER2D",
    "sampler3d":                "LICOMPTYPE_SAMPLER3D",
    "sampler_cube":             "LICOMPTYPE_SAMPLERCUBE",
    "sampler_cmp":              "LICOMPTYPE_SAMPLERCMP",
    "sampler":                  "LICOMPTYPE_SAMPLER",
    "any_sampler":              "LICOMPTYPE_ANY_SAMPLER",
    "resource":                 "LICOMPTYPE_RESOURCE",
    "ray_desc":                 "LICOMPTYPE_RAYDESC",
    "acceleration_struct":      "LICOMPTYPE_ACCELERATION_STRUCT",
    "triangle_positions":       "LICOMPTYPE_BUILTIN_TRIANGLE_POSITIONS",
    "udt":                      "LICOMPTYPE_USER_DEFINED_TYPE",
    "void":                     "LICOMPTYPE_VOID",
    "string":                   "LICOMPTYPE_STRING",
    "Texture2D":                "LICOMPTYPE_TEXTURE2D",
    "Texture2DArray":           "LICOMPTYPE_TEXTURE2DARRAY",
    "wave":                     "LICOMPTYPE_WAVE",
    "p32i8":                    "LICOMPTYPE_INT8_4PACKED",
    "p32u8":                    "LICOMPTYPE_UINT8_4PACKED",
    "any_int16or32":            "LICOMPTYPE_ANY_INT16_OR_32",
    "sint16or32_only":          "LICOMPTYPE_SINT16_OR_32_ONLY",
    "ByteAddressBuffer":        "LICOMPTYPE_BYTEADDRESSBUFFER",
    "RWByteAddressBuffer":      "LICOMPTYPE_RWBYTEADDRESSBUFFER",
    "NodeRecordOrUAV":          "LICOMPTYPE_NODE_RECORD_OR_UAV",
    "AnyNodeOutputRecord":      "LICOMPTYPE_ANY_NODE_OUTPUT_RECORD",
    "GroupNodeOutputRecords":   "LICOMPTYPE_GROUP_NODE_OUTPUT_RECORDS",
    "ThreadNodeOutputRecords":  "LICOMPTYPE_THREAD_NODE_OUTPUT_RECORDS",
    "DxHitObject":              "LICOMPTYPE_HIT_OBJECT",
    "LinAlgMatrix":             "LICOMPTYPE_LINALG_MATRIX",
    "VkBufferPointer":          "LICOMPTYPE_VK_BUFFER_POINTER",
    "RayQuery":                 "LICOMPTYPE_RAY_QUERY",
    "LinAlg":                   "LICOMPTYPE_LINALG",
}

# ---------------------------------------------------------------------------
# LICOMPTYPE -> list of concrete HLSL type names.
#
# Derived from the g_*CT arrays in SemaHLSL.cpp.
# AR_BASIC_LITERAL_FLOAT, AR_BASIC_LITERAL_INT, AR_BASIC_NOCAST, and
# AR_BASIC_UNKNOWN are omitted (they are implementation-only sentinels).
# AR_BASIC_FLOAT32_PARTIAL_PRECISION is surfaced to users as 'half'.
# ---------------------------------------------------------------------------
LICOMPTYPE_TO_TYPES = {
    # --- scalar numeric types ---
    # g_BoolCT
    "LICOMPTYPE_BOOL":           ["bool"],
    # g_IntCT
    "LICOMPTYPE_INT":            ["int"],
    # g_UIntCT
    "LICOMPTYPE_UINT":           ["uint"],
    # g_AnyIntCT  (AR_BASIC_INT32 first = default)
    "LICOMPTYPE_ANY_INT":        ["int", "int16_t", "uint", "uint16_t", "int64_t", "uint64_t"],
    # g_AnyInt32CT
    "LICOMPTYPE_ANY_INT32":      ["int", "uint"],
    # g_UIntOnlyCT
    "LICOMPTYPE_UINT_ONLY":      ["uint", "uint64_t"],
    # g_FloatCT
    "LICOMPTYPE_FLOAT":          ["float", "half"],
    # g_AnyFloatCT
    "LICOMPTYPE_ANY_FLOAT":      ["float", "half", "float16_t", "double",
                                   "min10float", "min16float"],
    # g_FloatLikeCT
    "LICOMPTYPE_FLOAT_LIKE":     ["float", "half", "float16_t", "min10float", "min16float"],
    # g_FloatDoubleCT
    "LICOMPTYPE_FLOAT_DOUBLE":   ["float", "half", "double"],
    # g_DoubleCT
    "LICOMPTYPE_DOUBLE":         ["double"],
    # g_DoubleOnlyCT
    "LICOMPTYPE_DOUBLE_ONLY":    ["double"],
    # g_NumericCT
    "LICOMPTYPE_NUMERIC":        ["float", "half", "float16_t", "double",
                                   "min10float", "min16float",
                                   "int16_t", "int", "uint16_t", "uint",
                                   "min12int", "min16int", "min16uint",
                                   "int64_t", "uint64_t"],
    # g_Numeric32CT
    "LICOMPTYPE_NUMERIC32":      ["float", "half", "int", "uint"],
    # g_Numeric32OnlyCT
    "LICOMPTYPE_NUMERIC32_ONLY": ["float", "half", "int", "uint"],
    # g_AnyCT
    "LICOMPTYPE_ANY":            ["float", "half", "float16_t", "double",
                                   "min10float", "min16float",
                                   "int16_t", "int", "uint16_t", "uint",
                                   "min12int", "min16int", "min16uint",
                                   "bool", "int64_t", "uint64_t"],
    # g_UInt64CT
    "LICOMPTYPE_UINT64":         ["uint64_t"],
    # g_Float16CT
    "LICOMPTYPE_FLOAT16":        ["float16_t"],
    # g_Int16CT
    "LICOMPTYPE_INT16":          ["int16_t"],
    # g_UInt16CT
    "LICOMPTYPE_UINT16":         ["uint16_t"],
    # g_Numeric16OnlyCT
    "LICOMPTYPE_NUMERIC16_ONLY": ["float16_t", "int16_t", "uint16_t"],
    # g_Int32OnlyCT
    "LICOMPTYPE_INT32_ONLY":     ["int", "uint"],
    # g_Float32OnlyCT
    "LICOMPTYPE_FLOAT32_ONLY":   ["float"],
    # g_Int64OnlyCT
    "LICOMPTYPE_INT64_ONLY":     ["uint64_t", "int64_t"],
    # g_AnyInt64CT
    "LICOMPTYPE_ANY_INT64":      ["int64_t", "uint64_t"],
    # g_Int8_4PackedCT
    "LICOMPTYPE_INT8_4PACKED":   ["int8_t4_packed"],
    # g_UInt8_4PackedCT
    "LICOMPTYPE_UINT8_4PACKED":  ["uint8_t4_packed"],
    # g_AnyInt16Or32CT
    "LICOMPTYPE_ANY_INT16_OR_32":   ["int", "uint", "int16_t", "uint16_t"],
    # g_SInt16Or32OnlyCT
    "LICOMPTYPE_SINT16_OR_32_ONLY": ["int", "int16_t"],
    # --- sampler / texture object types ---
    # g_Sampler1DCT
    "LICOMPTYPE_SAMPLER1D":      ["sampler1D"],
    # g_Sampler2DCT
    "LICOMPTYPE_SAMPLER2D":      ["sampler2D"],
    # g_Sampler3DCT
    "LICOMPTYPE_SAMPLER3D":      ["sampler3D"],
    # g_SamplerCUBECT
    "LICOMPTYPE_SAMPLERCUBE":    ["samplerCUBE"],
    # g_SamplerCmpCT
    "LICOMPTYPE_SAMPLERCMP":     ["SamplerComparisonState"],
    # g_SamplerCT
    "LICOMPTYPE_SAMPLER":        ["SamplerState"],
    # g_AnySamplerCT
    "LICOMPTYPE_ANY_SAMPLER":    ["SamplerState", "SamplerComparisonState"],
    # g_Texture2DCT
    "LICOMPTYPE_TEXTURE2D":      ["Texture2D<T>"],
    # g_Texture2DArrayCT
    "LICOMPTYPE_TEXTURE2DARRAY": ["Texture2DArray<T>"],
    # g_ResourceCT
    "LICOMPTYPE_RESOURCE":       ["Resource"],
    # g_ByteAddressBufferCT
    "LICOMPTYPE_BYTEADDRESSBUFFER":   ["ByteAddressBuffer"],
    # g_RWByteAddressBufferCT
    "LICOMPTYPE_RWBYTEADDRESSBUFFER": ["RWByteAddressBuffer"],
    # --- raytracing / misc object types ---
    # g_RayDescCT
    "LICOMPTYPE_RAYDESC":             ["RayDesc"],
    # g_AccelerationStructCT
    "LICOMPTYPE_ACCELERATION_STRUCT": ["RaytracingAccelerationStructure"],
    # g_UDTCT
    "LICOMPTYPE_USER_DEFINED_TYPE":   ["<udt>"],
    # g_StringCT
    "LICOMPTYPE_STRING":              ["string"],
    # g_WaveCT
    "LICOMPTYPE_WAVE":                ["wave"],
    # g_RayQueryCT
    "LICOMPTYPE_RAY_QUERY":           ["RayQuery<T>"],
    # g_DxHitObjectCT
    "LICOMPTYPE_HIT_OBJECT":          ["dx::HitObject"],
    # g_BuiltInTrianglePositionsCT
    "LICOMPTYPE_BUILTIN_TRIANGLE_POSITIONS": ["BuiltInTriangleIntersectionAttributes"],
    # --- work graph node types ---
    # g_NodeRecordOrUAVCT
    "LICOMPTYPE_NODE_RECORD_OR_UAV": [
        "DispatchNodeInputRecord<T>", "RWDispatchNodeInputRecord<T>",
        "GroupNodeInputRecords<T>",   "RWGroupNodeInputRecords<T>",
        "ThreadNodeInputRecord<T>",   "RWThreadNodeInputRecord<T>",
        "NodeOutput<T>", "ThreadNodeOutputRecords<T>", "GroupNodeOutputRecords<T>",
        "RWBuffer<T>", "RWTexture1D<T>", "RWTexture1DArray<T>",
        "RWTexture2D<T>", "RWTexture2DArray<T>", "RWTexture3D<T>",
        "RWStructuredBuffer<T>", "RWByteAddressBuffer", "AppendStructuredBuffer<T>",
    ],
    # g_AnyOutputRecordCT
    "LICOMPTYPE_ANY_NODE_OUTPUT_RECORD": [
        "GroupNodeOutputRecords<T>", "ThreadNodeOutputRecords<T>",
    ],
    # g_GroupNodeOutputRecordsCT
    "LICOMPTYPE_GROUP_NODE_OUTPUT_RECORDS":  ["GroupNodeOutputRecords<T>"],
    # g_ThreadNodeOutputRecordsCT
    "LICOMPTYPE_THREAD_NODE_OUTPUT_RECORDS": ["ThreadNodeOutputRecords<T>"],
    # --- linear algebra types ---
    # g_LinAlgMatrixCT
    "LICOMPTYPE_LINALG_MATRIX": ["LinAlgMatrix"],
    # g_LinAlgCT
    "LICOMPTYPE_LINALG": [
        "float16_t", "float", "half", "double",
        "uint16_t", "uint", "uint64_t",
        "int16_t",  "int",  "int64_t",
        "uint8_t4_packed", "int8_t4_packed",
    ],
    # --- SPIR-V types ---
    "LICOMPTYPE_VK_BUFFER_POINTER": ["vk::BufferPointer"],
    # --- void (not a real overload type) ---
    "LICOMPTYPE_VOID": [],
}


def namespace_to_scope(ns_name):
    """Convert a namespace name to an HLSL scope prefix.

    'Intrinsics'        -> '' (global HLSL built-in functions)
    'VkIntrinsics'      -> 'vk'
    'DxIntrinsics'      -> 'dx'
    'DxHitObjectMethods'-> 'dx::HitObject'
    'XxxMethods'        -> 'Xxx' (strip 'Methods', lowercase CUBE -> Cube)
    anything else       -> kept verbatim
    """
    if ns_name == "Intrinsics":
        return ""
    if ns_name == "VkIntrinsics":
        return "vk"
    if ns_name == "DxIntrinsics":
        return "dx"
    if ns_name == "DxHitObjectMethods":
        return "dx::HitObject"
    if ns_name.endswith("Methods"):
        scope = ns_name[: -len("Methods")]
        # TextureCUBE* -> TextureCube* (HLSL spells it with lowercase 'ube')
        scope = scope.replace("CUBE", "Cube")
        # Vk-prefixed types belong to the vk:: namespace
        if scope.startswith("Vk"):
            return "vk::" + scope[2:]
        return scope
    return ns_name


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _clean_brackets(s):
    """Replace commas inside angle brackets with '@'.

    Mirrors hctdb.py's bracket_cleanup_re so that parameter lists can be
    safely split on ',' without splitting inside $match<0, 1> etc.
    """
    result = []
    depth = 0
    for ch in s:
        if ch == "<":
            depth += 1
            result.append(ch)
        elif ch == ">":
            depth -= 1
            result.append(ch)
        elif ch == "," and depth > 0:
            result.append("@")
        else:
            result.append(ch)
    return "".join(result)


# Qualifiers shown in parameter output.
_DISPLAY_QUALS = {"out", "inout", "ref", "groupshared"}


def _format_quals(quals):
    return " ".join(q for q in quals if q in _DISPLAY_QUALS)


# ---------------------------------------------------------------------------
# Declaration-line parsing
# ---------------------------------------------------------------------------

# After _clean_brackets the line looks like:
#   ret_type [[attrs]] func_name(params) [: op] [;]
_DECL_RE = re.compile(
    r"^(.+?)\s+\[\[([^\]]*)\]\]\s+(\w+)\s*\(([^)]*)\)\s*(?::\s*\w+\s*)?;?\s*$"
)
_TYPEREF_RE = re.compile(r"^\$type(\d+)$")


def _parse_declaration_line(line):
    """Return (ret_type_cleaned, func_name, params_cleaned, is_static) or None."""
    cleaned = _clean_brackets(line.strip())
    m = _DECL_RE.match(cleaned)
    if not m:
        return None
    attrs = [a.strip() for a in m.group(2).split(",")]
    is_static = "static" in attrs
    return m.group(1).strip(), m.group(3), m.group(4).strip(), is_static


def _parse_params(params_cleaned):
    """Parse a bracket-cleaned param string into a list of dicts.

    Each dict: {quals, type_str, name, variadic}

    Following hctdb.py's process_arg():
      - Last whitespace-token  = parameter name
      - Second-to-last token   = type specifier ($typeN, $classT, or base type)
      - Remaining tokens       = qualifiers (in/out/inout/ref/$match<…> etc.)
    """
    if not params_cleaned:
        return []
    result = []
    for part in params_cleaned.split(","):
        part = part.strip()
        if not part:
            continue
        if part == "...":
            result.append({"quals": [], "type_str": "...", "name": "...", "variadic": True})
            continue
        tokens = part.split()
        if len(tokens) < 2:
            result.append({"quals": [], "type_str": tokens[0], "name": tokens[0], "variadic": False})
            continue
        result.append({
            "quals":    tokens[:-2],
            "type_str": tokens[-2],
            "name":     tokens[-1],
            "variadic": False,
        })
    return result


# ---------------------------------------------------------------------------
# Overload expansion
# ---------------------------------------------------------------------------

def _get_base_type(type_str):
    m = re.match(r"(\w+)", type_str)
    return m.group(1) if m else None


def _render_params(param_info, idx_to_lc, lc_to_type):
    """Render parameter list as a list of 'type name' strings."""
    def concrete(idx):
        lc = idx_to_lc.get(idx)
        return lc_to_type.get(lc, "T") if lc else "T"
    parts = []
    for (idx, dqual, name, _lc, _tref) in param_info:
        if name == "...":
            parts.append("...")
            continue
        t = concrete(idx)
        parts.append(f"{dqual} {t} {name}".strip() if dqual else f"{t} {name}")
    return parts


def _render_sig(scope, func_name, params_rendered, ret):
    """Build the flat signature string used for deduplication."""
    scoped = f"{scope}::{func_name}" if scope else func_name
    return f"{scoped}({', '.join(params_rendered)}) -> {ret}"


def expand_overloads(scope, func_name, is_static, ret_type_str, params):
    """Yield (sig_key, scope, func_name, is_static, params_rendered, ret) per overload.

    Type-expansion algorithm
    ------------------------
    1. Each parameter is given an *effective LICOMPTYPE* by:
         a. Looking up its explicit base type name, or
         b. Following a $typeN reference chain to a free parameter.
       $classT / $funcT parameters use a special 'T' sentinel (not expanded).

    2. Free parameters (those with explicit base types, not $typeN/$classT)
       that share the same LICOMPTYPE are treated as one type axis—they will
       always receive the same concrete type in every overload.  This matches
       the actual HLSL overload semantics where, e.g., all 'numeric' params
       in one intrinsic must resolve to the same scalar type.

    3. The cartesian product over distinct LICOMPTYPEs is enumerated; each
       combination produces one rendered signature.
    """

    # --- Step 1: build param_info list ---
    # Entries: [1-based-idx, display_qual, name, licomptype_or_None, typeref_or_None]
    # typeref: None  = free param (uses its own LICOMPTYPE)
    #          N > 0 = follows param N via $typeN
    #          -1    = $classT / $funcT (class/function template type)
    param_info = []
    for i, p in enumerate(params, start=1):
        dqual    = _format_quals(p["quals"])
        name     = p["name"]
        type_str = p["type_str"]

        if p["variadic"]:
            param_info.append([i, "", "...", None, None])
            continue

        m_ref = _TYPEREF_RE.match(type_str)
        if m_ref:
            param_info.append([i, dqual, name, None, int(m_ref.group(1))])
            continue

        if type_str in ("$classT", "$funcT", "$funcT2"):
            param_info.append([i, dqual, name, None, -1])
            continue

        base = _get_base_type(type_str)
        lc   = BASE_TYPE_TO_LICOMPTYPE.get(base) if base else None
        param_info.append([i, dqual, name, lc, None])

    # --- Step 2: resolve every param's effective LICOMPTYPE ---
    def resolve_lc(start):
        seen, idx = set(), start
        while True:
            if idx in seen:
                return None
            seen.add(idx)
            entry = next((e for e in param_info if e[0] == idx), None)
            if entry is None:
                return None
            lc, tref = entry[3], entry[4]
            if tref is None:
                return lc          # free param
            if tref == -1:
                return None        # class/func template
            idx = tref

    idx_to_lc = {e[0]: resolve_lc(e[0]) for e in param_info}

    # --- Step 3: collect unique free-param LICOMPTYPEs (in declaration order) ---
    free_lcs = []   # ordered list of unique non-None LICOMPTYPEs from free params
    lc_types = {}   # licomptype -> [concrete HLSL type names]
    for entry in param_info:
        lc, tref = entry[3], entry[4]
        if tref is not None or lc is None:
            continue
        if lc not in lc_types:
            types = LICOMPTYPE_TO_TYPES.get(lc, [])
            lc_types[lc] = types if types else [lc.replace("LICOMPTYPE_", "").lower()]
            free_lcs.append(lc)

    # --- Step 4: cartesian product and render ---
    if not free_lcs:
        params_r = _render_params(param_info, idx_to_lc, {})
        ret = _resolve_ret(ret_type_str, idx_to_lc, {})
        yield (_render_sig(scope, func_name, params_r, ret),
               scope, func_name, is_static, params_r, ret)
        return

    seen_sigs = set()
    for combo in itertools.product(*[lc_types[lc] for lc in free_lcs]):
        lc_to_type = dict(zip(free_lcs, combo))
        params_r = _render_params(param_info, idx_to_lc, lc_to_type)
        ret = _resolve_ret(ret_type_str, idx_to_lc, lc_to_type)
        sig_key = _render_sig(scope, func_name, params_r, ret)
        if sig_key not in seen_sigs:
            seen_sigs.add(sig_key)
            yield (sig_key, scope, func_name, is_static, params_r, ret)


def _resolve_ret(ret_type_str, idx_to_lc, lc_to_type):
    """Resolve a (bracket-cleaned) return type expression to a concrete name."""
    r = ret_type_str.strip()

    # $classT / $funcT / $funcT2
    if r in ("$classT", "$funcT", "$funcT2"):
        return "T"

    # $typeN  →  same type as param N
    m = _TYPEREF_RE.match(r)
    if m:
        lc = idx_to_lc.get(int(m.group(1)))
        return lc_to_type.get(lc, "T") if lc else "T"

    # $match<X@Y> explicit_type<dims>
    m2 = re.match(r"^\$match<([^>]+)>\s+(\w[\w<>@]*)", r)
    if m2:
        y_str = (m2.group(1).split("@") + ["-1"])[1]
        try:
            y_idx = int(y_str)
        except ValueError:
            y_idx = -1
        if y_idx == -1:
            # y == -1: component from class template element type → T
            return "T"
        if y_idx == 0:
            # y == 0: component from return type itself → use the explicit type
            base = _get_base_type(m2.group(2))
            if base:
                lc = BASE_TYPE_TO_LICOMPTYPE.get(base)
                if lc:
                    if lc in lc_to_type:
                        return lc_to_type[lc]
                    types = LICOMPTYPE_TO_TYPES.get(lc, [])
                    return types[0] if types else "void"
            return "T"
        # y > 0: component from param y
        lc = idx_to_lc.get(y_idx)
        if lc:
            return lc_to_type.get(lc, "T")
        return "T"

    # Explicit base type (possibly with dimensions)
    base = _get_base_type(r)
    if base:
        lc = BASE_TYPE_TO_LICOMPTYPE.get(base)
        if lc:
            if lc in lc_to_type:
                return lc_to_type[lc]
            types = LICOMPTYPE_TO_TYPES.get(lc, [])
            return types[0] if types else "void"

    return "void"


# ---------------------------------------------------------------------------
# File parsing
# ---------------------------------------------------------------------------

def parse(filepath):
    """Parse gen_intrin_main.txt and yield (scope, func_name, is_static, params_rendered, ret) tuples."""
    # First pass: collect all namespace names so we can detect which non-RW
    # Methods namespaces have a RW counterpart (RW types inherit non-RW methods).
    all_namespaces = set()
    with open(filepath, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^namespace\s+(\w+)\s*\{", line.strip())
            if m:
                all_namespaces.add(m.group(1))

    current_ns = None
    seen_global: set = set()

    with open(filepath, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("//"):
                continue

            m = re.match(r"^namespace\s+(\w+)\s*\{", line)
            if m:
                current_ns = m.group(1)
                continue

            if re.match(r"^}\s*namespace", line):
                current_ns = None
                continue

            if "(" not in line or "[[" not in line:
                continue

            parsed = _parse_declaration_line(line)
            if parsed is None:
                continue

            ret_type_str, func_name, params_cleaned, is_static = parsed
            params = _parse_params(params_cleaned)

            # Determine scopes for this declaration.
            # Non-RW Methods namespaces also appear on their RW counterpart
            # (e.g. Texture2D methods are also valid on RWTexture2D).
            if current_ns:
                scope = namespace_to_scope(current_ns)
                scopes = [scope]
                if (current_ns.endswith("Methods")
                        and not current_ns.startswith("RW")
                        and ("RW" + current_ns) in all_namespaces):
                    scopes.append("RW" + scope)
            else:
                scopes = [""]

            for scope_val in scopes:
                for (sig_key, _, fn, is_static_val, params_r, ret) in expand_overloads(
                        scope_val, func_name, is_static, ret_type_str, params):
                    if sig_key not in seen_global:
                        seen_global.add(sig_key)
                        yield (scope_val, fn, is_static_val, params_r, ret)


# ---------------------------------------------------------------------------
# Output formatting
# ---------------------------------------------------------------------------

def _uses_bare_T(*strs):
    """Return True if any string contains T as a standalone type (not inside <>)."""
    combined = " ".join(strs)
    cleaned = re.sub(r'<[^>]*>', '', combined)
    return bool(re.search(r'\bT\b', cleaned))


def _scope_to_ns_class(scope):
    """Return (outer_ns, class_name) from scope string.

    ''              -> ('hlsl', None)    global HLSL function
    'vk'            -> ('vk',  None)    free function in vk namespace
    'dx'            -> ('dx',  None)    free function in dx namespace
    'vk::Foo'       -> ('vk',  'Foo')   class in vk namespace
    'dx::HitObject' -> ('dx',  'HitObject')
    'Buffer'        -> ('hlsl', 'Buffer')  class in implicit hlsl namespace
    """
    if not scope:
        return ('hlsl', None)
    parts = scope.split('::')
    if parts[0] in ('vk', 'dx'):
        outer_ns = parts[0]
        class_name = '::'.join(parts[1:]) if len(parts) > 1 else None
        return (outer_ns, class_name)
    return ('hlsl', scope)


def load_categories(script_dir):
    """Load ClassCategories.json next to the script.

    Returns (name_to_cat, cat_order) where name_to_cat maps
    (outer_ns, class_name) -> category string, and cat_order is the
    ordered list of category names from the JSON file.
    Returns ({}, []) if the file does not exist.
    """
    path = os.path.join(script_dir, "ClassCategories.json")
    if not os.path.exists(path):
        return {}, []
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    name_to_cat: dict = {}
    cat_order = list(data.keys())
    for cat, names in data.items():
        for raw_name in names:
            # Normalize spaces (e.g. "Ray Query" -> "RayQuery")
            name = raw_name.replace(" ", "")
            if "::" in name:
                ns, cls = name.split("::", 1)
                name_to_cat[(ns, cls)] = cat
            else:
                name_to_cat[("hlsl", name)] = cat
    return name_to_cat, cat_order


def _get_category(outer_ns, class_name, name_to_cat):
    """Return the category for a class, or None if uncategorized."""
    if class_name is None or not name_to_cat:
        return None
    return name_to_cat.get((outer_ns, class_name))


def _format_output(records, name_to_cat=None, cat_order=None):
    """Format (scope, func_name, is_static, params_r, ret) records as C++ pseudo-code."""
    # Group by (outer_ns, class_name) preserving declaration order.
    groups: dict = {}
    group_order: list = []
    for (scope, fn, is_static, params_r, ret) in records:
        key = _scope_to_ns_class(scope)
        if key not in groups:
            groups[key] = []
            group_order.append(key)
        groups[key].append((fn, is_static, params_r, ret))

    # Further group by outer_ns.
    ns_groups: dict = {}
    ns_order: list = []
    for key in group_order:
        outer_ns = key[0]
        if outer_ns not in ns_groups:
            ns_groups[outer_ns] = []
            ns_order.append(outer_ns)
        ns_groups[outer_ns].append((key[1], groups[key]))

    def render_class(class_name, methods):
        all_strs = []
        for (_, _, p, r) in methods:
            all_strs.append(r)
            all_strs.extend(p)
        lines = []
        if _uses_bare_T(*all_strs):
            lines.append("template <typename T>")
        lines.append(f"class {class_name} {{")
        for (fn, is_static, params_r, ret) in methods:
            static_str = "static " if is_static else ""
            lines.append(f"  {static_str}{ret} {fn}({', '.join(params_r)});")
        lines.append("};")
        return "\n".join(lines)

    output_parts = []
    for outer_ns in ns_order:
        block_parts = []
        classes_list = [(cn, ms) for cn, ms in ns_groups[outer_ns] if cn is not None]
        free_funcs_list = [(cn, ms) for cn, ms in ns_groups[outer_ns] if cn is None]

        if name_to_cat and cat_order:
            # Group classes by category, preserving category order.
            cat_to_classes: dict = {}
            for cn, ms in classes_list:
                cat = _get_category(outer_ns, cn, name_to_cat)
                if cat not in cat_to_classes:
                    cat_to_classes[cat] = []
                cat_to_classes[cat].append((cn, ms))

            for cat in cat_order:
                if cat in cat_to_classes:
                    block_parts.append(f"// {cat}")
                    for cn, ms in cat_to_classes[cat]:
                        block_parts.append(render_class(cn, ms))

            # Uncategorized classes (not listed in any category)
            if None in cat_to_classes:
                for cn, ms in cat_to_classes[None]:
                    block_parts.append(render_class(cn, ms))
        else:
            for cn, ms in classes_list:
                block_parts.append(render_class(cn, ms))

        # Free functions at the end of each namespace block.
        for (_, ms) in free_funcs_list:
            func_lines = []
            for (fn, _is_static, params_r, ret) in ms:
                func_lines.append(f"{ret} {fn}({', '.join(params_r)});")
            block_parts.append("\n".join(func_lines))

        block = "\n\n".join(block_parts)
        if outer_ns == 'hlsl':
            output_parts.append(block)
        else:
            output_parts.append(
                f"namespace {outer_ns} {{\n\n{block}\n\n}} // namespace {outer_ns}")

    return "\n\n".join(output_parts)


def main():
    parser = argparse.ArgumentParser(
        description="Print HLSL intrinsic declarations as C++ pseudo-code.")
    parser.add_argument("filepath", nargs="?", default=None,
                        help="Path to gen_intrin_main.txt (default: same directory as this script)")
    parser.add_argument("--filter", dest="filter_prefix", default=None,
                        metavar="SCOPE",
                        help="Only print declarations for SCOPE. "
                             "Examples: 'dx', 'dx::HitObject', 'RWBuffer'")
    parser.add_argument("--category", dest="category", default=None,
                        metavar="CATEGORY",
                        help="Only print classes in CATEGORY (from ClassCategories.json). "
                             "Use 'None' for uncategorized classes and free functions.")
    parser.add_argument("--functions-only", dest="functions_only", action="store_true",
                        help="Only print free functions (not methods of a class).")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_path = os.path.join(script_dir, "gen_intrin_main.txt")
    filepath = args.filepath if args.filepath else default_path

    name_to_cat, cat_order = load_categories(script_dir)

    prefix = (args.filter_prefix + "::") if args.filter_prefix else None
    def _matches(rec):
        if prefix is None:
            return True
        scope, fn, _is_static, params_r, ret = rec
        sig = _render_sig(scope, fn, params_r, ret)
        if sig.startswith(prefix):
            return True
        # 'hlsl' is a special alias for the implicit hlsl namespace (global
        # intrinsics and types with no explicit namespace qualifier).
        if prefix == "hlsl::":
            return _scope_to_ns_class(scope)[0] == 'hlsl'
        return False

    records = [rec for rec in parse(filepath) if _matches(rec)]

    # Apply --category filter.
    if args.category is not None:
        cat_filter = None if args.category == "None" else args.category
        records = [
            rec for rec in records
            if _get_category(*_scope_to_ns_class(rec[0]), name_to_cat) == cat_filter
        ]

    # Apply --functions-only filter.
    if args.functions_only:
        records = [rec for rec in records if _scope_to_ns_class(rec[0])[1] is None]

    if records:
        print(_format_output(records, name_to_cat or None, cat_order or None))


if __name__ == "__main__":
    main()
