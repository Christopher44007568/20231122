#include "core/generator.h"
#include "core/scene.h"
#include "core/util.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

namespace ssor {
namespace {

struct Point2 {
    float x;
    float y;
};

float cross(const Point2& o, const Point2& a, const Point2& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

std::vector<Point2> convexHull(std::vector<Point2> points) {
    std::sort(points.begin(), points.end(), [](const Point2& a, const Point2& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });
    points.erase(std::unique(points.begin(), points.end(),
                             [](const Point2& a, const Point2& b) {
                                 return std::fabs(a.x - b.x) < 0.01f &&
                                        std::fabs(a.y - b.y) < 0.01f;
                             }),
                 points.end());
    if (points.size() <= 2) return points;
    std::vector<Point2> hull(points.size() * 2);
    std::size_t k = 0;
    for (const Point2& p : points) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], p) <= 0.0f) --k;
        hull[k++] = p;
    }
    for (std::size_t i = points.size() - 1, t = k + 1; i > 0; --i) {
        const Point2& p = points[i - 1];
        while (k >= t && cross(hull[k - 2], hull[k - 1], p) <= 0.0f) --k;
        hull[k++] = p;
    }
    hull.resize(k - 1);
    return hull;
}

void addProbe(RawMaterial& material, std::set<std::pair<std::uint32_t, std::uint32_t>>& seen,
              float x, float y, ProbeKind kind, std::uint32_t objectIndex) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    const int ix = static_cast<int>(std::lround(x));
    const int iy = static_cast<int>(std::lround(y));
    if (ix <= 1 || iy <= 1 || ix >= static_cast<int>(material.width) - 2 ||
        iy >= static_cast<int>(material.height) - 2) {
        return;
    }
    const auto key = std::make_pair(static_cast<std::uint32_t>(ix),
                                    static_cast<std::uint32_t>(iy));
    if (!seen.insert(key).second) return;
    material.boundaryPixels.push_back(
        {key.first, key.second, kind, objectIndex, 0});
}

void addProjectedProbes(RawMaterial& material) {
    SceneBuild build = buildScenes(material);
    const SceneDraw* base = nullptr;
    for (const SceneDraw& draw : build.baseline.draws) {
        if (!draw.duplicate && draw.mesh == MeshType::Cube &&
            draw.logicalId == "pillar.base") {
            base = &draw;
            break;
        }
    }
    if (!base) return;

    std::vector<Point2> projected;
    for (int z = -1; z <= 1; z += 2) {
        for (int y = -1; y <= 1; y += 2) {
            for (int x = -1; x <= 1; x += 2) {
                const Vec3f local{0.5f * x, 0.5f * y, 0.5f * z};
                const float wx = base->model.m[0] * local.x +
                                 base->model.m[4] * local.y +
                                 base->model.m[8] * local.z + base->model.m[12];
                const float wy = base->model.m[1] * local.x +
                                 base->model.m[5] * local.y +
                                 base->model.m[9] * local.z + base->model.m[13];
                const float wz = base->model.m[2] * local.x +
                                 base->model.m[6] * local.y +
                                 base->model.m[10] * local.z + base->model.m[14];
                float px = 0.0f;
                float py = 0.0f;
                float w = 0.0f;
                if (projectPoint(build.cameraMatrices.viewProj, {wx, wy, wz},
                                 material.width, material.height, &px, &py, &w)) {
                    projected.push_back({px, py});
                }
            }
        }
    }
    if (projected.size() < 3) return;

    const std::vector<Point2> hull = convexHull(projected);
    if (hull.size() < 3) return;
    std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
    float cx = 0.0f;
    float cy = 0.0f;
    for (const Point2& p : hull) {
        cx += p.x;
        cy += p.y;
    }
    cx /= static_cast<float>(hull.size());
    cy /= static_cast<float>(hull.size());

    for (std::size_t i = 0; i < hull.size(); ++i) {
        const Point2& a = hull[i];
        const Point2& b = hull[(i + 1) % hull.size()];
        for (float t : {0.15f, 0.38f, 0.62f, 0.85f}) {
            addProbe(material, seen, a.x + (b.x - a.x) * t,
                     a.y + (b.y - a.y) * t, ProbeKind::Boundary, 1);
        }
    }
    for (std::size_t i = 0; i < hull.size(); i += 2) {
        addProbe(material, seen, cx + (hull[i].x - cx) * 0.42f,
                 cy + (hull[i].y - cy) * 0.42f, ProbeKind::Interior, 1);
    }
    const float margin = 8.0f;
    const float w = static_cast<float>(material.width);
    const float h = static_cast<float>(material.height);
    const Point2 backgrounds[] = {
        {margin, margin}, {w - margin, margin}, {margin, h - margin},
        {w - margin, h - margin}, {w * 0.5f, margin},
        {margin, h * 0.5f}, {w - margin, h * 0.5f}, {w * 0.5f, h - margin}};
    for (const Point2& p : backgrounds) {
        addProbe(material, seen, p.x, p.y, ProbeKind::Background, 1);
    }
}

