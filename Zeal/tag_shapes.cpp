#include "tag_shapes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
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

// The wolf is traced from Wolf Pack's landing-page wolf (WolfParts(), generated data).
#include "tag_shapes_wolf.inc"

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

// A rope or line of the given width along a polyline, as one quad per segment (each convex). Segment ends
// overlap a little so bends don't show gaps.
std::vector<Part> Stroke(const std::vector<Point> &points, float width, Tone tone, int level, bool closed) {
  std::vector<Part> parts;
  const size_t count = closed ? points.size() : points.size() - 1;
  for (size_t i = 0; i < count; ++i) {
    const Point a = points[i], b = points[(i + 1) % points.size()];
    const float dx = b.x - a.x, dz = b.z - a.z;
    const float length = sqrtf(dx * dx + dz * dz);
    if (length <= 0) continue;
    const Point center = {(a.x + b.x) / 2, (a.z + b.z) / 2};
    auto part = Raised(Bar(center, length + width * 0.6f, width, atan2f(dz, dx)), center, tone, level);
    parts.push_back(std::move(part));
  }
  return parts;
}

std::vector<Part> LassoParts() {
  // A rope loop over a tail that runs down to a knot and a handle (for the puller's target).
  std::vector<Point> loop = Ellipse({0, 2.25f}, 1.25f, 0.55f, 36);
  const std::vector<Point> tail = {{0.05f, 1.7f},   {-0.2f, 1.5f},   {-0.36f, 1.22f},
                                   {-0.36f, 0.95f}, {-0.22f, 0.75f}, {0.0f, 0.62f}};
  std::vector<Part> parts;
  for (auto &part : Stroke(loop, 0.2f, Tone::Base, 0, true)) parts.push_back(std::move(part));
  for (auto &part : Stroke(tail, 0.2f, Tone::Base, 0, false)) parts.push_back(std::move(part));
  for (auto &part : Stroke(loop, 0.07f, Tone::Light, 1, true)) parts.push_back(std::move(part));  // Highlight.
  for (auto &part : Stroke(tail, 0.07f, Tone::Light, 1, false)) parts.push_back(std::move(part));
  parts.push_back(Raised(Ellipse({0.02f, 0.6f}, 0.17f, 0.17f, 16), {0.02f, 0.6f}, Tone::Base, 1));  // Knot.
  const Point grip = {0.3f, 0.3f};
  parts.push_back({Bar(grip, 0.6f, 0.2f, -0.8f), grip, Tone::Dark});                                // Handle.
  parts.push_back(Raised(Ellipse({0.5f, 0.11f}, 0.11f, 0.11f, 12), {0.5f, 0.11f}, Tone::Base, 1));  // End cap.
  return parts;
}

// Points along a circular arc from angle a0 to a1 (radians, counter-clockwise when a1 > a0).
std::vector<Point> Arc(Point center, float r, float a0, float a1, int count) {
  std::vector<Point> points;
  for (int i = 0; i <= count; ++i) {
    const float angle = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(count);
    points.push_back({center.x + r * cosf(angle), center.z + r * sinf(angle)});
  }
  return points;
}

// A thick stroke with a thin highlight along it, like the lasso's rope.
void AppendStroke(std::vector<Part> &parts, const std::vector<Point> &points, float width) {
  for (auto &part : Stroke(points, width, Tone::Base, 0, false)) parts.push_back(std::move(part));
  for (auto &part : Stroke(points, width * 0.3f, Tone::Light, 1, false)) parts.push_back(std::move(part));
}

std::vector<Part> DollarParts() {
  // An S of two arcs with a bar through it.
  constexpr float kDeg = kPi / 180;
  std::vector<Point> s = Arc({0, 1.78f}, 0.5f, 25 * kDeg, 270 * kDeg, 16);
  const auto lower = Arc({0, 0.78f}, 0.5f, 90 * kDeg, -155 * kDeg, 16);
  s.insert(s.end(), lower.begin() + 1, lower.end());
  std::vector<Part> parts;
  parts.push_back({Rect(-0.09f, 0, 0.09f, 2.55f), {0, 1.28f}, Tone::Base});
  AppendStroke(parts, s, 0.28f);
  return parts;
}

