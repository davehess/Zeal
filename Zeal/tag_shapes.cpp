#include "tag_shapes.h"

#include <cmath>
#include <string>
#include <utility>

namespace TagShapes {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kHalfThickness = 0.25f;  // Same thickness as the octagon and paw.
constexpr float kProud = 0.03f;          // How far detail parts stand out of each face.
constexpr float kInset = 0.02f;          // How far a one-sided part sinks into its face.

struct Point {
  float x;
  float z;
};

struct Part {
  std::vector<Point> outline;  // Convex, or star-shaped around center (unless triangles are given).
  Point center;
  Tone tone = Tone::Base;
  float y_front = -kHalfThickness;  // The part spans y_front to y_back.
  float y_back = kHalfThickness;
  std::vector<int16_t> triangles = {};  // Optional triangulation of the outline (index triples); else a fan.
};

// A part standing `level` steps proud of both faces (layers stack without z-fighting).
Part Raised(std::vector<Point> outline, Point center, Tone tone, int level) {
  const float y = kHalfThickness + kProud * static_cast<float>(level);
  return {std::move(outline), center, tone, -y, y};
}

std::vector<Point> Ellipse(Point center, float rx, float rz, int count) {
  std::vector<Point> points;
  for (int i = 0; i < count; ++i) {
    float angle = 2 * kPi * static_cast<float>(i) / static_cast<float>(count);
    points.push_back({center.x + rx * cosf(angle), center.z + rz * sinf(angle)});
  }
  return points;
}

std::vector<Point> Rect(float x0, float z0, float x1, float z1) { return {{x0, z0}, {x1, z0}, {x1, z1}, {x0, z1}}; }

// A bar of the given length and width centered on center and rotated counter-clockwise by angle.
std::vector<Point> Bar(Point center, float length, float width, float angle) {
  const float c = cosf(angle), s = sinf(angle);
  const float hl = length / 2, hw = width / 2;
  std::vector<Point> points;
  for (const auto &corner : {Point{-hl, -hw}, Point{hl, -hw}, Point{hl, hw}, Point{-hl, hw}})
    points.push_back({center.x + corner.x * c - corner.z * s, center.z + corner.x * s + corner.z * c});
  return points;
}

// A drop: a circle of radius r drawn out to a point at tip (the convex hull of the two).
std::vector<Point> Teardrop(Point center, float r, Point tip, int count) {
  const float dx = tip.x - center.x, dz = tip.z - center.z;
  const float direction = atan2f(dz, dx);
  const float tangent = acosf(r / sqrtf(dx * dx + dz * dz));  // Angle to where the sides leave the circle.
  std::vector<Point> points;
  for (int i = 0; i <= count; ++i) {
    float angle = direction + tangent + (2 * kPi - 2 * tangent) * static_cast<float>(i) / static_cast<float>(count);
    points.push_back({center.x + r * cosf(angle), center.z + r * sinf(angle)});
  }
  points.push_back(tip);
  return points;
}

// A star with the given number of points, the first one straight up.
std::vector<Point> Star(Point center, float outer, float inner, int points_count) {
  std::vector<Point> points;
  for (int i = 0; i < 2 * points_count; ++i) {
    float angle = kPi / 2 + kPi * static_cast<float>(i) / static_cast<float>(points_count);
    float r = (i % 2) ? inner : outer;
    points.push_back({center.x + r * cosf(angle), center.z + r * sinf(angle)});
  }
  return points;
}

Part Detail(std::vector<Point> outline, Point center, Tone tone) {
  return {std::move(outline), center, tone, -(kHalfThickness + kProud), kHalfThickness + kProud};
}

// Adds a strip to the mesh, joined to the previous one with degenerate triangles.
void AppendStrip(Mesh &mesh, const std::vector<int16_t> &strip) {
  if (strip.empty()) return;
  if (!mesh.indices.empty()) {
    mesh.indices.push_back(mesh.indices.back());
    mesh.indices.push_back(strip.front());
  }
  mesh.indices.insert(mesh.indices.end(), strip.begin(), strip.end());
}

// Adds the front face, back face and side wall of an extruded part.
void AppendPart(Mesh &mesh, const Part &part) {
  const int count = static_cast<int>(part.outline.size());
  const int base = static_cast<int>(mesh.vertices.size());
  for (float y : {part.y_front, part.y_back}) {
    mesh.vertices.push_back({part.center.x, y, part.center.z, part.tone});
    for (const auto &point : part.outline) mesh.vertices.push_back({point.x, y, point.z, part.tone});
  }
  const int front_ring = base + 1;
  const int back_ring = base + count + 2;

  for (const auto &[center, ring] : {std::pair{base, front_ring}, std::pair{base + count + 1, back_ring}}) {
    if (!part.triangles.empty()) {  // A traced outline: each given triangle is its own short strip.
      for (size_t i = 0; i + 2 < part.triangles.size(); i += 3)
        AppendStrip(mesh,
                    {static_cast<int16_t>(ring + part.triangles[i]), static_cast<int16_t>(ring + part.triangles[i + 1]),
                     static_cast<int16_t>(ring + part.triangles[i + 2])});
      continue;
    }
    // Otherwise the face is a fan around its center: ring, center, next ring, center, ... back to the first.
    std::vector<int16_t> fan;
    for (int i = 0; i < count; ++i) {
      fan.push_back(static_cast<int16_t>(ring + i));
      fan.push_back(static_cast<int16_t>(center));
    }
    fan.push_back(static_cast<int16_t>(ring));
    AppendStrip(mesh, fan);
  }

  std::vector<int16_t> wall;
  for (int i = 0; i <= count; ++i) {
    wall.push_back(static_cast<int16_t>(front_ring + i % count));
    wall.push_back(static_cast<int16_t>(back_ring + i % count));
  }
  AppendStrip(mesh, wall);
}

std::vector<Part> SkullParts() {
  const std::vector<Point> jaw = {{-0.78f, 1.0f}, {-0.72f, 0.3f}, {-0.55f, 0.06f}, {-0.3f, 0},
                                  {0.3f, 0},      {0.55f, 0.06f}, {0.72f, 0.3f},   {0.78f, 1.0f}};
  std::vector<Part> parts;
  parts.push_back({Ellipse({0, 1.6f}, 1.15f, 1.05f, 48), {0, 1.6f}});  // Cranium.
  parts.push_back({jaw, {0, 0.5f}});
  for (float x : {-0.44f, 0.44f})  // Eye sockets.
    parts.push_back(Detail(Ellipse({x, 1.45f}, 0.3f, 0.28f, 24), {x, 1.45f}, Tone::Dark));
  parts.push_back(Detail({{0, 1.12f}, {-0.15f, 0.84f}, {0.15f, 0.84f}}, {0, 0.93f}, Tone::Dark));  // Nose.
  for (float x : {-0.36f, -0.12f, 0.12f, 0.36f})  // Gaps between the teeth.
    parts.push_back(Detail(Rect(x - 0.035f, 0.12f, x + 0.035f, 0.55f), {x, 0.335f}, Tone::Dark));
  return parts;
}

std::vector<Part> CrossParts() {
  const Point center = {0, 1.256f};  // Puts the lowest corners at the bottom.
  return {{Bar(center, 3.0f, 0.55f, kPi / 4), center}, {Bar(center, 3.0f, 0.55f, -kPi / 4), center}};
}

std::vector<Part> SwordParts() {
  // Points down, like the arrow.
  const std::vector<Point> blade = {{0, 0}, {0.24f, 0.4f}, {0.24f, 1.72f}, {-0.24f, 1.72f}, {-0.24f, 0.4f}};
  const std::vector<Point> guard = {{-0.9f, 1.84f}, {-0.78f, 1.72f}, {0.78f, 1.72f},
                                    {0.9f, 1.84f},  {0.78f, 1.96f},  {-0.78f, 1.96f}};
  std::vector<Part> parts;
  parts.push_back({blade, {0, 1.0f}, Tone::Light});
  parts.push_back(Detail(Rect(-0.05f, 0.45f, 0.05f, 1.6f), {0, 1.0f}, Tone::Dark));  // Fuller.
  parts.push_back({guard, {0, 1.84f}});
  parts.push_back({Rect(-0.12f, 1.96f, 0.12f, 2.46f), {0, 2.21f}});      // Grip.
  parts.push_back({Ellipse({0, 2.58f}, 0.18f, 0.18f, 20), {0, 2.58f}});  // Pommel.
  return parts;
}

std::vector<Part> DiamondParts() {
  const Point center = {0, 1.35f};
  return {{{{0, 0}, {1.05f, 1.35f}, {0, 2.7f}, {-1.05f, 1.35f}}, center},
          Detail({{0, 0.6f}, {0.5f, 1.35f}, {0, 2.1f}, {-0.5f, 1.35f}}, center, Tone::Light)};  // Facet.
}

std::vector<Part> FlameParts() {
  // A body, a tongue either side, and a lighter core.
  return {{Teardrop({0, 0.95f}, 0.95f, {0.12f, 2.8f}, 32), {0, 0.95f}},
          {Teardrop({-0.62f, 0.72f}, 0.5f, {-1.1f, 2.0f}, 20), {-0.62f, 0.72f}},
          {Teardrop({0.62f, 0.66f}, 0.45f, {1.05f, 1.8f}, 20), {0.62f, 0.66f}},
          Detail(Teardrop({0.02f, 0.56f}, 0.42f, {-0.14f, 1.75f}, 24), {0.02f, 0.56f}, Tone::Light)};
}

std::vector<Part> StarParts() {
  const Point center = {0, 1.095f};  // Puts the two lower points at the bottom.
  return {{Star(center, 1.35f, 0.55f, 5), center}, Detail(Star(center, 0.62f, 0.25f, 5), center, Tone::Light)};
}

std::vector<Part> MoonParts() {
  // A full moon: the shaded rim shows as a crescent where the lit disc doesn't cover it, plus craters.
  const Point center = {0, 1.3f};
  std::vector<Part> parts;
  parts.push_back({Ellipse(center, 1.3f, 1.3f, 48), center, Tone::Shade});
  parts.push_back(Raised(Ellipse({-0.12f, 1.36f}, 1.17f, 1.17f, 48), {-0.12f, 1.36f}, Tone::Base, 1));

  struct Crater {
    float x, z, r;
  };

  for (const auto &c : {Crater{0.42f, 1.85f, 0.27f}, Crater{-0.45f, 0.85f, 0.28f}, Crater{-0.62f, 1.62f, 0.16f},
                        Crater{0.52f, 1.0f, 0.16f}, Crater{0.22f, 0.5f, 0.15f}, Crater{-0.22f, 2.12f, 0.09f},
                        Crater{-0.1f, 1.42f, 0.09f}, Crater{0.12f, 1.05f, 0.09f}, Crater{-0.95f, 1.2f, 0.09f},
                        Crater{-0.38f, 0.42f, 0.09f}})
    parts.push_back(Raised(Ellipse({c.x, c.z}, c.r, c.r, 20), {c.x, c.z}, Tone::Shade, 2));
  return parts;
}

std::vector<Part> ShieldParts() {
  // A heater shield: a darker rim, the face, and a cross band (the tank's mark).
  std::vector<Point> outline = {{-1.1f, 2.7f},   {1.1f, 2.7f},   {1.1f, 1.7f}, {1.02f, 1.2f},
                                {0.82f, 0.72f},  {0.52f, 0.3f},  {0, 0},       {-0.52f, 0.3f},
                                {-0.82f, 0.72f}, {-1.02f, 1.2f}, {-1.1f, 1.7f}};
  const Point center = {0, 1.6f};
  std::vector<Point> face;
  for (const auto &point : outline)  // The face: the outline drawn in toward the center.
    face.push_back({center.x + (point.x - center.x) * 0.86f, center.z + (point.z - center.z) * 0.9f});
  std::vector<Part> parts;
  parts.push_back({outline, center, Tone::Shade});
  parts.push_back(Raised(face, center, Tone::Base, 1));
  parts.push_back(Raised(Rect(-0.13f, 0.42f, 0.13f, 2.5f), {0, 1.46f}, Tone::Shade, 2));     // Upright band.
  parts.push_back(Raised(Rect(-0.9f, 1.72f, 0.9f, 1.98f), {0, 1.85f}, Tone::Shade, 2));      // Cross band.
  parts.push_back(Raised(Ellipse({0, 1.85f}, 0.2f, 0.2f, 16), {0, 1.85f}, Tone::Light, 3));  // Boss.
  return parts;
}

// Block digits for the numbered badges: seven-segment strokes that overlap at the corners, so a digit is a
// few rectangles. '1' is a stem with a flag rather than the right-hand segments, so it reads as a one.
constexpr float kDigitWidth = 0.64f;
constexpr float kDigitHeight = 1.3f;
constexpr float kStroke = 0.2f;

std::vector<std::vector<Point>> DigitStrokes(int digit, float x0, float z0) {
  const float x1 = x0 + kDigitWidth, z1 = z0 + kDigitHeight, zm = z0 + kDigitHeight / 2, t = kStroke;
  if (digit == 1) {
    const float stem = x0 + kDigitWidth * 0.55f;
    return {Rect(stem - t / 2, z0, stem + t / 2, z1), Rect(stem - t / 2 - 0.2f, z1 - t, stem - t / 2, z1)};
  }
  // Segments a (top), b (upper right), c (lower right), d (bottom), e (lower left), f (upper left), g (middle).
  const std::vector<Point> segments[7] = {Rect(x0, z1 - t, x1, z1),
                                          Rect(x1 - t, zm - t / 2, x1, z1),
                                          Rect(x1 - t, z0, x1, zm + t / 2),
                                          Rect(x0, z0, x1, z0 + t),
                                          Rect(x0, z0, x0 + t, zm + t / 2),
                                          Rect(x0, zm - t / 2, x0 + t, z1),
                                          Rect(x0, zm - t / 2, x1, zm + t / 2)};
  static constexpr const char *kLit[10] = {"abcdef", "bc",     "abdeg", "abcdg",   "bcfg",
                                           "acdfg",  "acdefg", "abc",   "abcdefg", "abcdfg"};
  std::vector<std::vector<Point>> strokes;
  for (const char *segment = kLit[digit]; *segment; ++segment) strokes.push_back(segments[*segment - 'a']);
  return strokes;
}

Point Centroid(const std::vector<Point> &points) {
  Point sum = {0, 0};
  for (const auto &point : points) sum = {sum.x + point.x, sum.z + point.z};
  return {sum.x / static_cast<float>(points.size()), sum.z / static_cast<float>(points.size())};
}

std::vector<Point> Mirror(const std::vector<Point> &points) {
  std::vector<Point> mirrored;
  for (const auto &point : points) mirrored.push_back({-point.x, point.z});
  return mirrored;
}

std::vector<Part> NumberParts(int number) {
  const Point center = {0, 1.25f};
  std::vector<Part> parts;
  parts.push_back({Ellipse(center, 1.25f, 1.25f, 48), center});  // The badge.
  const std::string digits = std::to_string(number);
  const float gap = 0.14f;
  const float width = static_cast<float>(digits.size()) * kDigitWidth + static_cast<float>(digits.size() - 1) * gap;
  float x = -width / 2;
  for (char digit : digits) {
    for (const auto &stroke : DigitStrokes(digit - '0', x, center.z - kDigitHeight / 2)) {
      // Each face gets its own copy, the back one mirrored, so the number reads correctly from either side.
      const auto back = Mirror(stroke);
      parts.push_back(
          {stroke, Centroid(stroke), Tone::Contrast, -(kHalfThickness + kProud), -(kHalfThickness - kInset)});
      parts.push_back({back, Centroid(back), Tone::Contrast, kHalfThickness - kInset, kHalfThickness + kProud});
    }
    x += kDigitWidth + gap;
  }
  return parts;
}

}  // namespace

