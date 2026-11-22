#!/usr/bin/env python3
"""Tiny, dependency-free SPIR-V assembler for the voxel slicer shaders.

It supports exactly the instruction subset used by src/shaders/*.spvasm and
emits a C++ header with a uint32_t word array. The Vulkan driver validates the
module at vkCreateShaderModule time, so a full assembler is unnecessary.
"""

import argparse
import struct
import sys

OPCODES = {
    "OpSourceContinued": 2, "OpSource": 3, "OpName": 5, "OpMemberName": 6,
    "OpString": 7, "OpExtension": 10, "OpExtInstImport": 11, "OpExtInst": 12,
    "OpMemoryModel": 14, "OpEntryPoint": 15, "OpExecutionMode": 16,
    "OpCapability": 17, "OpTypeVoid": 19, "OpTypeBool": 20,
    "OpTypeInt": 21, "OpTypeFloat": 22, "OpTypeVector": 23,
    "OpTypeImage": 24, "OpTypeSampledImage": 25, "OpTypeSampler": 26,
    "OpTypeArray": 28, "OpTypeStruct": 30, "OpTypePointer": 32,
    "OpTypeFunction": 33, "OpConstant": 43, "OpConstantComposite": 44,
    "OpFunction": 54, "OpFunctionParameter": 55, "OpFunctionEnd": 56,
    "OpVariable": 59, "OpLoad": 61, "OpStore": 62, "OpAccessChain": 65,
    "OpDecorate": 71, "OpMemberDecorate": 72, "OpCompositeExtract": 79,
    "OpCompositeConstruct": 80, "OpConvertFToS": 112,
    "OpSClamp": 103,
    "OpConvertSToF": 110, "OpConvertUToF": 111, "OpSNegate": 126,
    "OpIAdd": 128, "OpFAdd": 129, "OpISub": 130, "OpFSub": 131,
    "OpIMul": 132, "OpFMul": 133, "OpSampledImage": 86,
    "OpImageFetch": 93, "OpLabel": 248, "OpBranch": 251,
    "OpBranchConditional": 252, "OpReturn": 255, "OpReturnValue": 256,
}

ENUMS = {
    # capabilities / models / execution
    "Shader": 1, "Logical": 0, "GLSL450": 1, "Vertex": 0, "Fragment": 4,
    "OriginUpperLeft": 7,
    # storage classes
    "UniformConstant": 0, "Input": 1, "Uniform": 2, "Output": 3,
    "Function": 7, "PushConstant": 9,
    # image
    "Dim2D": 1, "Unknown": 0,
    # decorations
    "Block": 2, "BuiltIn": 11, "Location": 30, "Binding": 33,
    "DescriptorSet": 34, "Offset": 35,
    # builtins
    "Position": 0, "VertexIndex": 42,
    # GLSL.std.450 extended instructions used by the shaders
    "SClamp": 43,
}

# Opcodes that produce a result id but take no result-type operand:
# all type declarations, labels and ext-instruction imports.
NO_RESULT_TYPE_PREFIX = ("OpType",)
NO_RESULT_TYPE = {"OpLabel", "OpExtInstImport"}