std::vector<Part> EuroParts() {
  // A C open to the right, crossed by two bars.
  constexpr float kDeg = kPi / 180;
  const Point center = {0.1f, 1.16f};
  std::vector<Part> parts;
  AppendStroke(parts, Arc(center, 1.0f, 45 * kDeg, 315 * kDeg, 24), 0.28f);
  for (float z : {1.34f, 0.98f})
    parts.push_back(Raised(Rect(-1.2f, z - 0.09f, 0.45f, z + 0.09f), {-0.4f, z}, Tone::Base, 1));
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

// A part turned counter-clockwise by angle about the origin. Turning keeps a part's winding, so its fan still
// faces the right way. `lift` thickens it on both faces.
Part Turned(Part part, float angle, float lift) {
  const float c = cosf(angle), s = sinf(angle);
  auto turn = [&](Point point) { return Point{point.x * c - point.z * s, point.x * s + point.z * c}; };
  for (auto &point : part.outline) point = turn(point);
  part.center = turn(part.center);
  part.y_front -= lift;
  part.y_back += lift;
  return part;
}

std::vector<Part> CrossedSwordsTargetParts() {
  // The main assist: two identical straight swords crossed in an X, blades up and hilts down, with a target (a
  // ring around a dot, on a dark backing) exactly where their axes cross. Each sword is built upright with its
  // axis through the origin (pointed tip at the top, then the crossguard, grip and pommel) and turned 45 degrees
  // either way, so the crossing is the origin and the whole shape is mirror symmetric. The second sword is a
  // little thicker so the faces do not z-fight where they overlap, and the target stands out of both.
  constexpr float kBladeHalfWidth = 0.15f;
  constexpr float kTip = 1.9f, kPoint = 1.6f;  // The tip's height and where the taper ends.
  constexpr float kGuardZ = -0.6f;             // Where the blade ends and the crossguard sits.
  constexpr float kPommelZ = -1.7f;            // Puts the pommels as far below the crossing as the tips are above.
  std::vector<Part> sword;
  sword.push_back({{{-kBladeHalfWidth, kGuardZ}, {kBladeHalfWidth, kGuardZ}, {kBladeHalfWidth, kPoint}, {0, kTip},
                    {-kBladeHalfWidth, kPoint}},
                   {0, 0.5f},
                   Tone::Light});
  sword.push_back({Rect(-0.45f, kGuardZ - 0.14f, 0.45f, kGuardZ), {0, kGuardZ - 0.07f}});  // Crossguard.
  sword.push_back({Rect(-0.07f, kPommelZ, 0.07f, kGuardZ - 0.14f), {0, -1.1f}});            // Grip.
  sword.push_back({Ellipse({0, kPommelZ}, 0.15f, 0.15f, 14), {0, kPommelZ}});               // Pommel.

  std::vector<Part> parts;
  int index = 0;
  for (float angle : {-kPi / 4, kPi / 4}) {  // Tip up and to the right, then up and to the left.
    for (const auto &part : sword) parts.push_back(Turned(part, angle, 0.045f * static_cast<float>(index)));
    ++index;
  }
  // Level 3 and up stands out of the thicker sword (level 1.5).
  const Point cross = {0, 0};
  parts.push_back(Raised(Ellipse(cross, 0.58f, 0.58f, 20), cross, Tone::Dark, 3));  // A dark outline and backing.
  for (auto &part : Stroke(Ellipse(cross, 0.44f, 0.44f, 16), 0.12f, Tone::Light, 4, true))
    parts.push_back(std::move(part));
  parts.push_back(Raised(Ellipse(cross, 0.13f, 0.13f, 12), cross, Tone::Light, 5));  // The dot.

  float lowest = 0;  // Sit the lowest point on z = 0 (the shape is already centered across x).
  for (const auto &part : parts)
    for (const auto &point : part.outline) lowest = (point.z < lowest) ? point.z : lowest;
  for (auto &part : parts) {
    for (auto &point : part.outline) point.z -= lowest;
    part.center.z -= lowest;
  }
  return parts;
}

std::vector<Part> HourglassParts() {
  // Slow: two glass triangles tip to tip between a top and a bottom plate and two posts, with sand piled in
  // the lower bulb, a thin stream through the neck and a little still in the upper one.
  constexpr float kHalfWidth = 0.65f;  // Of each glass bulb at its plate.
  constexpr float kBottom = 0.2f, kNeck = 1.35f, kTop = 2.5f;
  // The half width of the lower and upper bulbs at height z.
  auto lower = [&](float z) { return kHalfWidth * (kNeck - z) / (kNeck - kBottom); };
  auto upper = [&](float z) { return kHalfWidth * (z - kNeck) / (kTop - kNeck); };
  std::vector<Part> parts;
  parts.push_back({Rect(-0.85f, 0, 0.85f, kBottom), {0, kBottom / 2}, Tone::Shade});  // Plates.
  parts.push_back({Rect(-0.85f, kTop, 0.85f, kTop + kBottom), {0, kTop + kBottom / 2}, Tone::Shade});
  for (float x : {-0.72f, 0.72f})  // Posts.
    parts.push_back({Rect(x - 0.07f, kBottom, x + 0.07f, kTop), {x, (kBottom + kTop) / 2}, Tone::Shade});
  parts.push_back(Raised({{-kHalfWidth, kBottom}, {kHalfWidth, kBottom}, {0, kNeck}}, {0, 0.6f}, Tone::Light, 1));
  parts.push_back(Raised({{0, kNeck}, {kHalfWidth, kTop}, {-kHalfWidth, kTop}}, {0, 2.1f}, Tone::Light, 1));
  const float pile = 0.85f;  // Height of the sand in the lower bulb.
  parts.push_back(Raised({{-kHalfWidth, kBottom}, {kHalfWidth, kBottom}, {lower(pile), pile}, {-lower(pile), pile}},
                         {0, 0.5f}, Tone::Base, 2));
  parts.push_back(Raised(Rect(-0.04f, pile, 0.04f, 1.55f), {0, 1.2f}, Tone::Base, 2));  // Stream.
  parts.push_back(Raised({{0, kNeck}, {upper(1.75f), 1.75f}, {-upper(1.75f), 1.75f}}, {0, 1.6f}, Tone::Base, 2));
  return parts;
}

std::vector<Part> LuteParts() {
  // A lute, built upright (neck up) and then tilted like the bard's instrument in hand.
  std::vector<Part> parts;
  const Point body = {0, 0.85f};
  parts.push_back({Teardrop(body, 0.86f, {0, 2.3f}, 32), body, Tone::Light});  // Rim.
  parts.push_back(Raised(Teardrop(body, 0.78f, {0, 2.2f}, 32), body, Tone::Base, 1));
  parts.push_back(Raised(Rect(-0.13f, 1.9f, 0.13f, 3.05f), {0, 2.5f}, Tone::Base, 1));  // Neck.
  parts.push_back({Bar({0.08f, 3.2f}, 0.4f, 0.2f, 1.2f), {0.08f, 3.2f}, Tone::Base});   // Pegbox.
  for (float z : {2.95f, 3.1f, 3.25f})                                                  // Pegs.
    for (float side : {-1.0f, 1.0f})
      parts.push_back(
          Raised(Ellipse({side * 0.28f + 0.05f, z}, 0.07f, 0.07f, 10), {side * 0.28f + 0.05f, z}, Tone::Light, 1));
  parts.push_back(Raised(Ellipse({0, 1.2f}, 0.28f, 0.28f, 24), {0, 1.2f}, Tone::Dark, 2));  // Rosette.
  parts.push_back(Raised(Ellipse({0, 1.2f}, 0.17f, 0.17f, 20), {0, 1.2f}, Tone::Light, 3));
  parts.push_back(Raised(Ellipse({0, 1.2f}, 0.06f, 0.06f, 12), {0, 1.2f}, Tone::Dark, 4));
  parts.push_back(Raised(Rect(-0.26f, 0.42f, 0.26f, 0.5f), {0, 0.46f}, Tone::Dark, 2));  // Bridge.
  for (float x : {-0.08f, 0.0f, 0.08f})                                                  // Strings.
    parts.push_back(Raised(Rect(x - 0.012f, 0.46f, x + 0.012f, 3.0f), {x, 1.7f}, Tone::Light, 3));

  // Tilt 40 degrees to the right about the base, then sit the lowest point on z = 0.
  const float angle = -0.7f, c = cosf(angle), s = sinf(angle);
  const auto tilt = [c, s](Point p) { return Point{p.x * c - p.z * s, p.x * s + p.z * c}; };
  float min_z = 1e9f, min_x = 1e9f, max_x = -1e9f;
  for (auto &part : parts) {
    for (auto &point : part.outline) {
      point = tilt(point);
      min_z = fminf(min_z, point.z);
      min_x = fminf(min_x, point.x);
      max_x = fmaxf(max_x, point.x);
    }
    part.center = tilt(part.center);
  }
  const float shift_x = -(min_x + max_x) / 2;
  for (auto &part : parts) {
    for (auto &point : part.outline) point = {point.x + shift_x, point.z - min_z};
    part.center = {part.center.x + shift_x, part.center.z - min_z};
  }
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

// 5x7 block font for the letters and digits drawn on the pet paw (a charmer's initial), and the guild banners
// (index 36 is an ampersand, which GlyphIndex does not return). Each row is five bits, most significant on
// the left, top row first.
constexpr uint8_t kGlyphRows[37][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},      {14, 17, 1, 2, 4, 8, 31},      // 0 1 2
    {31, 2, 4, 2, 1, 17, 14},     {2, 6, 10, 18, 31, 2, 2},     {31, 16, 30, 1, 1, 17, 14},    // 3 4 5
    {6, 8, 16, 30, 17, 17, 14},   {31, 1, 2, 4, 8, 8, 8},       {14, 17, 17, 14, 17, 17, 14},  // 6 7 8
    {14, 17, 17, 15, 1, 2, 12},   {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},  // 9 A B
    {14, 17, 16, 16, 16, 17, 14}, {28, 18, 17, 17, 17, 18, 28}, {31, 16, 16, 30, 16, 16, 31},  // C D E
    {31, 16, 16, 30, 16, 16, 16}, {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},  // F G H
    {14, 4, 4, 4, 4, 4, 14},      {7, 2, 2, 2, 2, 18, 12},      {17, 18, 20, 24, 20, 18, 17},  // I J K
    {16, 16, 16, 16, 16, 16, 31}, {17, 27, 21, 21, 17, 17, 17}, {17, 17, 25, 21, 19, 17, 17},  // L M N
    {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13},  // O P Q
    {30, 17, 17, 30, 20, 18, 17}, {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},        // R S T
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10},  // U V W
    {17, 17, 10, 4, 10, 17, 17},  {17, 17, 17, 10, 4, 4, 4},    {31, 1, 2, 4, 8, 16, 31},      // X Y Z
    {12, 18, 20, 8, 21, 18, 13}};                                                              // & (banner names only)

// A 5x7 glyph with its top left corner at (x0, z_top), `cell` per font cell, standing `proud` out of each
// face. Runs of lit cells become rectangles, and a run repeated on the rows below grows into one taller
// rectangle.
std::vector<Part> GlyphRects(int index, float x0, float z_top, float cell, Tone tone, float proud) {
  struct Run {
    int start, end, top, bottom;
  };

  std::vector<Run> runs;
  for (int row = 0; row < 7; ++row) {
    const int bits = kGlyphRows[index][row];
    for (int col = 0; col < 5;) {
      if (!(bits & (16 >> col))) {
        ++col;
        continue;
      }
      int end = col;
      while (end + 1 < 5 && (bits & (16 >> (end + 1)))) ++end;
      bool grown = false;
      for (auto &run : runs)
        if (run.start == col && run.end == end && run.bottom == row - 1) {
          run.bottom = row;
          grown = true;
          break;
        }
      if (!grown) runs.push_back({col, end, row, row});
      col = end + 1;
    }
  }
  std::vector<Part> parts;
  for (const auto &run : runs) {
    const auto front =
        Rect(x0 + static_cast<float>(run.start) * cell, z_top - static_cast<float>(run.bottom + 1) * cell,
             x0 + static_cast<float>(run.end + 1) * cell, z_top - static_cast<float>(run.top) * cell);
    // Like the badges: a copy on each face, the back one mirrored, so it reads correctly from either side.
    const auto back = Mirror(front);
    parts.push_back({front, Centroid(front), tone, -(kHalfThickness + proud), -(kHalfThickness - kInset)});
    parts.push_back({back, Centroid(back), tone, kHalfThickness - kInset, kHalfThickness + proud});
  }
  return parts;
}

std::vector<Part> GlyphParts(int index) {
  // Sits on the paw's main pad (x -1.05..1.05, z 0..1.19). Dark rather than contrast: a dark letter reads
  // best on the paw's green.
  constexpr float kCell = 0.13f;
  return GlyphRects(index, -2.5f * kCell, 0.6f + 3.5f * kCell, kCell, Tone::Dark, kProud);
}

// Guild banners and icons ---------------------------------------------------------------------------------

constexpr float kDeg = kPi / 180;

