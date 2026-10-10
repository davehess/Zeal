#pragma once
#include <string>
#include <vector>

#include "zeal_settings.h"

class OutputFile {
 public:
  OutputFile(class ZealService *zeal);
  ~OutputFile(){};
  void export_inventory(const std::vector<std::string> &args = {});
  void export_quarmy(const std::vector<std::string> &args = {});
  void export_spellbook(const std::vector<std::string> &args = {});
  // Sends #popflags to the server and writes the reply lines to <name>-PoPFlags.txt a few seconds later.
  void export_popflags(const std::vector<std::string> &args = {}, bool verbose = false);

  ZealSetting<bool> setting_export_on_camp = {false, "Zeal", "ExportOnCamp", false};
  ZealSetting<int> setting_export_format = {0, "Zeal", "ExportFormat", false};

 private:
  void export_raidlist(std::vector<std::string> &args);
  void write_to_file(std::string data, std::string filename, std::string optional_name, bool add_host_tag = false);
  void popflags_capture(const std::string &msg, short channel);
  void popflags_finish();

  bool popflags_pending = false;  // True from sending #popflags until the capture window closes.
  bool popflags_verbose = false;  // Print the result to chat (set for the manual /outputfile popflags).
  ULONGLONG popflags_last_request_ms = 0;
  std::string popflags_character;
  std::string popflags_filename;  // Without the .txt extension.
  std::vector<std::string> popflags_lines;
};