class Assembler:
    def __init__(self):
        self.ids = {}
        self.next_id = 1
        self.caps, self.ext_imports, self.entrypoints = [], [], []
        self.exec_modes, self.names, self.decorations = [], [], []
        self.globals = []
        self.types = []
        self.functions = []

    def id_of(self, token):
        if token not in self.ids:
            self.ids[token] = self.next_id
            self.next_id += 1
        return self.ids[token]

    def operand(self, tok):
        if tok.startswith("%"):
            return [("id", self.id_of(tok))]
        if tok.startswith('"'):
            raw = tok[1:-1].encode("utf-8")
            words = [raw[i:i + 4].ljust(4, b"\0") for i in range(0, len(raw), 4)]
            return [("str", b"".join(words) or b"\0\0\0\0")]
        if tok in ENUMS:
            return [("lit", ENUMS[tok])]
        try:
            if tok.endswith("f") or "." in tok:
                return [("float", struct.unpack("<I", struct.pack("<f", float(tok.rstrip("f"))))[0])]
            return [("lit", int(tok, 0))]
        except ValueError:
            raise SystemExit("spvasm: cannot parse operand %r" % tok)

    def assemble_instruction(self, line):
        assign = None
        tokens = self.tokenize(line)
        if "=" in tokens:
            at = tokens.index("=")
            # "%x = Op ..." (leading result) or "Op ... %x = ..." (operand id)
            if at >= 2 and tokens[at - 2].startswith("%") and \
                    tokens[at - 1] in OPCODES:
                assign = tokens[at - 2]
                tokens = tokens[:at - 2] + tokens[at + 1:]
            elif at == 1 and tokens[0].startswith("%"):
                assign = tokens[0]
                tokens = tokens[2:]
            else:
                # Result id lives among the operands (e.g. OpExtInstImport).
                result_tok = tokens[at - 1]
                assign = result_tok if result_tok.startswith("%") else None
                tokens = tokens[:at - 1] + tokens[at + 1:]
        op = tokens[0]
        if op not in OPCODES:
            raise SystemExit("spvasm: unknown opcode %s" % op)
        words = []
        for tok in tokens[1:]:
            words.extend(self.operand(tok))
        rid = self.id_of(assign) if assign else None
        return OPCODES[op], rid, words

    @staticmethod
    def tokenize(line):
        out, cur, in_str = [], "", False
        for ch in line:
            if ch == '"':
                in_str = not in_str
                cur += ch
            elif ch.isspace() and not in_str:
                if cur:
                    out.append(cur)
                    cur = ""
            else:
                cur += ch
        if cur:
            out.append(cur)
        return out

    def emit(self, words, section=None):
        target = self.body if section is None else section
        target.append(words)

    def parse(self, text):
        for raw in text.splitlines():
            line = raw.split(";", 1)[0].strip()
            if not line:
                continue
            parts = line.split(None, 1)
            head = parts[0]
            rest = parts[1] if len(parts) > 1 else ""
            if head == "cap":
                self.caps.append(self.assemble_instruction("OpCapability " + rest))
            elif head == "extinst":
                self.ext_imports.append(
                    self.assemble_instruction("OpExtInstImport " + rest))
            elif head == "memmodel":
                self.memmodel = self.assemble_instruction("OpMemoryModel " + rest)
            elif head == "entry":
                self.entrypoints.append(
                    self.assemble_instruction("OpEntryPoint " + rest))
            elif head == "mode":
                self.exec_modes.append(
                    self.assemble_instruction("OpExecutionMode " + rest))
            elif head == "name":
                self.names.append(self.assemble_instruction("OpName " + rest))
            elif head in ("decorate", "memberdecorate"):
                opcode = "OpDecorate" if head == "decorate" else "OpMemberDecorate"
                self.decorations.append(
                    self.assemble_instruction("%s %s" % (opcode, rest)))
            elif head == "global":
                # Constants/composites must live in the global section even
                # when declared textually next to the function that uses them.
                self.globals.append(self.assemble_instruction(rest))
            else:
                ins = self.assemble_instruction(line)
                op = ins[0]
                if OPCODES["OpFunction"] <= op <= OPCODES["OpFunctionEnd"] or \
                        op == OPCODES["OpLabel"]:
                    self.functions.append(ins)
                elif op in (OPCODES["OpTypeVoid"], OPCODES["OpTypeBool"],
                            OPCODES["OpTypeInt"], OPCODES["OpTypeFloat"],
                            OPCODES["OpTypeVector"], OPCODES["OpTypeImage"],
                            OPCODES["OpTypeSampledImage"],
                            OPCODES["OpTypeSampler"], OPCODES["OpTypeArray"],
                            OPCODES["OpTypeStruct"], OPCODES["OpTypePointer"],
                            OPCODES["OpTypeFunction"], OPCODES["OpVariable"]):
                    self.types.append(ins)
                else:
                    # Non-constant globals (rare in these shaders) keep
                    # textual order alongside type declarations.
                    self.types.append(ins)

    def encode(self, instruction):
        opcode, result_id, operands = instruction
        payload = []
        op_name = next((k for k, v in OPCODES.items() if v == opcode), "")
        no_type = op_name in NO_RESULT_TYPE or \
            op_name.startswith(NO_RESULT_TYPE_PREFIX)
        if result_id is not None and not no_type:
            # Result type id is the first id operand for instructions that
            # have both a result type and a result id.
            type_id = None
            kept = []
            for kind, val in operands:
                if type_id is None and kind == "id":
                    type_id = val
                else:
                    kept.append((kind, val))
            payload.append(type_id)
            payload.append(result_id)
            operands = kept
        elif result_id is not None:
            payload.append(result_id)
        for kind, val in operands:
            if kind == "str":
                payload.extend(struct.unpack("<%dI" % (len(val) // 4), val))
            else:
                if val is None:
                    raise SystemExit("spvasm: unresolved operand in opcode %d"
                                     % opcode)
                payload.append(val)
        word_count = 1 + len(payload)
        return [(word_count << 16) | opcode] + payload

    def words(self):
        out = [0x07230203, 0x00010300, 0, self.next_id, 0]
        sections = (self.caps, self.ext_imports, [self.memmodel],
                    self.entrypoints, self.exec_modes, self.names,
                    self.decorations, self.types, self.globals,
                    self.functions)
        for section in sections:
            for ins in section:
                out.extend(self.encode(ins))
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("symbol")
    args = ap.parse_args()
    asm = Assembler()
    with open(args.input) as f:
        asm.parse(f.read())
    words = asm.words()
    with open(args.output, "w") as f:
        f.write("// Generated by tools/spvasm.py from %s — do not edit.\n"
                "#pragma once\n#include <cstdint>\n" % args.input)
        f.write("static const uint32_t %s[] = {\n" % args.symbol)
        for i in range(0, len(words), 8):
            f.write("  " + ",".join("0x%08x" % w for w in words[i:i + 8]) + ",\n")
        f.write("};\n")


if __name__ == "__main__":
    main()