// Ear-clipping triangulation of a simple polygon, for the concave outlines (a crescent, a bolt, a wing).
std::vector<int16_t> Triangulate(const std::vector<Point> &outline) {
  const auto cross = [](Point o, Point a, Point b) { return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x); };
  std::vector<int> remaining;
  float area = 0;
  for (size_t i = 0; i < outline.size(); ++i) {
    remaining.push_back(static_cast<int>(i));
    const Point a = outline[i], b = outline[(i + 1) % outline.size()];
    area += a.x * b.z - b.x * a.z;
  }
  if (area < 0) std::reverse(remaining.begin(), remaining.end());  // Clip ears counter-clockwise.

  std::vector<int16_t> triangles;
  while (remaining.size() > 3) {
    const size_t n = remaining.size();
    size_t ear = n;
    for (size_t i = 0; i < n && ear == n; ++i) {
      const int ia = remaining[(i + n - 1) % n], ib = remaining[i], ic = remaining[(i + 1) % n];
      const Point a = outline[ia], b = outline[ib], c = outline[ic];
      if (cross(a, b, c) <= 1e-9f) continue;  // Reflex or flat.
      bool contains = false;
      for (int j : remaining) {
        const Point p = outline[j];
        if (j == ia || j == ib || j == ic) continue;
        if (cross(a, b, p) >= 0 && cross(b, c, p) >= 0 && cross(c, a, p) >= 0) {
          contains = true;
          break;
        }
      }
      if (!contains) ear = i;
    }
    if (ear == n) {  // Only a degenerate spot is left: drop the flattest vertex and carry on.
      float flattest = 1e9f;
      for (size_t i = 0; i < n; ++i) {
        const float f =
            fabsf(cross(outline[remaining[(i + n - 1) % n]], outline[remaining[i]], outline[remaining[(i + 1) % n]]));
        if (f < flattest) {
          flattest = f;
          ear = i;
        }
      }
      remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(ear));
      continue;
    }
    for (size_t k : {(ear + n - 1) % n, ear, (ear + 1) % n}) triangles.push_back(static_cast<int16_t>(remaining[k]));
    remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(ear));
  }
  if (remaining.size() == 3)
    for (int k : remaining) triangles.push_back(static_cast<int16_t>(k));
  return triangles;
}

Part Concave(std::vector<Point> outline, Tone tone, int level) {
  Part part = Raised(outline, Centroid(outline), tone, level);
  part.triangles = Triangulate(part.outline);
  return part;
}

Part Flat(std::vector<Point> outline, Tone tone, int level) {
  const Point center = Centroid(outline);
  return Raised(std::move(outline), center, tone, level);
}

void Append(std::vector<Part> &parts, std::vector<Part> more) {
  for (auto &part : more) parts.push_back(std::move(part));
}

Point Rotate(Point p, float angle, Point pivot) {
  const float c = cosf(angle), s = sinf(angle), dx = p.x - pivot.x, dz = p.z - pivot.z;
  return {pivot.x + dx * c - dz * s, pivot.z + dx * s + dz * c};
}

std::vector<Point> Rotate(std::vector<Point> points, float angle, Point pivot) {
  for (auto &point : points) point = Rotate(point, angle, pivot);
  return points;
}

void RotateParts(std::vector<Part> &parts, float angle, Point pivot) {
  for (auto &part : parts) {
    part.outline = Rotate(part.outline, angle, pivot);
    part.center = Rotate(part.center, angle, pivot);
  }
}

std::vector<Point> MirrorX(std::vector<Point> points, float side) {
  for (auto &point : points) point.x *= side;
  return points;
}

// A curved, tapering stroke from a to b as an outline. It bows `bend` to the left of a->b at its middle, and
// its width is width * profile(t) along it (0 makes a point).
template <typename Profile>
std::vector<Point> Tapered(Point a, Point b, float bend, float width, Profile profile) {
  constexpr int kCount = 14;
  const float dx = b.x - a.x, dz = b.z - a.z, length = sqrtf(dx * dx + dz * dz);
  const float nx = -dz / length, nz = dx / length;
  std::vector<Point> left, right;
  for (int i = 0; i <= kCount; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kCount);
    const float bow = bend * sinf(kPi * t);
    const Point middle = {a.x + dx * t + nx * bow, a.z + dz * t + nz * bow};
    const float w = width * profile(t) / 2;
    left.push_back({middle.x + nx * w, middle.z + nz * w});
    right.push_back({middle.x - nx * w, middle.z - nz * w});
  }
  if (profile(1.0f) < 1e-6f) right.pop_back();  // A pointed end is a single vertex.
  if (profile(0.0f) < 1e-6f) right.erase(right.begin());
  left.insert(left.end(), right.rbegin(), right.rend());
  return left;
}

// A petal (pointed at both ends) of the given length and half width, standing on base and tilted by angle.
std::vector<Point> Petal(float length, float width, float angle, Point base) {
  constexpr int kCount = 16;
  std::vector<Point> points;
  for (int i = 0; i <= kCount; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kCount);
    points.push_back({width * fmaxf(0.0f, sinf(kPi * t)), length * t});
  }
  for (int i = kCount - 1; i > 0; --i) points.push_back({-points[i].x, points[i].z});
  for (auto &point : points) point = Rotate({base.x + point.x, base.z + point.z}, angle, base);
  return points;
}

// A chain link: a rounded slot around center.
std::vector<Point> Stadium(Point center, float half, float r) {
  auto points = Arc({center.x + half, center.z}, r, -kPi / 2, kPi / 2, 12);
  const auto left = Arc({center.x - half, center.z}, r, kPi / 2, 3 * kPi / 2, 12);
  points.insert(points.end(), left.begin(), left.end());
  return points;
}

// Centers a finished shape across x and sits its lowest point on z = 0.
void Settle(std::vector<Part> &parts) {
  float min_x = 1e9f, max_x = -1e9f, min_z = 1e9f;
  for (const auto &part : parts)
    for (const auto &point : part.outline) {
      min_x = fminf(min_x, point.x);
      max_x = fmaxf(max_x, point.x);
      min_z = fminf(min_z, point.z);
    }
  const float shift_x = -(min_x + max_x) / 2;
  for (auto &part : parts) {
    for (auto &point : part.outline) point = {point.x + shift_x, point.z - min_z};
    part.center = {part.center.x + shift_x, part.center.z - min_z};
  }
}

// What a banner carries: the guild's code in letters, its logo (the same drawing as its ^I<code>^ icon), or
// the logo with the guild's name around it. Flip this one constant to change every banner.
enum class BannerStyle { Code, Logo, LogoName };
constexpr BannerStyle kBannerStyle = BannerStyle::LogoName;

std::vector<Part> GuildIconParts(int index);  // Defined with the guild icons below.

// The banner's field, where its mark goes (above the swallowtail notch).
constexpr float kFieldHalfWidth = 0.74f, kFieldBottom = 0.85f, kFieldTop = 2.52f;
// Name text, in font cells (a cell is one dot of the 5x7 letters). Letters are drawn as large as fits the width,
// but no larger than the largest (past it a bigger logo is worth more); below the smallest they are no longer
// read at raid distance (a cell of 0.03 is a letter 0.21 tall, 31% of the code's).
constexpr float kNameMaxCell = 0.055f, kNameMinCell = 0.03f, kNameWidth = 1.6f;
constexpr float kLineGap = 0.05f;       // Between two lines of text.
constexpr float kBlockGap = 0.1f;       // Between text and the logo (its rim takes some).
constexpr float kGoodLogoHeight = 0.6f;  // The logo gets this much room if the letters can stay readable,
constexpr float kMinLogoHeight = 0.4f;   // and is never squeezed below this.

// Width in font cells of a line of text: a letter is 5 cells plus a 1 cell gap, a space is 4, no trailing gap.
float TextCells(const std::string &text) {
  float cells = -1;
  for (char c : text) cells += (c == ' ') ? 4.0f : 6.0f;
  return cells;
}

int TextGlyph(char c) { return (c == '&') ? 36 : GlyphIndex(c); }

// Draws a line of text with its top left corner at (x0, z_top), in the banner's text color, mirrored on the back.
void AppendText(std::vector<Part> &parts, const std::string &text, float x0, float z_top, float cell) {
  float x = x0;
  for (char c : text) {
    if (const int glyph = TextGlyph(c); glyph >= 0)
      Append(parts, GlyphRects(glyph, x, z_top, cell, Tone::LogoText, 2 * kProud));
    x += ((c == ' ') ? 4.0f : 6.0f) * cell;
  }
}

// Lines of text above and below the logo, all drawn at one cell size.
struct NameLines {
  std::vector<std::string> above, below;
  float cell = 0;
  float logo_height = 0;
};

