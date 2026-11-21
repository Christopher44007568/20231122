#include "app/scene_builder.h"

#include "core/clip_surface.h"
#include "core/obj_io.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace clipfix {
namespace {

using Color = std::array<float, 3>;

constexpr std::array<Color, 3> kObjectColors{{
    {0.34f, 0.66f, 0.96f},
    {0.95f, 0.62f, 0.28f},
    {0.42f, 0.82f, 0.60f},
}};

Color scaleColor(Color color, float scale) {
    return {
        std::clamp(color[0] * scale, 0.0f, 1.0f),
        std::clamp(color[1] * scale, 0.0f, 1.0f),
        std::clamp(color[2] * scale, 0.0f, 1.0f),
    };
}

Color diffColor(double deltaMm) {
    const float t = static_cast<float>(std::clamp(deltaMm / 0.14, 0.0, 1.0));
    if (t < 0.55f) {
        const float f = t / 0.55f;
        return {
            0.16f + 0.84f * f,
            0.58f + 0.26f * f,
            1.0f - 0.78f * f,
        };
    }
    const float f = (t - 0.55f) / 0.45f;
    return {1.0f, 0.84f - 0.58f * f, 0.22f - 0.10f * f};
}

void appendVertex(
    std::vector<RenderVertex>& target,
    const Vec3& position,
    const Vec3& normal,
    const Color& color) {
    target.push_back({
        {static_cast<float>(position.x), static_cast<float>(position.y),
         static_cast<float>(position.z)},
        {static_cast<float>(normal.x), static_cast<float>(normal.y),
         static_cast<float>(normal.z)},
        {color[0], color[1], color[2]},
    });
}

void addLine(
    std::vector<RenderVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const Vec3& a,
    const Vec3& b,
    const Color& color) {
    const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
    appendVertex(vertices, a, {}, color);
    appendVertex(vertices, b, {}, color);
    indices.push_back(base);
    indices.push_back(base + 1);
}

void addCross(
    std::vector<RenderVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const Vec3& center,
    double size,
    const Color& color) {
    addLine(vertices, indices, center - Vec3{size, 0.0, 0.0}, center + Vec3{size, 0.0, 0.0}, color);
    addLine(vertices, indices, center - Vec3{0.0, size, 0.0}, center + Vec3{0.0, size, 0.0}, color);
    addLine(vertices, indices, center - Vec3{0.0, 0.0, size}, center + Vec3{0.0, 0.0, size}, color);
}

Mesh loadDisplayMesh(
    const AppState& app,
    const std::string& objectId,
    bool preferCorrected) {
    if (preferCorrected && app.workspace.latestRun(objectId) != nullptr) {
        try {
            return app.workspace.loadLatestRunMesh(objectId);
        } catch (const std::exception&) {
        }
    }
    const auto* rawObject = app.workspace.findRawObject(objectId);
    if (rawObject == nullptr) {
        throw std::runtime_error("missing raw object");
    }
    return rawObject->mesh;
}

void appendObject(
    RenderData& data,
    const AppState& app,
    int objectIndex,
    const RawObject& rawObject,
    bool wireframe) {
    const bool selected = objectIndex == app.workspace.state().selectedObject;
    Mesh mesh;
    Mesh original;
    bool hasCorrected = false;
    if (app.viewMode == ViewMode::Original) {
        mesh = rawObject.mesh;
    } else {
        mesh = loadDisplayMesh(app, rawObject.id, true);
        hasCorrected = app.workspace.latestRun(rawObject.id) != nullptr;
    }

    Color base = kObjectColors[static_cast<std::size_t>(objectIndex) % kObjectColors.size()];
    base = scaleColor(base, selected ? 1.16f : 0.58f);
    if (app.viewMode != ViewMode::Diff && !hasCorrected &&
        app.viewMode == ViewMode::Corrected) {
        base = scaleColor(base, 0.55f);
    }

    if (app.viewMode == ViewMode::Diff) {
        original = rawObject.mesh;
        if (!hasCorrected) {
            base = {0.43f, 0.47f, 0.52f};
        }
    }

    if (mesh.normals.size() != mesh.positions.size()) {
        recomputeVertexNormals(mesh);
    }
    const std::uint32_t baseVertex = static_cast<std::uint32_t>(data.triangleVertices.size());
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        Color color = base;
        if (app.viewMode == ViewMode::Diff && hasCorrected &&
            i < original.positions.size()) {
            color = diffColor((mesh.positions[i] - original.positions[i]).length());
        } else if (app.viewMode == ViewMode::Diff) {
            color = {0.43f, 0.47f, 0.52f};
        }
        appendVertex(data.triangleVertices, mesh.positions[i], mesh.normals[i], color);
    }
    for (const auto index : mesh.indices) {
        data.triangleIndices.push_back(baseVertex + index);
    }

