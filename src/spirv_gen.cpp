#include "spirv_gen.h"

#include <cstring>

namespace {

// Opcodes (SPIR-V 1.0).
enum Op : uint16_t {
  OpMemoryModel = 14,
  OpEntryPoint = 15,
  OpExecutionMode = 16,
  OpCapability = 17,
  OpTypeVoid = 19,
  OpTypeInt = 21,
  OpTypeFloat = 22,
  OpTypeVector = 23,
  OpTypeMatrix = 24,
  OpTypeImage = 25,
  OpTypeSampledImage = 27,
  OpTypeStruct = 30,
  OpTypePointer = 32,
  OpTypeFunction = 33,
  OpConstant = 43,
  OpFunction = 54,
  OpFunctionEnd = 56,
  OpVariable = 59,
  OpLoad = 61,
  OpStore = 62,
  OpAccessChain = 65,
  OpDecorate = 71,
  OpMemberDecorate = 72,
  OpCompositeConstruct = 80,
  OpCompositeExtract = 81,
  OpImageSampleImplicitLod = 87,
  OpConvertSToF = 111,
  OpFAdd = 129,
  OpFSub = 131,
  OpFMul = 133,
  OpMatrixTimesVector = 145,
  OpShiftLeftLogical = 196,
  OpBitwiseAnd = 199,
  OpLabel = 248,
  OpReturn = 253,
};

struct Builder {
  std::vector<uint32_t> entry, execModes, annotations, types, functions;
  uint32_t nextId = 1;

  uint32_t id() { return nextId++; }

  static void emit(std::vector<uint32_t>& sec, uint16_t op,
                   std::initializer_list<uint32_t> ops) {
    sec.push_back(((uint32_t)(ops.size() + 1) << 16) | op);
    for (uint32_t w : ops) sec.push_back(w);
  }

  void entryPoint(uint16_t model, uint32_t fn, const char* name,
                  std::initializer_list<uint32_t> iface) {
    std::vector<uint32_t> ops = {model, fn};
    size_t len = std::strlen(name) + 1;
    size_t words = (len + 3) / 4;
    size_t base = ops.size();
    ops.resize(base + words, 0);
    std::memcpy(&ops[base], name, len);
    for (uint32_t v : iface) ops.push_back(v);
    entry.push_back(((uint32_t)(ops.size() + 1) << 16) | OpEntryPoint);
    entry.insert(entry.end(), ops.begin(), ops.end());
  }

  std::vector<uint32_t> assemble() {
    std::vector<uint32_t> m;
    m.push_back(0x07230203); // magic
    m.push_back(0x00010000); // version 1.0
    m.push_back(0);          // generator
    m.push_back(nextId);     // bound
    m.push_back(0);          // schema
    emit(m, OpCapability, {1}); // Shader
    emit(m, OpMemoryModel, {0, 1}); // Logical, GLSL450
    m.insert(m.end(), entry.begin(), entry.end());
    m.insert(m.end(), execModes.begin(), execModes.end());
    m.insert(m.end(), annotations.begin(), annotations.end());
    m.insert(m.end(), types.begin(), types.end());
    m.insert(m.end(), functions.begin(), functions.end());
    return m;
  }
};

} // namespace