// Fills in the cell and the room left for the logo. The cell is as large as the widest line allows, but small
// enough to leave the logo kGoodLogoHeight (else, failing that, kMinLogoHeight). False when that cell is below
// the smallest readable size.
bool MeasureLines(NameLines &layout) {
  float widest = 0;
  const size_t lines = layout.above.size() + layout.below.size();
  for (const auto *side : {&layout.above, &layout.below})
    for (const auto &line : *side) widest = std::max(widest, TextCells(line));
  if (!lines) return false;
  const float gaps = static_cast<float>((layout.above.size() > 1 ? layout.above.size() - 1 : 0) +
                                        (layout.below.size() > 1 ? layout.below.size() - 1 : 0));
  const float blocks = (layout.above.empty() || layout.below.empty()) ? 1.0f : 2.0f;
  const float room = (kFieldTop - kFieldBottom) - kLineGap * gaps - kBlockGap * blocks;  // For logo and letters.
  const float fits = std::min(kNameMaxCell, kNameWidth / widest);
  layout.cell = std::min(fits, (room - kGoodLogoHeight) / (7 * static_cast<float>(lines)));
  if (layout.cell < kNameMinCell)
    layout.cell = std::min(fits, (room - kMinLogoHeight) / (7 * static_cast<float>(lines)));
  if (layout.cell < kNameMinCell) return false;
  layout.logo_height = room - 7 * layout.cell * static_cast<float>(lines);
  return true;
}

// Every way to put the words on one line or two (split at a word boundary).
std::vector<std::vector<std::string>> LineChoices(const std::vector<std::string> &words, size_t from, size_t to) {
  const auto join = [&](size_t a, size_t b) {
    std::string line;
    for (size_t i = a; i < b; ++i) line += (line.empty() ? "" : " ") + words[i];
    return line;
  };
  if (from == to) return {{}};
  std::vector<std::vector<std::string>> choices = {{join(from, to)}};
  for (size_t split = from + 1; split < to; ++split) choices.push_back({join(from, split), join(split, to)});
  return choices;
}

// The best layout for a guild's name: the whole name split into lines above and below the logo, preferring the
// largest smallest-line, then the largest logo. Guilds whose split is chosen by hand are listed here. False when
// no layout reaches the smallest readable size (the caller then falls back to the code).
bool ChooseNameLines(const std::string &code, const std::string &name, NameLines &best) {
  std::vector<std::string> words;
  std::string word;
  for (char c : name + " ") {
    if (c == ' ') {
      if (!word.empty()) words.push_back(word);
      word.clear();
    } else if (c != '\'') {
      word += c;
    }
  }
  struct Chosen {
    const char *code;
    std::vector<std::string> above, below;
  };
  static const Chosen kChosen[] = {{"HBM", {"HERE", "THERE"}, {"BE", "MONSTERS"}}};
  for (const auto &chosen : kChosen)
    if (code == chosen.code) {
      NameLines layout;
      layout.above = chosen.above;
      layout.below = chosen.below;
      if (!MeasureLines(layout)) return false;
      best = layout;
      return true;
    }
  bool found = false;
  for (int attempt = 0; attempt < 2 && !found && !words.empty(); ++attempt) {  // The second without "The".
    for (size_t split = 0; split <= words.size(); ++split)
      for (const auto &above : LineChoices(words, 0, split))
        for (const auto &below : LineChoices(words, split, words.size())) {
          NameLines layout;
          layout.above = above;
          layout.below = below;
          if (!MeasureLines(layout)) continue;
          if (!found || layout.cell > best.cell + 1e-4f ||
              (layout.cell > best.cell - 1e-4f && layout.logo_height > best.logo_height + 1e-4f)) {
            best = layout;
            found = true;
          }
        }
    if (words.size() < 2 || words.front() != "The") break;
    words.erase(words.begin());
  }
  return found;
}

// A guild's logo: its icon's drawing, or for a guild whose icon is an existing shape that shape's drawing.
std::vector<Part> LogoParts(int index) {
  auto parts = GuildIconParts(index);
  if (parts.empty() && kGuilds[index].icon_key) {
    const std::string key = kGuilds[index].icon_key;
    parts = (key == "WP") ? WolfParts() : (key == "E") ? EuroParts() : (key == "$") ? DollarParts() : parts;
  }
  return parts;
}

// Scales parts (keeping their shape) to fit the box and centres them in it, and stands them proud of the flag.
void FitParts(std::vector<Part> &parts, float x0, float z0, float x1, float z1) {
  float min_x = 1e9f, max_x = -1e9f, min_z = 1e9f, max_z = -1e9f;
  for (const auto &part : parts)
    for (const auto &point : part.outline) {
      min_x = std::min(min_x, point.x);
      max_x = std::max(max_x, point.x);
      min_z = std::min(min_z, point.z);
      max_z = std::max(max_z, point.z);
    }
  const float scale = std::min((x1 - x0) / (max_x - min_x), (z1 - z0) / (max_z - min_z));
  const Point from = {(min_x + max_x) / 2, (min_z + max_z) / 2}, to = {(x0 + x1) / 2, (z0 + z1) / 2};
  const auto map = [&](Point p) { return Point{to.x + (p.x - from.x) * scale, to.z + (p.z - from.z) * scale}; };
  constexpr float kLift = 0.05f;  // The field stands 0.03 out of the flag; the logo has to clear it.
  for (auto &part : parts) {
    for (auto &point : part.outline) point = map(point);
    part.center = map(part.center);
    part.y_front += (part.y_front < 0) ? -kLift : kLift;
    part.y_back += (part.y_back < 0) ? -kLift : kLift;
    switch (part.tone) {  // The icon's own accents become the logo's accents.
      case Tone::Base:
        part.tone = Tone::Logo;
        break;
      case Tone::Dark:
        part.tone = Tone::LogoDark;
        break;
      case Tone::Light:
        part.tone = Tone::LogoLight;
        break;
      case Tone::Shade:
        part.tone = Tone::LogoShade;
        break;
      default:  // Eye keeps its glow.
        break;
    }
  }
}

// A polygon's outline pushed outward by d (a miter at each corner, capped), whichever way it winds.
std::vector<Point> Offset(const std::vector<Point> &outline, float d) {
  float area = 0;
  for (size_t i = 0; i < outline.size(); ++i) {
    const Point a = outline[i], b = outline[(i + 1) % outline.size()];
    area += a.x * b.z - b.x * a.z;
  }
  const float side = (area >= 0) ? 1.0f : -1.0f;  // Counter-clockwise: the outside is to the right of each edge.
  const auto normal = [&](Point a, Point b) {
    const float dx = b.x - a.x, dz = b.z - a.z, length = sqrtf(dx * dx + dz * dz);
    return (length < 1e-6f) ? Point{0, 0} : Point{side * dz / length, -side * dx / length};
  };
  std::vector<Point> out;
  const size_t n = outline.size();
  for (size_t i = 0; i < n; ++i) {
    const Point prev = outline[(i + n - 1) % n], cur = outline[i], next = outline[(i + 1) % n];
    const Point n1 = normal(prev, cur), n2 = normal(cur, next);
    Point m = {n1.x + n2.x, n1.z + n2.z};
    const float length = sqrtf(m.x * m.x + m.z * m.z);
    if (length < 1e-4f) {
      out.push_back({cur.x + n1.x * d, cur.z + n1.z * d});
      continue;
    }
    m = {m.x / length, m.z / length};
    const float miter = std::min(2.0f, 1.0f / std::max(0.5f, m.x * n1.x + m.z * n1.z));
    out.push_back({cur.x + m.x * d * miter, cur.z + m.z * d * miter});
  }
  return out;
}

// A thin rim for a logo whose own colors do not stand out from the flag: every part a little larger, flat, in the
// banner's text color (near-black or near-white, whichever contrasts), just above the field and below the logo.
std::vector<Part> RimParts(const std::vector<Part> &logo) {
  constexpr float kRim = 0.035f;
  std::vector<Part> rim;
  for (const auto &part : logo) {
    Part copy = part;
    copy.outline = Offset(part.outline, kRim);
    copy.tone = Tone::LogoText;
    copy.y_front = -(kHalfThickness + 0.04f);
    copy.y_back = kHalfThickness + 0.04f;
    rim.push_back(std::move(copy));
  }
  return rim;
}

// The mark on a guild's banner. Appends parts a step above the field.
void AppendBannerMark(std::vector<Part> &parts, int index) {
  const std::string code = kGuilds[index].code;
  if (kBannerStyle == BannerStyle::Code) {
    const float count = static_cast<float>(code.size());
    const float cell = (code.size() > 2) ? 0.095f : 0.125f;
    float x = -(count * 5 + count - 1) * cell / 2;
    for (char c : code) {  // Letters a step above the field, or they would sit level with it.
      Append(parts, GlyphRects(GlyphIndex(c), x, 1.75f + 3.5f * cell, cell, Tone::Contrast, 2 * kProud));
      x += 6 * cell;
    }
    return;
  }
  auto logo = LogoParts(index);
  if (logo.empty()) {  // A guild with no drawing of its own (a picture takes its place): the code, as before.
    const float cell = 0.095f;
    AppendText(parts, code, -TextCells(code) * cell / 2, 1.75f + 3.5f * cell, cell);
    return;
  }
  float logo_bottom = kFieldBottom, logo_top = kFieldTop;  // The logo's box: what the name leaves.
  if (kBannerStyle == BannerStyle::LogoName) {
    NameLines text;
    if (!ChooseNameLines(code, kGuilds[index].name, text)) {  // Too long to read at the smallest size: the code.
      text.above.clear();
      text.below = {code};
      text.cell = 0.09f;
    }
    const float cell = text.cell;
    float z = kFieldTop;  // Above the logo, from the top down.
    for (const auto &line : text.above) {
      AppendText(parts, line, -TextCells(line) * cell / 2, z, cell);
      z -= 7 * cell + kLineGap;
    }
    if (!text.above.empty()) logo_top = z + kLineGap - kBlockGap;
    z = kFieldBottom;  // Below it, from the bottom up.
    for (size_t i = text.below.size(); i-- > 0;) {
      z += 7 * cell;
      AppendText(parts, text.below[i], -TextCells(text.below[i]) * cell / 2, z, cell);
      z += kLineGap;
    }
    if (!text.below.empty()) logo_bottom = z - kLineGap + kBlockGap;
  }
  FitParts(logo, -kFieldHalfWidth, logo_bottom, kFieldHalfWidth, logo_top);
  if (BannerLogoIsOutlined(index)) Append(parts, RimParts(logo));
  Append(parts, std::move(logo));
}