Mesh Build(Kind kind) {
  std::vector<Part> parts;
  switch (kind) {
    case Kind::Skull:
      parts = SkullParts();
      break;
    case Kind::Cross:
      parts = CrossParts();
      break;
    case Kind::Sword:
      parts = SwordParts();
      break;
    case Kind::Diamond:
      parts = DiamondParts();
      break;
    case Kind::Flame:
      parts = FlameParts();
      break;
    case Kind::Star:
      parts = StarParts();
      break;
    case Kind::Moon:
      parts = MoonParts();
      break;
    case Kind::Shield:
      parts = ShieldParts();
      break;
    default:
      if (kind >= Kind::Number1 && kind <= Kind::Number12)
        parts = NumberParts(static_cast<int>(kind) - static_cast<int>(Kind::Number1) + 1);
      break;
  }

  Mesh mesh;
  for (const auto &part : parts) AppendPart(mesh, part);
  if (!mesh.vertices.empty()) {
    mesh.min_z = mesh.max_z = mesh.vertices.front().z;
    for (const auto &vertex : mesh.vertices) {
      mesh.min_z = (vertex.z < mesh.min_z) ? vertex.z : mesh.min_z;
      mesh.max_z = (vertex.z > mesh.max_z) ? vertex.z : mesh.max_z;
    }
  }
  return mesh;
}

}  // namespace TagShapes