std::vector<uint32_t> genDepthVert() {
  Builder b;
  uint32_t tVoid = b.id(), tFn = b.id();
  uint32_t tFloat = b.id(), tV3 = b.id(), tV4 = b.id(), tM4 = b.id();
  uint32_t tUint = b.id();
  uint32_t tPush = b.id(), tPtrPush = b.id(), tPtrPushM4 = b.id();
  uint32_t tPtrInV3 = b.id(), tPtrOutV4 = b.id();
  uint32_t cOne = b.id(), cZero = b.id();
  uint32_t vPc = b.id(), vInPos = b.id(), vGlPos = b.id();
  uint32_t fnMain = b.id();

  b.entryPoint(0 /*Vertex*/, fnMain, "main", {vInPos, vGlPos});

  Builder::emit(b.annotations, OpDecorate, {vInPos, 30 /*Location*/, 0});
  Builder::emit(b.annotations, OpDecorate, {vGlPos, 11 /*BuiltIn*/, 0});
  Builder::emit(b.annotations, OpMemberDecorate, {tPush, 0, 5 /*ColMajor*/});
  Builder::emit(b.annotations, OpMemberDecorate, {tPush, 0, 7, 16});
  Builder::emit(b.annotations, OpMemberDecorate, {tPush, 0, 35 /*Offset*/, 0});
  Builder::emit(b.annotations, OpDecorate, {tPush, 2 /*Block*/});

  auto& t = b.types;
  Builder::emit(t, OpTypeVoid, {tVoid});
  Builder::emit(t, OpTypeFunction, {tFn, tVoid});
  Builder::emit(t, OpTypeFloat, {tFloat, 32});
  Builder::emit(t, OpTypeVector, {tV3, tFloat, 3});
  Builder::emit(t, OpTypeVector, {tV4, tFloat, 4});
  Builder::emit(t, OpTypeMatrix, {tM4, tV4, 4});
  Builder::emit(t, OpTypeInt, {tUint, 32, 0});
  Builder::emit(t, OpTypeStruct, {tPush, tM4});
  Builder::emit(t, OpTypePointer, {tPtrPush, 9 /*PushConstant*/, tPush});
  Builder::emit(t, OpTypePointer, {tPtrPushM4, 9, tM4});
  Builder::emit(t, OpTypePointer, {tPtrInV3, 1 /*Input*/, tV3});
  Builder::emit(t, OpTypePointer, {tPtrOutV4, 3 /*Output*/, tV4});
  Builder::emit(t, OpConstant, {tFloat, cOne, 0x3f800000u});
  Builder::emit(t, OpConstant, {tUint, cZero, 0});
  Builder::emit(t, OpVariable, {tPtrPush, vPc, 9});
  Builder::emit(t, OpVariable, {tPtrInV3, vInPos, 1});
  Builder::emit(t, OpVariable, {tPtrOutV4, vGlPos, 3});

  auto& f = b.functions;
  Builder::emit(f, OpFunction, {tVoid, fnMain, 0, tFn});
  uint32_t l0 = b.id();
  Builder::emit(f, OpLabel, {l0});
  uint32_t pos = b.id(), px = b.id(), py = b.id(), pz = b.id();
  uint32_t pos4 = b.id(), pMat = b.id(), mat = b.id(), res = b.id();
  Builder::emit(f, OpLoad, {tV3, pos, vInPos});
  Builder::emit(f, OpCompositeExtract, {tFloat, px, pos, 0});
  Builder::emit(f, OpCompositeExtract, {tFloat, py, pos, 1});
  Builder::emit(f, OpCompositeExtract, {tFloat, pz, pos, 2});
  Builder::emit(f, OpCompositeConstruct, {tV4, pos4, px, py, pz, cOne});
  Builder::emit(f, OpAccessChain, {tPtrPushM4, pMat, vPc, cZero});
  Builder::emit(f, OpLoad, {tM4, mat, pMat});
  Builder::emit(f, OpMatrixTimesVector, {tV4, res, mat, pos4});
  Builder::emit(f, OpStore, {vGlPos, res});
  Builder::emit(f, OpReturn, {});
  Builder::emit(f, OpFunctionEnd, {});
  return b.assemble();
}

std::vector<uint32_t> genDepthFrag() {
  Builder b;
  uint32_t tVoid = b.id(), tFn = b.id(), fnMain = b.id();
  b.entryPoint(4 /*Fragment*/, fnMain, "main", {});
  Builder::emit(b.execModes, OpExecutionMode, {fnMain, 7 /*OriginUpperLeft*/});
  Builder::emit(b.types, OpTypeVoid, {tVoid});
  Builder::emit(b.types, OpTypeFunction, {tFn, tVoid});
  Builder::emit(b.functions, OpFunction, {tVoid, fnMain, 0, tFn});
  uint32_t l0 = b.id();
  Builder::emit(b.functions, OpLabel, {l0});
  Builder::emit(b.functions, OpReturn, {});
  Builder::emit(b.functions, OpFunctionEnd, {});
  return b.assemble();
}