std::vector<Part> BannerParts(int index) {
  // A swallowtail flag in the guild's color (shaded border, raised field) hanging from a rod, with the guild's
  // logo (or code) on each face. Both outlines are star-shaped about (0, 1.5), above the notch.
  const std::vector<Point> flag = {{-0.95f, 0}, {0, 0.55f}, {0.95f, 0}, {0.95f, 2.75f}, {-0.95f, 2.75f}};
  const std::vector<Point> field = {{-0.83f, 0.2f}, {0, 0.7f}, {0.83f, 0.2f}, {0.83f, 2.63f}, {-0.83f, 2.63f}};
  std::vector<Part> parts;
  parts.push_back({flag, {0, 1.5f}, Tone::Shade});
  parts.push_back(Raised(field, {0, 1.5f}, Tone::Base, 1));
  parts.push_back(Flat(Rect(-1.15f, 2.75f, 1.15f, 2.9f), Tone::Dark, 0));  // The rod and its finials.
  for (float x : {-1.2f, 1.2f})
    parts.push_back(Raised(Ellipse({x, 2.825f}, 0.1f, 0.1f, 12), {x, 2.825f}, Tone::Dark, 1));
  AppendBannerMark(parts, index);
  return parts;
}

std::vector<Part> LightningParts() {  // Mayhem.
  return {
      Concave({{0.25f, 2.8f}, {-0.75f, 1.25f}, {-0.05f, 1.25f}, {-0.45f, 0}, {0.8f, 1.7f}, {0.1f, 1.7f}, {0.65f, 2.8f}},
              Tone::Base, 0)};
}

std::vector<Part> LotusParts() {  // Tranquility.

  struct Petals {
    float angle, length, width;
    Tone tone;
    int level;
  };

  std::vector<Part> parts;
  for (const auto &p : {Petals{1.3f, 1.25f, 0.32f, Tone::Shade, 0}, Petals{-1.3f, 1.25f, 0.32f, Tone::Shade, 0},
                        Petals{0.62f, 1.75f, 0.4f, Tone::Shade, 1}, Petals{-0.62f, 1.75f, 0.4f, Tone::Shade, 1},
                        Petals{0, 2.25f, 0.46f, Tone::Base, 2}})
    parts.push_back(Flat(Petal(p.length, p.width, p.angle, {0, 0.35f}), p.tone, p.level));
  Append(parts, Stroke({{-1.3f, 0.3f}, {1.3f, 0.3f}}, 0.12f, Tone::Light, 3, false));  // The water line.
  return parts;
}

std::vector<Part> AcornParts() {  // Squirrels of War.
  std::vector<Part> parts;
  parts.push_back(
      Concave({{-0.66f, 1.3f}, {-0.6f, 0.85f}, {-0.35f, 0.35f}, {0, 0}, {0.35f, 0.35f}, {0.6f, 0.85f}, {0.66f, 1.3f}},
              Tone::Base, 0));
  parts.push_back(Raised(Arc({0, 1.3f}, 0.8f, 0, kPi, 18), {0, 1.55f}, Tone::Shade, 1));  // The cap.
  parts.push_back(Raised(Rect(-0.07f, 2.05f, 0.07f, 2.45f), {0, 2.25f}, Tone::Shade, 1));
  Append(parts, Stroke(Arc({0, 1.3f}, 0.8f, 0.15f, kPi - 0.15f, 10), 0.05f, Tone::Dark, 2, false));
  return parts;
}

std::vector<Part> AnkhParts() {  // Intervention.
  const auto loop = Ellipse({0, 2.2f}, 0.42f, 0.56f, 24);
  std::vector<Part> parts = Stroke(loop, 0.26f, Tone::Base, 0, true);
  Append(parts, Stroke(loop, 0.07f, Tone::Light, 1, true));
  parts.push_back(Flat(Rect(-0.82f, 1.42f, 0.82f, 1.7f), Tone::Base, 0));
  parts.push_back(Flat({{-0.13f, 1.6f}, {0.13f, 1.6f}, {0.26f, 0}, {-0.26f, 0}}, Tone::Base, 0));
  return parts;
}

std::vector<Part> AnchorParts() {  // Erud's Crossing Guard.
  std::vector<Part> parts = Stroke(Ellipse({0, 2.52f}, 0.22f, 0.22f, 16), 0.12f, Tone::Base, 0, true);  // Ring.
  parts.push_back(Flat(Rect(-0.09f, 0.3f, 0.09f, 2.3f), Tone::Base, 0));                                // Shank.
  parts.push_back(Flat(Rect(-0.6f, 1.95f, 0.6f, 2.12f), Tone::Base, 0));                                // Stock.
  Append(parts, Stroke(Arc({0, 1.05f}, 0.95f, 200 * kDeg, 340 * kDeg, 14), 0.22f, Tone::Base, 0, false));
  for (float side : {-1.0f, 1.0f}) {  // Flukes.
    const float x = side * 0.95f * cosf(20 * kDeg), z = 1.05f - 0.95f * sinf(20 * kDeg);
    parts.push_back(Flat({{x + side * 0.05f, z + 0.45f}, {x - side * 0.2f, z - 0.1f}, {x + side * 0.26f, z - 0.02f}},
                         Tone::Base, 0));
  }
  return parts;
}

std::vector<Part> ClawParts() {  // Savage.
  const auto profile = [](float t) { return powf(fmaxf(0.0f, sinf(kPi * t)), 0.7f); };
  std::vector<Part> parts;
  for (float dx : {-0.5f, 0.0f, 0.5f})
    parts.push_back(Concave(Tapered({0.55f + dx, 2.7f}, {-0.75f + dx, 0.1f}, 0.18f, 0.34f, profile), Tone::Base, 0));
  return parts;
}

std::vector<Part> CigaretteParts() {  // Burnouts: a lit rolled cigarette lying at a slight diagonal, smoke rising.
  // Drawn level along x (filter left, ember right) about z = 0, then lifted and tilted so the ember end is up.
  const float hw = 0.27f;  // Half the width of the paper.
  std::vector<Part> parts;
  parts.push_back(Flat(Rect(-1.3f, -hw, -0.5f, hw), Tone::Base, 0));                    // Filter (the guild's tan).
  parts.push_back(Flat(Rect(-0.56f, -hw, -0.44f, hw), Tone::Dark, 1));                  // Tipping band.
  parts.push_back(Flat(Rect(-0.44f, -hw, 0.8f, hw), Tone::Light, 0));                   // White paper.
  parts.push_back(Flat(Rect(-0.44f, -hw, 0.8f, -hw + 0.08f), Tone::Shade, 1));          // Its shaded underside.
  parts.push_back(Flat(Rect(0.8f, -hw, 0.9f, hw), Tone::Dark, 1));                      // Charred edge.
  parts.push_back(Flat({{0.9f, -0.22f}, {1.16f, -0.18f}, {1.16f, 0.18f}, {0.9f, 0.22f}}, Tone::Shade, 1));  // Ash.
  parts.push_back(Raised(Ellipse({1.22f, 0}, 0.17f, 0.23f, 14), {1.22f, 0}, Tone::Eye, 2));                 // Ember.
  for (auto &part : parts) {
    for (auto &point : part.outline) point.z += 1.0f;
    part.center.z += 1.0f;
  }
  RotateParts(parts, 28 * kDeg, {0, 1.0f});
  const Point ember = Rotate({1.3f, 1.0f}, 28 * kDeg, {0, 1.0f});
  std::vector<Point> smoke;  // A wisp that curls as it rises.
  for (int i = 0; i <= 8; ++i) {
    const float t = static_cast<float>(i) / 8;
    smoke.push_back({ember.x + 0.22f * sinf(2.2f * kPi * t) * (0.4f + t) + 0.05f, ember.z + 0.18f + 1.25f * t});
  }
  Append(parts, Stroke(smoke, 0.1f, Tone::Light, 0, false));
  return parts;
}

