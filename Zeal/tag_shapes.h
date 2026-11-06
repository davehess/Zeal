#pragma once

#include <cstdint>
#include <vector>

// Geometry for the icon-style tag shapes (skull, cross, sword, diamond, flame, star, wolf, moon, lasso, shield) and the
// numbered badges 1 to 12.
//
// Each shape is a set of flat parts extruded to a thickness. Every part is convex (or at least star-shaped
// around its center) so it triangulates as a simple fan. Concave outlines are built by overlapping parts, and
// detail such as eye sockets or a blade's fuller is a part that stands slightly proud of the face in an accent
// tone, so no texture is needed.
//
// This is plain math with no DirectX dependency so the meshes can be checked and previewed off-client.
namespace TagShapes {

enum class Kind {
  Skull = 0,
  Cross,
  Sword,
  Diamond,
  Flame,
  Star,
  Wolf,
  Moon,
  Lasso,
  Shield,
  Number1,  // Number1 to Number12 are consecutive: Number1 + (n - 1).
  Number12 = Number1 + 11,
  Count,  // Number of shapes (not a shape).
};

// Which color a vertex takes: the tag color, a dark accent, a light accent, whichever of the two accents
// contrasts with the tag color (the digits on a numbered badge), a fixed eye yellow, or a shaded version of the tag
// color (the moon's craters).
enum class Tone : uint8_t {
  Base = 0,
  Dark,
  Light,
  Contrast,
  Eye,
  Shade,
};

struct Vertex {
  float x;  // Model space: x runs across the face,
  float y;  // y through its thickness (negative is the front face),
  float z;  // and z up, with the bottom of the shape at 0.
  Tone tone;
};

struct Mesh {
  std::vector<Vertex> vertices;
  std::vector<int16_t> indices;  // A single triangle strip; degenerate triangles join the parts.
  float min_z = 0;
  float max_z = 0;
};

// Returns the mesh for a shape (empty for Kind::Count).
Mesh Build(Kind kind);

}  // namespace TagShapes