std::vector<uint32_t> genBlitVert() {
  Builder b;
  uint32_t tVoid = b.id(), tFn = b.id();
  uint32_t tFloat = b.id(), tV2 = b.id(), tV4 = b.id(), tInt = b.id();
  uint32_t tPtrInInt = b.id(), tPtrOutV2 = b.id(), tPtrOutV4 = b.id();
  uint32_t cI1 = b.id(), cI2 = b.id();
  uint32_t cF0 = b.id(), cF1 = b.id(), cF2 = b.id(), cFHalf = b.id();
  uint32_t vIdx = b.id(), vUv = b.id(), vGlPos = b.id();
  uint32_t fnMain = b.id();

  b.entryPoint(0 /*Vertex*/, fnMain, "main", {vIdx, vUv, vGlPos});

  Builder::emit(b.annotations, OpDecorate, {vIdx, 11 /*BuiltIn*/, 42});
  Builder::emit(b.annotations, OpDecorate, {vUv, 30 /*Location*/, 0});
  Builder::emit(b.annotations, OpDecorate, {vGlPos, 11 /*BuiltIn*/, 0});

  auto& t = b.types;
  Builder::emit(t, OpTypeVoid, {tVoid});
  Builder::emit(t, OpTypeFunction, {tFn, tVoid});
  Builder::emit(t, OpTypeFloat, {tFloat, 32});
  Builder::emit(t, OpTypeVector, {tV2, tFloat, 2});
  Builder::emit(t, OpTypeVector, {tV4, tFloat, 4});
  Builder::emit(t, OpTypeInt, {tInt, 32, 1});
  Builder::emit(t, OpTypePointer, {tPtrInInt, 1 /*Input*/, tInt});
  Builder::emit(t, OpTypePointer, {tPtrOutV2, 3 /*Output*/, tV2});
  Builder::emit(t, OpTypePointer, {tPtrOutV4, 3 /*Output*/, tV4});
  Builder::emit(t, OpConstant, {tInt, cI1, 1});
  Builder::emit(t, OpConstant, {tInt, cI2, 2});
  Builder::emit(t, OpConstant, {tFloat, cF0, 0x00000000u});          // 0.0
  Builder::emit(t, OpConstant, {tFloat, cF1, 0x3f800000u});          // 1.0
  Builder::emit(t, OpConstant, {tFloat, cF2, 0x40000000u});          // 2.0
  Builder::emit(t, OpConstant, {tFloat, cFHalf, 0x3f000000u});       // 0.5
  Builder::emit(t, OpVariable, {tPtrInInt, vIdx, 1});
  Builder::emit(t, OpVariable, {tPtrOutV2, vUv, 3});
  Builder::emit(t, OpVariable, {tPtrOutV4, vGlPos, 3});

  auto& f = b.functions;
  Builder::emit(f, OpFunction, {tVoid, fnMain, 0, tFn});
  uint32_t l0 = b.id();
  Builder::emit(f, OpLabel, {l0});
  uint32_t idx = b.id(), shl = b.id(), aX = b.id(), aY = b.id();
  uint32_t fx = b.id(), fy = b.id(), fx2 = b.id(), fy2 = b.id();
  uint32_t x = b.id(), y = b.id();
  uint32_t ux = b.id(), uy = b.id(), ux2 = b.id(), uy2 = b.id();
  uint32_t uv = b.id(), pos = b.id();
  Builder::emit(f, OpLoad, {tInt, idx, vIdx});
  // x = float((idx << 1) & 2) * 2 - 1 ; y = float(idx & 2) * 2 - 1
  Builder::emit(f, OpShiftLeftLogical, {tInt, shl, idx, cI1});
  Builder::emit(f, OpBitwiseAnd, {tInt, aX, shl, cI2});
  Builder::emit(f, OpBitwiseAnd, {tInt, aY, idx, cI2});
  Builder::emit(f, OpConvertSToF, {tFloat, fx, aX});
  Builder::emit(f, OpConvertSToF, {tFloat, fy, aY});
  Builder::emit(f, OpFMul, {tFloat, fx2, fx, cF2});
  Builder::emit(f, OpFMul, {tFloat, fy2, fy, cF2});
  Builder::emit(f, OpFSub, {tFloat, x, fx2, cF1});
  Builder::emit(f, OpFSub, {tFloat, y, fy2, cF1});
  // uv = vec2(x, y) * 0.5 + 0.5
  Builder::emit(f, OpFMul, {tFloat, ux, x, cFHalf});
  Builder::emit(f, OpFMul, {tFloat, uy, y, cFHalf});
  Builder::emit(f, OpFAdd, {tFloat, ux2, ux, cFHalf});
  Builder::emit(f, OpFAdd, {tFloat, uy2, uy, cFHalf});
  Builder::emit(f, OpCompositeConstruct, {tV2, uv, ux2, uy2});
  Builder::emit(f, OpStore, {vUv, uv});
  Builder::emit(f, OpCompositeConstruct, {tV4, pos, x, y, cF0, cF1});
  Builder::emit(f, OpStore, {vGlPos, pos});
  Builder::emit(f, OpReturn, {});
  Builder::emit(f, OpFunctionEnd, {});
  return b.assemble();
}