std::vector<Part> CrownParts() {  // Former Glory: a toppled crown.
  std::vector<Part> parts;
  parts.push_back(Flat(Rect(-0.9f, 0.5f, 0.9f, 0.9f), Tone::Base, 0));
  parts.push_back(
      Concave({{-0.9f, 0.85f}, {0.9f, 0.85f}, {1.05f, 2.0f}, {0.5f, 1.35f}, {0, 2.25f}, {-0.5f, 1.35f}, {-1.05f, 2.0f}},
              Tone::Base, 0));
  for (const Point tip : {Point{-1.05f, 2.0f}, Point{0, 2.25f}, Point{1.05f, 2.0f}})
    parts.push_back(Raised(Ellipse(tip, 0.12f, 0.12f, 10), tip, Tone::Light, 1));
  for (float x : {-0.5f, 0.0f, 0.5f})
    parts.push_back(Raised(Ellipse({x, 0.7f}, 0.1f, 0.1f, 10), {x, 0.7f}, Tone::Dark, 1));
  RotateParts(parts, -18 * kDeg, {0, 1.2f});
  return parts;
}

std::vector<Part> DeltaParts() {  // Axiom.
  const std::vector<Point> triangle = {{0, 2.5f}, {-1.25f, 0.15f}, {1.25f, 0.15f}};
  std::vector<Part> parts = Stroke(triangle, 0.26f, Tone::Base, 0, true);
  Append(parts, Stroke(triangle, 0.07f, Tone::Light, 1, true));
  parts.push_back({Ellipse({0, 0.95f}, 0.24f, 0.24f, 16), {0, 0.95f}, Tone::Base});
  return parts;
}

std::vector<Part> HouseParts() {  // Haven: a house with its window lit.
  std::vector<Part> parts;
  parts.push_back(Flat(Rect(-0.85f, 0, 0.85f, 1.3f), Tone::Base, 0));
  parts.push_back(Flat(Rect(0.5f, 1.5f, 0.78f, 2.35f), Tone::Shade, 0));  // Chimney.
  parts.push_back(Flat({{-1.18f, 1.2f}, {1.18f, 1.2f}, {0, 2.3f}}, Tone::Shade, 1));
  parts.push_back(Flat(Rect(-0.22f, 0, 0.22f, 0.78f), Tone::Dark, 1));
  parts.push_back(Flat(Rect(0.36f, 0.62f, 0.68f, 0.98f), Tone::Eye, 1));
  return parts;
}

std::vector<Part> BirdParts() {  // Freedom.
  const auto wing = [](float t) { return powf(1 - t, 0.8f); };
  std::vector<Part> parts;
  parts.push_back(Concave(Tapered({-0.08f, 1.1f}, {-1.35f, 2.0f}, -0.32f, 0.34f, wing), Tone::Base, 0));
  parts.push_back(Concave(Tapered({0.08f, 1.1f}, {1.35f, 2.0f}, 0.32f, 0.34f, wing), Tone::Base, 0));
  parts.push_back({Ellipse({0, 1.08f}, 0.2f, 0.16f, 12), {0, 1.08f}, Tone::Base});
  return parts;
}

std::vector<Part> EyeParts() {  // Seekers of Souls.
  const float half_w = 1.3f, half_h = 0.62f, cz = 1.25f;
  const float r = (half_w * half_w + half_h * half_h) / (2 * half_h), span = asinf(half_w / r);
  auto lens = Arc({0, cz + half_h - r}, r, kPi / 2 - span, kPi / 2 + span, 18);
  const auto lower = Arc({0, cz - half_h + r}, r, 3 * kPi / 2 - span, 3 * kPi / 2 + span, 18);
  lens.insert(lens.end(), lower.begin() + 1, lower.end() - 1);
  std::vector<Part> parts;
  parts.push_back({lens, {0, cz}, Tone::Base});
  parts.push_back(Raised(Ellipse({0, cz}, 0.52f, 0.52f, 24), {0, cz}, Tone::Eye, 1));
  parts.push_back(Raised(Ellipse({0, cz}, 0.24f, 0.24f, 16), {0, cz}, Tone::Dark, 2));
  parts.push_back(Raised(Ellipse({0.16f, cz + 0.16f}, 0.08f, 0.08f, 10), {0.16f, cz + 0.16f}, Tone::Light, 3));
  return parts;
}

std::vector<Part> TankardParts() {  // Hardened Casuals.
  std::vector<Part> parts;
  parts.push_back(Flat(Rect(-0.6f, 0, 0.6f, 1.7f), Tone::Base, 0));
  Append(parts, Stroke(Arc({0.62f, 0.9f}, 0.45f, -80 * kDeg, 80 * kDeg, 10), 0.18f, Tone::Shade, 0, false));
  for (float z : {0.22f, 1.3f}) parts.push_back(Flat(Rect(-0.6f, z, 0.6f, z + 0.13f), Tone::Dark, 1));
  parts.push_back(Flat(Rect(-0.64f, 1.62f, 0.64f, 1.82f), Tone::Light, 1));  // Foam.

  struct Bubble {
    float x, z, rx, rz;
  };

  for (const auto &b :
       {Bubble{-0.35f, 1.85f, 0.34f, 0.26f}, Bubble{0.1f, 1.95f, 0.38f, 0.3f}, Bubble{0.45f, 1.83f, 0.3f, 0.24f}})
    parts.push_back(Raised(Ellipse({b.x, b.z}, b.rx, b.rz, 16), {b.x, b.z}, Tone::Light, 1));
  return parts;
}

std::vector<Part> CrescentParts() {  // Nocturnal (the round moon is the mez mark).
  const float big = 1.2f, small = 1.0f, offset = 0.55f, cz = 1.25f;
  const float x = (big * big - small * small + offset * offset) / (2 * offset);  // Where the circles cross.
  const float z = sqrtf(big * big - x * x);
  const float a = atan2f(z, x), b = atan2f(z, x - offset);
  auto outline = Arc({0, cz}, big, a, 2 * kPi - a, 30);
  const auto inner = Arc({offset, cz}, small, 2 * kPi - b, b, 24);
  outline.insert(outline.end(), inner.begin() + 1, inner.end() - 1);
  std::vector<Part> parts;
  parts.push_back(Concave(outline, Tone::Base, 0));
  parts.push_back(Raised(Ellipse({-0.86f, 1.55f}, 0.1f, 0.1f, 10), {-0.86f, 1.55f}, Tone::Shade, 1));
  parts.push_back(Raised(Ellipse({-0.8f, 0.95f}, 0.13f, 0.13f, 10), {-0.8f, 0.95f}, Tone::Shade, 1));
  return parts;
}

std::vector<Part> D20Parts() {  // Dungeons and Dragons.
  std::vector<Point> hexagon;
  for (int i = 0; i < 6; ++i) {
    const float angle = (90 + 60 * static_cast<float>(i)) * kDeg;
    hexagon.push_back({1.15f * cosf(angle), 1.3f + 1.3f * sinf(angle)});
  }
  const std::vector<Point> face = {{0, 2.05f}, {-0.72f, 0.8f}, {0.72f, 0.8f}};
  std::vector<Part> parts;
  parts.push_back({hexagon, {0, 1.3f}, Tone::Base});
  parts.push_back(Flat(face, Tone::Light, 1));
  const std::pair<int, int> edges[] = {{0, 0}, {1, 2}, {1, 3}, {2, 3}, {2, 4}, {0, 1}, {0, 5}, {1, 1}, {2, 5}};
  for (const auto &[f, h] : edges) Append(parts, Stroke({face[f], hexagon[h]}, 0.06f, Tone::Dark, 2, false));
  constexpr float kCell = 0.055f;
  float x = -11 * kCell / 2;
  for (char digit : {'2', '0'}) {
    Append(parts, GlyphRects(GlyphIndex(digit), x, 1.2f + 3.5f * kCell, kCell, Tone::Dark, 2 * kProud));
    x += 6 * kCell;
  }
  return parts;
}

std::vector<Part> AxeParts() {  // Zek.
  std::vector<Part> parts;
  parts.push_back(Flat(Rect(-0.08f, 0, 0.08f, 2.75f), Tone::Dark, 0));
  parts.push_back(Flat({{-0.08f, 2.75f}, {0.08f, 2.75f}, {0, 3.05f}}, Tone::Dark, 0));
  for (float side : {-1.0f, 1.0f}) {
    std::vector<Point> blade = {{0.08f, 2.35f}};
    const auto edge = Arc({0.3f, 2.1f}, 0.95f, 48 * kDeg, -48 * kDeg, 12);
    blade.insert(blade.end(), edge.begin(), edge.end());
    blade.push_back({0.08f, 1.85f});
    parts.push_back(Flat(MirrorX(blade, side), Tone::Base, 1));
    Append(parts,
           Stroke(MirrorX(Arc({0.3f, 2.1f}, 0.86f, 44 * kDeg, -44 * kDeg, 10), side), 0.06f, Tone::Light, 2, false));
  }
  return parts;
}