    if (wireframe && selected) {
        const Color lineColor = selected ? Color{0.94f, 0.96f, 1.0f}
                                         : Color{0.34f, 0.38f, 0.44f};
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const auto a = mesh.positions[mesh.indices[i]];
            const auto b = mesh.positions[mesh.indices[i + 1]];
            const auto c = mesh.positions[mesh.indices[i + 2]];
            addLine(data.lineVertices, data.lineIndices, a, b, lineColor);
            addLine(data.lineVertices, data.lineIndices, b, c, lineColor);
            addLine(data.lineVertices, data.lineIndices, c, a, lineColor);
        }
    }
}

}  // namespace

RenderData buildRenderData(const AppState& app) {
    RenderData data;
    data.selectedObject = app.workspace.state().selectedObject;
    const auto& objects = app.workspace.material().scene.objects;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        appendObject(
            data,
            app,
            static_cast<int>(i),
            objects[i],
            app.showWireframe);
    }

    const ClipSurface surface(
        app.workspace.anchorPositions(),
        app.workspace.anchorNormals(),
        app.workspace.state().currentOffsetsMm);
    if (app.showClipSurface) {
        const Color surfaceColor{0.82f, 0.54f, 0.17f};
        const int longitudinal = 72;
        const int lateralLines = 9;
        for (int lateral = 0; lateral < lateralLines; ++lateral) {
            const double s = -1.8 + 3.6 * static_cast<double>(lateral) /
                                       static_cast<double>(lateralLines - 1);
            Vec3 previous;
            for (int step = 0; step <= longitudinal; ++step) {
                const double t = static_cast<double>(step) / longitudinal;
                const Vec3 point = surface.pointAt(t);
                const Vec3 tangent = surface.tangentAt(t);
                const Vec3 normal = surface.surfaceNormalAt(t);
                const Vec3 binormal = tangent.cross(normal).normalized();
                const Vec3 current = point + binormal * s;
                if (step > 0) {
                    addLine(
                        data.lineVertices,
                        data.lineIndices,
                        previous,
                        current,
                        surfaceColor);
                }
                previous = current;
            }
        }
        for (int step = 0; step <= 16; ++step) {
            const double t = static_cast<double>(step) / 16.0;
            const Vec3 point = surface.pointAt(t);
            const Vec3 tangent = surface.tangentAt(t);
            const Vec3 normal = surface.surfaceNormalAt(t);
            const Vec3 binormal = tangent.cross(normal).normalized();
            addLine(
                data.lineVertices,
                data.lineIndices,
                point - binormal * 1.8,
                point + binormal * 1.8,
                surfaceColor);
        }
    }

    const std::array<Color, 3> anchorColors{{
        {1.0f, 0.34f, 0.30f},
        {0.38f, 1.0f, 0.42f},
        {0.38f, 0.56f, 1.0f},
    }};
    const auto positions = app.workspace.anchorPositions();
    const auto normals = app.workspace.anchorNormals();
    for (std::size_t i = 0; i < 3; ++i) {
        addCross(data.lineVertices, data.lineIndices, positions[i], 0.10, anchorColors[i]);
        const Vec3 applied =
            positions[i] + normals[i].normalized() * app.workspace.state().currentOffsetsMm[i];
        addLine(
            data.lineVertices,
            data.lineIndices,
            positions[i],
            applied,
            anchorColors[i]);
        addCross(data.lineVertices, data.lineIndices, applied, 0.045, anchorColors[i]);
    }

    addLine(
        data.lineVertices,
        data.lineIndices,
        {0.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {0.92f, 0.20f, 0.20f});
    addLine(
        data.lineVertices,
        data.lineIndices,
        {0.0, 0.0, 0.0},
        {0.0, 1.0, 0.0},
        {0.20f, 0.92f, 0.24f});
    addLine(
        data.lineVertices,
        data.lineIndices,
        {0.0, 0.0, 0.0},
        {0.0, 0.0, 1.0},
        {0.24f, 0.44f, 0.96f});
    return data;
}

}  // namespace clipfix
