#include <iostream>
#include <optional>
#include <cmath>
#include <cassert>

struct Vec2 { float x = 0.0f, y = 0.0f; };
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

struct Vertex
{
    Vec2 screenPos; // 屏幕空间像素坐标 (x, y)
    float viewZ;    // 相机空间正深度 (> 0)
    Vec2 uv;        // 纹理坐标 (u, v)
};

// 步骤 1：计算屏幕空间 2D 重心坐标（含退化三角形与外部点边界防御）
std::optional<Vec3> computeBarycentric2D(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) noexcept
{
    float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    // 防御退化三角形（面积接近 0，如三点共线）
    if (std::fabs(area) < 1e-6f)
    {
        return std::nullopt;
    }

    float invArea = 1.0f / area; // 优化：将 3 次除法转换为 1 次除法 + 3 次乘法
    float alpha = ((b.x - p.x) * (c.y - p.y) - (b.y - p.y) * (c.x - p.x)) * invArea;
    float beta = ((c.x - p.x) * (a.y - p.y) - (c.y - p.y) * (a.x - p.x)) * invArea;
    float gamma = ((a.x - p.x) * (b.y - p.y) - (a.y - p.y) * (b.x - p.x)) * invArea;

    // 防御三角形外部点（允许 1e-5f 的浮点边界误差）
    if (alpha < -1e-5f || beta < -1e-5f || gamma < -1e-5f)
    {
        return std::nullopt;
    }

    return Vec3{ alpha, beta, gamma };
}

// 步骤 2：屏幕空间直接线性插值 (Affine Interpolation / noperspective)
constexpr Vec2 interpolateAffine(const Vec3& bary2D, const Vertex& v0, const Vertex& v1, const Vertex& v2) noexcept
{
    float u = bary2D.x * v0.uv.x + bary2D.y * v1.uv.x + bary2D.z * v2.uv.x;
    float v = bary2D.x * v0.uv.y + bary2D.y * v1.uv.y + bary2D.z * v2.uv.y;
    return Vec2{ u, v };
}

// 步骤 3：透视正确插值 (Perspective-Correct Interpolation)
std::optional<Vec2> interpolatePerspective(const Vec3& bary2D, const Vertex& v0, const Vertex& v1, const Vertex& v2) noexcept
{
    // 防御非正深度，避免除零产生 Inf / NaN
    if (v0.viewZ <= 1e-6f || v1.viewZ <= 1e-6f || v2.viewZ <= 1e-6f)
    {
        return std::nullopt;
    }

    float invZ0 = 1.0f / v0.viewZ;
    float invZ1 = 1.0f / v1.viewZ;
    float invZ2 = 1.0f / v2.viewZ;

    // 1. 在屏幕空间线性插值 1/Z
    float invZp = bary2D.x * invZ0 + bary2D.y * invZ1 + bary2D.z * invZ2;
    if (std::fabs(invZp) <= 1e-6f) return std::nullopt;
    float Zp = 1.0f / invZp; // 还原当前像素的真实相机深度

    // 2. 在屏幕空间线性插值 UV/Z
    float uOverZ = bary2D.x * (v0.uv.x * invZ0) + bary2D.y * (v1.uv.x * invZ1) + bary2D.z * (v2.uv.x * invZ2);
    float vOverZ = bary2D.x * (v0.uv.y * invZ0) + bary2D.y * (v1.uv.y * invZ1) + bary2D.z * (v2.uv.y * invZ2);

    // 3. 乘以真实深度 Zp 还原透视正确的 UV
    return Vec2{ uOverZ * Zp, vOverZ * Zp };
}

int main()
{
    // --- 测试用例 1：不同深度下的透视校正对比 ---
    Vertex v0{ {0.0f, 0.0f},  1.0f,  {0.0f, 0.0f} };
    Vertex v1{ {10.0f, 0.0f}, 10.0f, {1.0f, 0.0f} };
    Vertex v2{ {0.0f, 10.0f}, 1.0f,  {0.0f, 1.0f} };
    Vec2 p{ 5.0f, 0.0f };

    auto baryOpt = computeBarycentric2D(p, v0.screenPos, v1.screenPos, v2.screenPos);
    assert(baryOpt.has_value());
    Vec2 uvAffine = interpolateAffine(*baryOpt, v0, v1, v2);
    auto uvPerspOpt = interpolatePerspective(*baryOpt, v0, v1, v2);
    assert(uvPerspOpt.has_value());

    std::cout << "[Test 1] Barycentric: (" << baryOpt->x << ", " << baryOpt->y << ", " << baryOpt->z << ")\n";
    std::cout << "[Test 1] Affine U:      " << uvAffine.x << " (Expected: 0.5)\n";
    std::cout << "[Test 1] Perspective U: " << uvPerspOpt->x << " (Expected: ~0.0909091)\n";
    assert(std::fabs(uvAffine.x - 0.5f) < 1e-5f);
    assert(std::fabs(uvPerspOpt->x - (1.0f / 11.0f)) < 1e-5f);

    // --- 测试用例 2：等深度退化验证（平行于相机平面） ---
    v0.viewZ = v1.viewZ = v2.viewZ = 5.0f;
    auto uvPerspEqualZ = interpolatePerspective(*baryOpt, v0, v1, v2);
    assert(uvPerspEqualZ.has_value());
    assert(std::fabs(uvAffine.x - uvPerspEqualZ->x) < 1e-5f);
    std::cout << "[Test 2] Equal Depth Passed! Affine U == Perspective U == " << uvPerspEqualZ->x << "\n";

    // --- 测试用例 3：退化三角形（共线）与外部点防御 ---
    auto degenerateBary = computeBarycentric2D(p, { 0,0 }, { 5,5 }, { 10,10 });
    assert(!degenerateBary.has_value()); // 共线三角形应返回 nullopt
    auto outsideBary = computeBarycentric2D({ 20.0f, 20.0f }, v0.screenPos, v1.screenPos, v2.screenPos);
    assert(!outsideBary.has_value());    // 外部点应返回 nullopt
    std::cout << "[Test 3] Boundary & Degenerate Defense Passed!\n";

    return 0;
}