std::vector<Part> WaveParts() {  // The Drift.
  std::vector<Part> parts;
  for (int k = 0; k < 3; ++k) {
    std::vector<Point> wave;
    for (int i = -12; i <= 12; ++i) {
      const float x = static_cast<float>(i) / 10;
      wave.push_back({x, 0.45f + 0.8f * static_cast<float>(k) + 0.18f * sinf(2.6f * x + static_cast<float>(k))});
    }
    Append(parts, Stroke(wave, 0.22f, Tone::Base, 0, false));
    Append(parts, Stroke(wave, 0.06f, Tone::Light, 1, false));
  }
  return parts;
}

std::vector<Part> InfinityParts() {  // Continuum.
  std::vector<Part> parts;
  for (float x : {-0.58f, 0.58f}) {
    const auto loop = Ellipse({x, 1.1f}, 0.58f, 0.46f, 28);
    Append(parts, Stroke(loop, 0.24f, Tone::Base, 0, true));
    Append(parts, Stroke(loop, 0.07f, Tone::Light, 1, true));
  }
  return parts;
}

std::vector<Part> EclipseParts() {  // Eclipse: a dark disc inside a glowing ring.
  const Point center = {0, 1.25f};
  std::vector<Part> parts;
  parts.push_back({Ellipse(center, 1.25f, 1.25f, 40), center, Tone::Eye});
  parts.push_back(Raised(Ellipse(center, 1.02f, 1.02f, 40), center, Tone::Base, 1));
  Append(parts, Stroke(Ellipse(center, 1.12f, 1.12f, 40), 0.05f, Tone::Light, 2, true));
  return parts;
}

std::vector<Part> NovaParts() {  // Novae.
  const Point center = {0, 1.3f};
  return {{Star(center, 1.3f, 0.5f, 8), center, Tone::Base},
          Raised(Ellipse(center, 0.38f, 0.38f, 20), center, Tone::Light, 1)};
}

std::vector<Part> MirrorParts() {  // Mass Group Ego: a hand mirror.
  std::vector<Part> parts;
  parts.push_back(Flat(Rect(-0.12f, 0, 0.12f, 1.05f), Tone::Base, 0));
  parts.push_back({Ellipse({0, 0.05f}, 0.2f, 0.12f, 12), {0, 0.05f}, Tone::Base});
  parts.push_back({Ellipse({0, 1.85f}, 0.76f, 0.92f, 32), {0, 1.85f}, Tone::Base});
  parts.push_back(Raised(Ellipse({0, 1.85f}, 0.6f, 0.76f, 32), {0, 1.85f}, Tone::Dark, 1));  // The glass.
  Append(parts, Stroke({{-0.35f, 1.9f}, {-0.05f, 2.3f}}, 0.09f, Tone::Light, 2, false));     // Glints.
  Append(parts, Stroke({{-0.3f, 1.6f}, {0.12f, 2.15f}}, 0.05f, Tone::Light, 2, false));
  return parts;
}

std::vector<Part> EggParts() {  // Breakfast Club.
  std::vector<Point> white;
  for (int i = 0; i < 40; ++i) {
    const float t = 2 * kPi * static_cast<float>(i) / 40;
    white.push_back({(1.25f + 0.12f * sinf(5 * t)) * cosf(t), 1.25f + (1.1f + 0.1f * sinf(4 * t + 1)) * sinf(t)});
  }
  std::vector<Part> parts;
  parts.push_back({white, {0, 1.25f}, Tone::Base});
  parts.push_back(Raised(Ellipse({0.12f, 1.35f}, 0.48f, 0.45f, 28), {0.12f, 1.35f}, Tone::Eye, 1));  // Yolk.
  parts.push_back(Raised(Ellipse({0, 1.5f}, 0.14f, 0.1f, 12), {0, 1.5f}, Tone::Light, 2));
  return parts;
}

std::vector<Part> SerpentParts() {  // Here There Be Monsters: a sea serpent over the waterline.
  std::vector<Point> water;
  for (int i = -14; i <= 14; ++i)
    water.push_back({static_cast<float>(i) / 10, 0.45f + 0.06f * sinf(static_cast<float>(i))});
  std::vector<Part> parts = Stroke(water, 0.08f, Tone::Light, 0, false);
  for (float x : {-0.95f, -0.2f}) Append(parts, Stroke(Arc({x, 0.5f}, 0.32f, 0, kPi, 10), 0.26f, Tone::Base, 1, false));
  Append(parts, Stroke({{0.4f, 0.5f}, {0.5f, 1.1f}, {0.7f, 1.55f}}, 0.26f, Tone::Base, 1, false));  // Neck.
  parts.push_back(Raised(Ellipse({0.9f, 1.62f}, 0.32f, 0.18f, 16), {0.9f, 1.62f}, Tone::Base, 1));
  parts.push_back(Raised(Ellipse({0.88f, 1.7f}, 0.05f, 0.05f, 8), {0.88f, 1.7f}, Tone::Eye, 2));
  return parts;
}

std::vector<Part> TowerParts() {  // Sentinels: a watchtower.
  std::vector<Part> parts;
  parts.push_back(Flat({{-0.6f, 0}, {0.6f, 0}, {0.5f, 1.9f}, {-0.5f, 1.9f}}, Tone::Base, 0));
  parts.push_back(Flat(Rect(-0.72f, 1.85f, 0.72f, 2.2f), Tone::Base, 0));
  for (float x : {-0.72f, -0.15f, 0.42f}) parts.push_back(Flat(Rect(x, 2.15f, x + 0.3f, 2.5f), Tone::Base, 0));
  parts.push_back(Flat(Rect(-0.2f, 0, 0.2f, 0.5f), Tone::Dark, 1));  // Door.
  parts.push_back(Raised(Ellipse({0, 0.5f}, 0.2f, 0.2f, 12), {0, 0.5f}, Tone::Dark, 1));
  parts.push_back(Flat(Rect(-0.06f, 1.1f, 0.06f, 1.5f), Tone::Eye, 1));  // Lit arrow slit.
  return parts;
}

std::vector<Part> ChainParts() {  // Alianza: two linked chain links.
  std::vector<Part> parts = Stroke(Stadium({-0.55f, 1.25f}, 0.4f, 0.4f), 0.2f, Tone::Base, 0, true);
  Append(parts, Stroke(Stadium({0.55f, 1.25f}, 0.4f, 0.4f), 0.2f, Tone::Shade, 1, true));
  // The first link passes back over the second here, so they read as linked.
  Append(parts, Stroke(Arc({-0.15f, 1.25f}, 0.4f, 45 * kDeg, 88 * kDeg, 5), 0.2f, Tone::Base, 2, false));
  RotateParts(parts, 30 * kDeg, {0, 1.25f});
  return parts;
}

std::vector<Part> TentParts() {  // Camped.
  std::vector<Part> parts;
  parts.push_back(Flat({{-1.3f, 0}, {1.3f, 0}, {0, 2.3f}}, Tone::Base, 0));
  parts.push_back(Flat({{-0.42f, 0}, {0.42f, 0}, {0, 1.1f}}, Tone::Dark, 1));
  parts.push_back(Flat(Rect(-0.05f, 2.2f, 0.05f, 2.7f), Tone::Dark, 0));
  parts.push_back(Flat({{0.05f, 2.7f}, {0.6f, 2.55f}, {0.05f, 2.4f}}, Tone::Shade, 0));  // Pennant.
  return parts;
}

std::vector<Part> BallAndChainParts() {  // Convicts.
  std::vector<Part> parts;
  parts.push_back({Ellipse({0.45f, 0.75f}, 0.75f, 0.75f, 28), {0.45f, 0.75f}, Tone::Base});
  parts.push_back(Raised(Ellipse({0.2f, 1.0f}, 0.16f, 0.16f, 10), {0.2f, 1.0f}, Tone::Light, 1));
  const Point links[] = {{-0.12f, 1.55f}, {-0.42f, 1.85f}, {-0.72f, 2.15f}};
  for (int i = 0; i < 3; ++i) {  // Alternate links face on and edge on.
    const Point c = links[i];
    if (i % 2 == 0)
      Append(parts, Stroke(Ellipse(c, 0.24f, 0.16f, 14), 0.1f, Tone::Shade, 1, true));
    else
      parts.push_back(
          Flat(Rotate(Rect(c.x - 0.26f, c.z - 0.06f, c.x + 0.26f, c.z + 0.06f), -45 * kDeg, c), Tone::Shade, 2));
  }
  Append(parts,
         Stroke(Arc({-0.98f, 2.5f}, 0.36f, -60 * kDeg, 240 * kDeg, 14), 0.14f, Tone::Shade, 1, false));  // Shackle.
  return parts;
}

