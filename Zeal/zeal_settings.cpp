#include "zeal_settings.h"

#include <cstring>
#include <filesystem>
#include <vector>

#include "callbacks.h"
#include "game_addresses.h"
#include "game_functions.h"
#include "game_ui.h"
#include "io_ini.h"
#include "zeal.h"

static const char *get_character_name();

namespace {
constexpr char kCharFileSection[] = "Zeal";
constexpr char kCharFileKey[] = "CharacterSettingsFile";
constexpr char kCharFileMarkerSection[] = "CharacterSettings";  // Marks a per character file as created.
constexpr char kCharFileMarkerKey[] = "Migrated";
int char_file_enabled = -1;      // Lazy loaded from zeal.ini: -1 unknown, 0 off, 1 on.
std::string char_file_prepared;  // Name of the character whose file is known to exist (avoids repeated checks).

// Copies the character's "<Section>_<Name>" sections from zeal.ini into [<Section>] sections of the new file.
// The old copies are left in place so that turning the option off falls back cleanly.
void copy_character_sections(const std::string &name, const std::string &new_file) {
  std::vector<char> names(65536);
  DWORD names_len = GetPrivateProfileSectionNamesA(names.data(), static_cast<DWORD>(names.size()),
                                                   IO_ini::kZealIniFilename);
  const std::string suffix = "_" + name;
  std::vector<char> entries(65536);
  for (const char *p = names.data(); names_len && *p != '\0'; p += strlen(p) + 1) {
    std::string old_section = p;
    if (old_section.length() <= suffix.length()) continue;
    if (_stricmp(old_section.c_str() + old_section.length() - suffix.length(), suffix.c_str()) != 0) continue;
    std::string new_section = old_section.substr(0, old_section.length() - suffix.length());
    DWORD entries_len = GetPrivateProfileSectionA(old_section.c_str(), entries.data(),
                                                  static_cast<DWORD>(entries.size()), IO_ini::kZealIniFilename);
    for (const char *e = entries.data(); entries_len && *e != '\0'; e += strlen(e) + 1) {
      std::string entry = e;  // Formatted as key=value.
      size_t equals = entry.find('=');
      if (equals == std::string::npos || equals == 0) continue;
      WritePrivateProfileStringA(new_section.c_str(), entry.substr(0, equals).c_str(),
                                 entry.substr(equals + 1).c_str(), new_file.c_str());
    }
  }
  WritePrivateProfileStringA(kCharFileMarkerSection, kCharFileMarkerKey, "TRUE", new_file.c_str());
}

// Returns the full path of the character's settings file, creating it from the old values on first use.
// Returns an empty string if the option is off or the name is not known.
std::string get_character_file(const char *name) {
  if (!ZealCharSettingsFile::is_enabled() || !name || !name[0]) return "";
  std::string path = (Zeal::Game::get_game_path() / (std::string(name) + "_zeal.ini")).string();
  if (char_file_prepared != name) {
    IO_ini file(path);
    if (!file.exists(kCharFileMarkerSection, kCharFileMarkerKey)) copy_character_sections(name, path);
    char_file_prepared = name;
  }
  return path;
}
}  // namespace

bool ZealCharSettingsFile::is_enabled() {
  if (char_file_enabled < 0) {
    IO_ini ini(IO_ini::kZealIniFilename);
    char_file_enabled = ini.getValue<bool>(kCharFileSection, kCharFileKey) ? 1 : 0;
  }
  return char_file_enabled == 1;
}

void ZealCharSettingsFile::set_enabled(bool enable) {
  IO_ini ini(IO_ini::kZealIniFilename);
  ini.setValue<bool>(kCharFileSection, kCharFileKey, enable);
  char_file_enabled = enable ? 1 : 0;
  if (enable) get_character_file(get_character_name());
}

