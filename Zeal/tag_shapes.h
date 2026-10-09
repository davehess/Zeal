#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Geometry for the icon-style tag shapes (skull, cross, sword, diamond, flame, star, wolf, moon, lasso,
// lute, shield, dollar, euro), the numbered badges 1 to 12, the letters and digits drawn on the pet paw,
// and a banner and an icon for each guild in kGuilds.
//
// Each shape is a set of flat parts extruded to a thickness. Most parts are convex (or at least
// star-shaped around their center) so they triangulate as a simple fan; the wolf's traced parts carry
// their own triangles. Concave outlines are built by overlapping parts, and detail such as eye sockets
// or a blade's fuller is a part that stands slightly proud of the face in an accent tone, so no
// texture is needed.
//
// This is plain math with no DirectX dependency so the meshes can be checked and previewed off-client.
namespace TagShapes {

// Guilds with a banner (^B<code>^: a swallowtail flag in the guild's color with its code on it) and an
// icon (^I<code>^: the guild's own symbol).
struct Guild {
  const char *code;      // Two or three uppercase letters.
  const char *name;      // For the /tag guilds listing.
  uint32_t banner_rgb;   // The banner's color, 0xRRGGBB.
  uint32_t icon_rgb;     // The icon's color, 0xRRGGBB (unused when icon_key is set).
  const char *icon_key;  // An existing shape's key used as the icon instead of one of its own, else nullptr.
};

constexpr int kGuildCount = 30;
extern const Guild kGuilds[kGuildCount];

// Returns the index in kGuilds of a guild code (either case), else -1.
int GuildIndex(const std::string &code);

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
  Lute,
  Shield,
  Dollar,
  Euro,
  Number1,  // Number1 to Number12 are consecutive: Number1 + (n - 1).
  Number12 = Number1 + 11,
  Glyph0,  // Glyphs drawn over the paw's main pad: '0' to '9' then 'A' to 'Z', consecutive.
  GlyphZ = Glyph0 + 35,
  Banner0,  // Guild banners and then guild icons, each in kGuilds order.
  BannerLast = Banner0 + kGuildCount - 1,
  GuildIcon0,  // Empty for a guild whose icon is an existing shape (icon_key).
  GuildIconLast = GuildIcon0 + kGuildCount - 1,
  Count,  // Number of shapes (not a shape).
};

// Which color a vertex takes: the tag color, a dark accent, a light accent, whichever of the two
// accents contrasts with the tag color (the digits on a numbered badge), a fixed eye yellow, or a
// shaded version of the tag color (the moon's craters).
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

// Returns the paw glyph index (0 to 35) of '0'-'9' or 'A'-'Z' (either case), else -1.
int GlyphIndex(char c);

}  // namespace TagShapes