std::vector<Part> GuildIconParts(int index) {
  struct Icon {
    const char *code;
    std::vector<Part> (*build)();
  };

  static constexpr Icon kIcons[] = {
      {"MAY", LightningParts}, {"TRQ", LotusParts},    {"SOW", AcornParts},       {"INT", AnkhParts},
      {"ECG", AnchorParts},    {"SAV", ClawParts},     {"BRN", CigaretteParts},   {"FG", CrownParts},
      {"AX", DeltaParts},      {"HVN", HouseParts},    {"FRE", BirdParts},        {"SOS", EyeParts},
      {"HC", TankardParts},    {"NOC", CrescentParts}, {"DND", D20Parts},         {"ZEK", AxeParts},
      {"DRF", WaveParts},      {"CON", InfinityParts}, {"ECL", EclipseParts},     {"NOV", NovaParts},
      {"MGE", MirrorParts},    {"BC", EggParts},       {"HBM", SerpentParts},     {"SEN", TowerParts},
      {"ALZ", ChainParts},     {"CMP", TentParts},     {"CVT", BallAndChainParts}};
  for (const auto &icon : kIcons)
    if (std::string(icon.code) == kGuilds[index].code) {
      auto parts = icon.build();
      Settle(parts);
      return parts;
    }
  return {};  // The guild uses an existing shape (icon_key).
}

}  // namespace

// Wolf Pack first, then the other guilds in the order they were listed. The banner colors step round the hue
// wheel by the golden ratio, alternating bright and deep, so neighbors never look alike.
const Guild kGuilds[kGuildCount] = {
    {"WP", "Wolf Pack", 0xd29922, 0, "WP"},  // Its icon is the wolf.
    {"MAY", "Mayhem", 0x5279d8, 0xf4d030, nullptr},
    {"EUR", "Europa", 0x709e2f, 0, "E"},  // The euro sign.
    {"TRQ", "Tranquility", 0xd852c8, 0xf09ac8, nullptr},
    {"SOW", "Squirrels of War", 0x2f9e8b, 0xb07840, nullptr},
    {"INT", "Intervention", 0xd89b52, 0xe8c040, nullptr},
    {"ECG", "Erud's Crossing Guard", 0x4b2f9e, 0x4a86d8, nullptr},
    {"SAV", "Savage", 0x58d852, 0xd82a2a, nullptr},
    {"BRN", "Burnouts", 0x9e2f54, 0xd8b078, nullptr},
    {"FG", "Former Glory", 0x52a6d8, 0xe8b830, nullptr},
    {"AX", "Axiom", 0x959e2f, 0x40d0b0, nullptr},
    {"HVN", "Haven", 0xbc52d8, 0xc88a5a, nullptr},
    {"FRE", "Freedom", 0x2f9e66, 0x8cc8f0, nullptr},
    {"SOS", "Seekers of Souls", 0xd86e52, 0x9ab4d8, nullptr},
    {"HC", "Hardened Casuals", 0x2f389e, 0xc88630, nullptr},
    {"NOC", "Nocturnal", 0x85d852, 0xd8dcf0, nullptr},
    {"DND", "Dungeons and Dragons", 0x9e2f79, 0xc03030, nullptr},
    {"ZEK", "Zek", 0x52d3d8, 0xb8bec8, nullptr},
    {"DRF", "The Drift", 0x9e822f, 0x30b8c8, nullptr},
    {"CON", "Continuum", 0x8f52d8, 0x40c0e0, nullptr},
    {"ECL", "Eclipse", 0x2f9e41, 0x1c212a, nullptr},
    {"LSF", "Loot & Some Fun", 0xd85263, 0, "$"},  // The dollar sign.
    {"NOV", "Novae", 0x2f5d9e, 0xa070f0, nullptr},
    {"MGE", "Mass Group Ego", 0xb1d852, 0xe0b040, nullptr},
    {"BC", "Breakfast Club", 0x9d2f9e, 0xf6f4ee, nullptr},
    {"HBM", "Here There Be Monsters", 0x52d8b1, 0x48b060, nullptr},
    {"SEN", "Sentinels", 0x9e5d2f, 0x9ca4b0, nullptr},
    {"ALZ", "Alianza", 0x6252d8, 0xe0b848, nullptr},
    {"CMP", "Camped", 0x429e2f, 0x6e8b3d, nullptr},
    {"CVT", "Convicts", 0xd85290, 0x8a929e, nullptr},
};

int GuildIndex(const std::string &code) {
  for (int i = 0; i < kGuildCount; ++i) {
    const std::string guild = kGuilds[i].code;
    bool same = guild.size() == code.size();
    for (size_t k = 0; same && k < code.size(); ++k)
      same = std::toupper(static_cast<unsigned char>(code[k])) == guild[k];
    if (same) return i;
  }
  return -1;
}

static std::string AlnumLower(const std::string &text) {
  std::string out;
  for (unsigned char c : text)
    if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
  return out;
}

int GuildIndexByName(const std::string &name) {
  const std::string wanted = AlnumLower(name);
  if (wanted.empty()) return -1;
  for (int i = 0; i < kGuildCount; ++i)
    if (AlnumLower(kGuilds[i].name) == wanted) return i;
  return -1;
}

float RelativeLuminance(uint32_t rgb) {
  const auto linear = [](uint32_t channel) {
    const float c = static_cast<float>(channel) / 255;
    return (c <= 0.03928f) ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
  };
  return 0.2126f * linear((rgb >> 16) & 0xFF) + 0.7152f * linear((rgb >> 8) & 0xFF) + 0.0722f * linear(rgb & 0xFF);
}

float ContrastRatio(uint32_t a, uint32_t b) {
  const float la = RelativeLuminance(a), lb = RelativeLuminance(b);
  return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

uint32_t ContrastingLogoRgb(uint32_t banner_rgb, uint32_t logo_rgb) {
  constexpr float kMinContrast = 3.0f;
  if (ContrastRatio(banner_rgb, logo_rgb) >= kMinContrast) return logo_rgb;
  const auto mix = [&](int target) {  // 85% of the way from the logo's color to white (255) or black (0).
    uint32_t out = 0;
    for (int shift : {16, 8, 0}) {
      const int channel = static_cast<int>((logo_rgb >> shift) & 0xFF);
      out |= static_cast<uint32_t>(channel + (target - channel) * 85 / 100) << shift;
    }
    return out;
  };
  const uint32_t light = mix(255), dark = mix(0);
  return (ContrastRatio(banner_rgb, light) >= ContrastRatio(banner_rgb, dark)) ? light : dark;
}

namespace {

// A guild's logo color: its icon color, or for an icon that is an existing shape that shape's tag color
// (TagArrowColor in nameplate.cpp).
uint32_t LogoBaseRgb(const Guild &g) {
  if (!g.icon_key) return g.icon_rgb;
  const std::string key = g.icon_key;
  return (key == "WP") ? 0xe8e2d4 : (key == "E") ? 0xe8a020 : (key == "$") ? 0x3ab05a : 0xffffff;
}

}  // namespace

uint32_t BannerLogoRgb(int guild) { return LogoBaseRgb(kGuilds[guild]); }

uint32_t BannerTextRgb(int guild) {
  const Guild &g = kGuilds[guild];
  return ContrastingLogoRgb(g.banner_rgb, LogoBaseRgb(g));
}

bool BannerLogoIsOutlined(int guild) {
  const Guild &g = kGuilds[guild];
  return ContrastRatio(g.banner_rgb, LogoBaseRgb(g)) < 3.0f;
}

int GlyphIndex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
  if (c >= 'a' && c <= 'z') return 10 + (c - 'a');
  return -1;
}

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
    case Kind::Wolf:
      parts = WolfParts();
      break;
    case Kind::Moon:
      parts = MoonParts();
      break;
    case Kind::Lasso:
      parts = LassoParts();
      break;
    case Kind::Lute:
      parts = LuteParts();
      break;
    case Kind::Shield:
      parts = ShieldParts();
      break;
    case Kind::Dollar:
      parts = DollarParts();
      break;
    case Kind::Euro:
      parts = EuroParts();
      break;
    case Kind::CrossedSwordsTarget:
      parts = CrossedSwordsTargetParts();
      break;
    case Kind::Hourglass:
      parts = HourglassParts();
      break;
    default:
      if (kind >= Kind::Number1 && kind <= Kind::Number12)
        parts = NumberParts(static_cast<int>(kind) - static_cast<int>(Kind::Number1) + 1);
      else if (kind >= Kind::Glyph0 && kind <= Kind::GlyphZ)
        parts = GlyphParts(static_cast<int>(kind) - static_cast<int>(Kind::Glyph0));
      else if (kind >= Kind::Banner0 && kind <= Kind::BannerLast)
        parts = BannerParts(static_cast<int>(kind) - static_cast<int>(Kind::Banner0));
      else if (kind >= Kind::GuildIcon0 && kind <= Kind::GuildIconLast)
        parts = GuildIconParts(static_cast<int>(kind) - static_cast<int>(Kind::GuildIcon0));
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
