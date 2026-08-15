#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "plugin/player_interface.h"
#include "plugin/plugin_api.h"

namespace gomoku::plugin {

class LoadedPlugin;

class EnginePluginLoader {
 public:
  // Loads a dynamic engine plugin from a .so / .dll / .dylib file.
  // Throws std::runtime_error if loading or symbol resolution fails.
  static std::shared_ptr<LoadedPlugin> LoadPlugin(
      const std::filesystem::path& plugin_path);

  // Scans a directory for compatible engine plugins and loads all valid ones.
  static std::vector<std::shared_ptr<LoadedPlugin>> DiscoverPlugins(
      const std::filesystem::path& directory);
};

class LoadedPlugin : public std::enable_shared_from_this<LoadedPlugin> {
 public:
  ~LoadedPlugin();

  // Instantiates a new IPlayer engine instance from this plugin.
  std::unique_ptr<IPlayer> CreatePlayer(
      const std::string& config_json = "{}");

  const GomokuPluginInfo& GetInfo() const { return info_; }
  const std::filesystem::path& GetPath() const { return path_; }
  std::string GetName() const { return info_.plugin_name ? info_.plugin_name : "UnnamedPlugin"; }
  std::string GetVersion() const { return info_.plugin_version ? info_.plugin_version : "1.0"; }

 private:
  friend class EnginePluginLoader;
  friend class PluginPlayerAdapter;

  using GetInfoFn = GomokuPluginInfo (*)();
  using CreateFn = GomokuPlayerHandle (*)(const char*);
  using DestroyFn = void (*)(GomokuPlayerHandle);
  using OnMatchStartFn = void (*)(GomokuPlayerHandle, const GomokuMatchSettings*);
  using InquireActionFn = int (*)(GomokuPlayerHandle, const uint8_t*, int, int, int);
  using ApplyActionFn = void (*)(GomokuPlayerHandle, int);
  using OnMatchEndFn = void (*)(GomokuPlayerHandle, const GomokuMatchResult*);
  using GetWinRateFn = int (*)(GomokuPlayerHandle, float*);
  using GetPolicyFn = int (*)(GomokuPlayerHandle, float*, int);
  using CancelInquiryFn = void (*)(GomokuPlayerHandle);

  LoadedPlugin(void* lib_handle, std::filesystem::path path);

  void* lib_handle_{nullptr};
  std::filesystem::path path_;
  GomokuPluginInfo info_{};

  GetInfoFn get_info_fn_{nullptr};
  CreateFn create_fn_{nullptr};
  DestroyFn destroy_fn_{nullptr};
  OnMatchStartFn on_match_start_fn_{nullptr};
  InquireActionFn inquire_action_fn_{nullptr};
  ApplyActionFn apply_action_fn_{nullptr};
  OnMatchEndFn on_match_end_fn_{nullptr};
  GetWinRateFn get_win_rate_fn_{nullptr};
  GetPolicyFn get_policy_fn_{nullptr};
  CancelInquiryFn cancel_inquiry_fn_{nullptr};
};

}  // namespace gomoku::plugin