template <typename T>
ZealSetting<T>::ZealSetting(T default_value_in, const std::string &ini_section, const std::string &ini_key,
                            bool save_per_character, const std::function<void(const T &value)> &callback_on_set) {
  set_callback = callback_on_set;
  default_value = default_value_in;
  value = default_value;
  section = ini_section;
  key = ini_key;
  per_character = save_per_character;
  init();
  // Add a callback that executes when exiting CharacterSelect to trigger a refresh init of settings.
  ZealService::get_instance()->callbacks->AddGeneric([this]() { init(); }, callback_type::CleanCharSelectUI);
}

// The memory only setting just sets the section and key names blank to avoid use of ini io. It
// will still get reset to the default by the init callback.
template <typename T>
ZealSetting<T>::ZealSetting(T default_value_in) : ZealSetting(default_value_in, "", "", false, nullptr) {}

template <typename T>
void ZealSetting<T>::init() {
  // Resets to the default value and then tries to retrieve the persistent value. The per_character ones
  // will stay at the default initially until a character is known.
  value = default_value;
  if (section.length() && key.length()) {
    // Per character file (opt-in) is read first and falls back to the old shared zeal.ini location.
    std::string char_file = per_character ? get_character_file(get_character_name()) : "";
    IO_ini char_ini(char_file);
    if (char_file.length() && char_ini.exists(section, key)) {
      value = char_ini.getValue<T>(section, key);
    } else {
      IO_ini ini(IO_ini::kZealIniFilename);
      std::string section_name = get_section_name();
      if (section_name.length() && ini.exists(section_name, key)) value = ini.getValue<T>(section_name, key);
    }
  }
  if (set_callback) set_callback(value);
}

template <typename T>
void ZealSetting<T>::set(T val, bool store) {
  if (store && section.length() && key.length()) {
    std::string char_file = per_character ? get_character_file(get_character_name()) : "";
    std::string section_name = get_section_name();
    if (char_file.length()) {
      IO_ini char_ini(char_file);
      char_ini.setValue<T>(section, key, val);  // Only the per character file is written when enabled.
    } else if (section_name.length()) {
      IO_ini ini(IO_ini::kZealIniFilename);
      ini.setValue<T>(section_name, key, val);
    }
  }
  if (value != val) {
    value = val;
    if (set_callback) set_callback(value);
  }
}

static const char *get_character_name() {
  // In order to properly reset everything to the defaults and reload the per character settings,
  // this method may be accessed in GAMESTATE_ENTERWORLD where charinfo has not yet been updated.
  // We peek at the character select results to retrieve the name, which is similar to what
  // StartNetworkGame() does to set g_next_player at 0x00795274.
  const char *name = nullptr;
  if (Zeal::Game::get_gamestate() == GAMESTATE_INGAME) {
    Zeal::GameStructures::GAMECHARINFO *c = Zeal::Game::get_char_info();
    name = (c) ? c->Name : nullptr;
  } else if (Zeal::Game::get_gamestate() == GAMESTATE_ENTERWORLD) {
    int index = -1;
    if (Zeal::Game::is_new_ui()) {
      if (Zeal::Game::Windows && Zeal::Game::Windows->CharacterSelect) {
        index = Zeal::Game::Windows->CharacterSelect->SelectIndex;
      }
    } else {
      int old_char_select = *reinterpret_cast<int *>(0x007f959c);
      if (old_char_select) index = *reinterpret_cast<short *>(old_char_select + 0x80);
    }
    if (index >= 0 && index < 8) {
      name = index * 0x40 + 0x38e54 + reinterpret_cast<const char *>(Zeal::Game::get_game());
    }
  }
  return name;
}

template <typename T>
std::string ZealSetting<T>::get_section_name() const {
  if (!per_character) return section;

  const char *name = get_character_name();
  if (!name) return "";
  return section + "_" + std::string(name);
  return std::string(name);
}

// Perform explicit instatiation of the supported template types.
template class ZealSetting<bool>;
template class ZealSetting<int>;
template class ZealSetting<float>;
template class ZealSetting<std::string>;