std::vector<uint32_t> genBlitFrag() {
  Builder b;
  uint32_t tVoid = b.id(), tFn = b.id();
  uint32_t tFloat = b.id(), tV2 = b.id(), tV4 = b.id();
  uint32_t tImg = b.id(), tSamp = b.id(), tPtrSamp = b.id();
  uint32_t tPtrInV2 = b.id(), tPtrOutV4 = b.id();
  uint32_t cF1 = b.id();
  uint32_t vTex = b.id(), vUv = b.id(), vOut = b.id();
  uint32_t fnMain = b.id();

  b.entryPoint(4 /*Fragment*/, fnMain, "main", {vTex, vUv, vOut});
  Builder::emit(b.execModes, OpExecutionMode, {fnMain, 7 /*OriginUpperLeft*/});

  Builder::emit(b.annotations, OpDecorate, {vTex, 34 /*DescriptorSet*/, 0});
  Builder::emit(b.annotations, OpDecorate, {vTex, 33 /*Binding*/, 0});
  Builder::emit(b.annotations, OpDecorate, {vUv, 30 /*Location*/, 0});
  Builder::emit(b.annotations, OpDecorate, {vOut, 30 /*Location*/, 0});

  auto& t = b.types;
  Builder::emit(t, OpTypeVoid, {tVoid});
  Builder::emit(t, OpTypeFunction, {tFn, tVoid});
  Builder::emit(t, OpTypeFloat, {tFloat, 32});
  Builder::emit(t, OpTypeVector, {tV2, tFloat, 2});
  Builder::emit(t, OpTypeVector, {tV4, tFloat, 4});
  // OpTypeImage: float, 2D, depth=0, arrayed=0, ms=0, sampled=1, Unknown
  Builder::emit(t, OpTypeImage, {tImg, tFloat, 1, 0, 0, 0, 1, 0});
  Builder::emit(t, OpTypeSampledImage, {tSamp, tImg});
  Builder::emit(t, OpTypePointer, {tPtrSamp, 0 /*UniformConstant*/, tSamp});
  Builder::emit(t, OpTypePointer, {tPtrInV2, 1 /*Input*/, tV2});
  Builder::emit(t, OpTypePointer, {tPtrOutV4, 3 /*Output*/, tV4});
  Builder::emit(t, OpConstant, {tFloat, cF1, 0x3f800000u});
  Builder::emit(t, OpVariable, {tPtrSamp, vTex, 0});
  Builder::emit(t, OpVariable, {tPtrInV2, vUv, 1});
  Builder::emit(t, OpVariable, {tPtrOutV4, vOut, 3});

  auto& f = b.functions;
  Builder::emit(f, OpFunction, {tVoid, fnMain, 0, tFn});
  uint32_t l0 = b.id();
  Builder::emit(f, OpLabel, {l0});
  uint32_t uv = b.id(), tex = b.id(), s = b.id(), d = b.id(), out = b.id();
  Builder::emit(f, OpLoad, {tV2, uv, vUv});
  Builder::emit(f, OpLoad, {tSamp, tex, vTex});
  Builder::emit(f, OpImageSampleImplicitLod, {tV4, s, tex, uv});
  Builder::emit(f, OpCompositeExtract, {tFloat, d, s, 0});
  Builder::emit(f, OpCompositeConstruct, {tV4, out, d, d, d, cF1});
  Builder::emit(f, OpStore, {vOut, out});
  Builder::emit(f, OpReturn, {});
  Builder::emit(f, OpFunctionEnd, {});
  return b.assemble();
}