RawMaterial baseMaterial(const std::string& name) {
    RawMaterial material;
    material.path = name;
    material.width = 320;
    material.height = 240;
    material.outlineDepth = 0.035f;
    material.normalThreshold = 0.30f;
    material.gtDepthEps = 0.004f;
    material.gtNormalEps = 0.02f;
    material.cameraEye = {3.15f, 2.35f, 4.25f};
    material.cameraTarget = {-0.05f, 0.65f, 0.0f};
    material.flags = 0;

    ObjectDef ground;
    ground.id = "ground";
    ground.mesh = MeshType::GroundPlane;
    ground.drawOrder = 0;
    ground.translation = {0.0f, 0.0f, 0.0f};
    ground.originalOffset = {0.0f, 0.0f, 0.0f};
    ground.rotationYDeg = 0.0f;
    ground.scale = {1.0f, 1.0f, 1.0f};
    ground.repeatOf = kInvalidObject;
    ground.flags = 0;
    material.objects.push_back(ground);

    ObjectDef base;
    base.id = "pillar.base";
    base.mesh = MeshType::Cube;
    base.drawOrder = 10;
    base.translation = {-0.36f, 0.64f, 0.0f};
    base.originalOffset = {0.0f, 0.0f, 0.0f};
    base.rotationYDeg = 22.0f;
    base.scale = {0.82f, 1.16f, 0.82f};
    base.repeatOf = kInvalidObject;
    base.flags = 0;
    material.objects.push_back(base);

    ObjectDef repeat;
    repeat.id = "pillar.repeat";
    repeat.mesh = MeshType::Cube;
    repeat.drawOrder = 11;
    repeat.translation = {0.22f, 0.64f, 0.06f};
    repeat.originalOffset = {0.58f, 0.0f, 0.06f};
    repeat.rotationYDeg = 22.0f;
    repeat.scale = {0.82f, 1.16f, 0.82f};
    repeat.repeatOf = 1;
    repeat.flags = 1;
    material.objects.push_back(repeat);
    return material;
}

}  // namespace

RawMaterial makeCanonicalMaterial() {
    RawMaterial material = baseMaterial("outline_repeat.ssoraw");
    addProjectedProbes(material);
    material.objectCount = static_cast<std::uint32_t>(material.objects.size());
    material.boundaryCount =
        static_cast<std::uint32_t>(material.boundaryPixels.size());
    material.fileSize = encodeMaterial(material).size();
    return material;
}

RawMaterial makeEmptyBoundaryMaterial() {
    RawMaterial material = baseMaterial("empty_boundaries.ssoraw");
    material.objectCount = static_cast<std::uint32_t>(material.objects.size());
    material.boundaryCount = 0;
    material.fileSize = encodeMaterial(material).size();
    return material;
}

bool writeCanonicalMaterials(const std::string& rawDir) {
    if (!makeDirs(rawDir)) return false;
    const RawMaterial canonical = makeCanonicalMaterial();
    const RawMaterial emptyBoundaries = makeEmptyBoundaryMaterial();
    return writeMaterialFile(pathJoin(rawDir, "outline_repeat.ssoraw"), canonical) &&
           writeMaterialFile(pathJoin(rawDir, "empty_boundaries.ssoraw"),
                             emptyBoundaries);
}

}  // namespace ssor
