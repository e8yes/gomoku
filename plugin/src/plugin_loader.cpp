#include "plugin/plugin_loader.h"

#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace gomoku::plugin {

namespace {

void* OpenSharedLibrary(const std::filesystem::path& path) {
  std::filesystem::path abs_path = std::filesystem::absolute(path);
#ifdef _WIN32
  HMODULE handle = LoadLibraryW(abs_path.wstring().c_str());
  if (!handle) {
    throw std::runtime_error("Failed to load DLL: " + abs_path.string() +
                             " (Error code: " + std::to_string(GetLastError()) +
                             ")");
  }
  return reinterpret_cast<void*>(handle);
#else
  dlerror();  // Clear any existing error
  void* handle = dlopen(abs_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    const char* err = dlerror();
    throw std::runtime_error(
        "Failed to load plugin library: " + abs_path.string() +
        " (Error: " + (err ? err : "unknown") + ")");
  }
  return handle;
#endif
}

void CloseSharedLibrary(void* handle) {
  if (!handle) return;
#ifdef _WIN32
  FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
  dlclose(handle);
#endif
}

void* GetLibrarySymbol(void* handle, const char* symbol_name) {
#ifdef _WIN32
  return reinterpret_cast<void*>(
      GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol_name));
#else
  return dlsym(handle, symbol_name);
#endif
}

}  // namespace

class PluginPlayerAdapter : public IPlayer {
 public:
  PluginPlayerAdapter(std::shared_ptr<LoadedPlugin> plugin,
                      GomokuPlayerHandle handle)
      : plugin_(std::move(plugin)), handle_(handle) {
    if (!handle_) {
      throw std::runtime_error(
          "Failed to create player instance from plugin: " +
          plugin_->GetName());
    }
  }

  ~PluginPlayerAdapter() override {
    if (handle_ && plugin_ && plugin_->destroy_fn_) {
      plugin_->destroy_fn_(handle_);
      handle_ = nullptr;
    }
  }

  void OnMatchStart(const MatchSettings& settings) override {
    if (!plugin_ || !plugin_->on_match_start_fn_ || !handle_) return;

    GomokuMatchSettings c_settings{};
    c_settings.seat = static_cast<int>(settings.seat);
    c_settings.difficulty = static_cast<GomokuDifficulty>(settings.difficulty);
    c_settings.opponent_name = settings.opponent_name.c_str();
    c_settings.custom_config_json = settings.custom_config_json.c_str();

    plugin_->on_match_start_fn_(handle_, &c_settings);
  }

  int InquireAction(const BoardState& board) override {
    if (!plugin_ || !plugin_->inquire_action_fn_ || !handle_) {
      return -1;
    }

    std::array<uint8_t, kNumBoardCells> raw_cells{};
    for (size_t i = 0; i < kNumBoardCells; ++i) {
      raw_cells[i] = static_cast<uint8_t>(board.cells[i]);
    }

    return plugin_->inquire_action_fn_(
        handle_, raw_cells.data(), static_cast<int>(board.current_seat),
        static_cast<int>(board.stone_to_place), static_cast<int>(board.phase));
  }

  void ApplyAction(int action_id) override {
    if (!plugin_ || !plugin_->apply_action_fn_ || !handle_) return;
    plugin_->apply_action_fn_(handle_, action_id);
  }

  void OnMatchEnd(const MatchResultInfo& result_info) override {
    if (!plugin_ || !plugin_->on_match_end_fn_ || !handle_) return;

    GomokuMatchResult c_result{};
    c_result.result = static_cast<int>(result_info.result);
    c_result.winner_seat = static_cast<int>(result_info.winner_seat);
    c_result.termination_reason = result_info.termination_reason.c_str();

    plugin_->on_match_end_fn_(handle_, &c_result);
  }

  std::optional<float> GetEstimatedWinRate() const override {
    if (!plugin_ || !plugin_->get_win_rate_fn_ || !handle_) {
      return std::nullopt;
    }

    float win_rate = 0.0f;
    if (plugin_->get_win_rate_fn_(handle_, &win_rate)) {
      return win_rate;
    }
    return std::nullopt;
  }

  std::optional<std::vector<float>> GetPolicy() const override {
    if (!plugin_ || !plugin_->get_policy_fn_ || !handle_) {
      return std::nullopt;
    }

    std::vector<float> policy(kNumTotalActions, 0.0f);
    int written = plugin_->get_policy_fn_(handle_, policy.data(),
                                          static_cast<int>(policy.size()));
    if (written > 0) {
      policy.resize(written);
      return policy;
    }
    return std::nullopt;
  }

  void CancelInquiry() override {
    if (plugin_ && plugin_->cancel_inquiry_fn_ && handle_) {
      plugin_->cancel_inquiry_fn_(handle_);
    }
  }

  std::string GetName() const override {
    return plugin_ ? plugin_->GetName() : "PluginPlayer";
  }

  bool IsHuman() const override { return false; }

 private:
  std::shared_ptr<LoadedPlugin> plugin_;
  GomokuPlayerHandle handle_{nullptr};
};

LoadedPlugin::LoadedPlugin(void* lib_handle, std::filesystem::path path)
    : lib_handle_(lib_handle), path_(std::move(path)) {}

LoadedPlugin::~LoadedPlugin() {
  CloseSharedLibrary(lib_handle_);
  lib_handle_ = nullptr;
}

std::unique_ptr<IPlayer> LoadedPlugin::CreatePlayer(
    const std::string& config_json) {
  if (!create_fn_) {
    throw std::runtime_error("Plugin has no create_player function: " +
                             path_.string());
  }

  GomokuPlayerHandle handle = create_fn_(config_json.c_str());
  if (!handle) {
    throw std::runtime_error("Plugin failed to instantiate player: " +
                             path_.string());
  }

  return std::make_unique<PluginPlayerAdapter>(shared_from_this(), handle);
}

std::shared_ptr<LoadedPlugin> EnginePluginLoader::LoadPlugin(
    const std::filesystem::path& plugin_path) {
  void* handle = OpenSharedLibrary(plugin_path);

  auto plugin =
      std::shared_ptr<LoadedPlugin>(new LoadedPlugin(handle, plugin_path));

  // Resolve required C ABI symbols
  plugin->get_info_fn_ = reinterpret_cast<LoadedPlugin::GetInfoFn>(
      GetLibrarySymbol(handle, "gomoku_plugin_get_info"));
  plugin->create_fn_ = reinterpret_cast<LoadedPlugin::CreateFn>(
      GetLibrarySymbol(handle, "gomoku_player_create"));
  plugin->destroy_fn_ = reinterpret_cast<LoadedPlugin::DestroyFn>(
      GetLibrarySymbol(handle, "gomoku_player_destroy"));
  plugin->on_match_start_fn_ = reinterpret_cast<LoadedPlugin::OnMatchStartFn>(
      GetLibrarySymbol(handle, "gomoku_player_on_match_start"));
  plugin->inquire_action_fn_ = reinterpret_cast<LoadedPlugin::InquireActionFn>(
      GetLibrarySymbol(handle, "gomoku_player_inquire_action"));
  plugin->apply_action_fn_ = reinterpret_cast<LoadedPlugin::ApplyActionFn>(
      GetLibrarySymbol(handle, "gomoku_player_apply_action"));
  plugin->on_match_end_fn_ = reinterpret_cast<LoadedPlugin::OnMatchEndFn>(
      GetLibrarySymbol(handle, "gomoku_player_on_match_end"));

  // Optional C ABI symbols
  plugin->get_win_rate_fn_ = reinterpret_cast<LoadedPlugin::GetWinRateFn>(
      GetLibrarySymbol(handle, "gomoku_player_get_win_rate"));
  plugin->get_policy_fn_ = reinterpret_cast<LoadedPlugin::GetPolicyFn>(
      GetLibrarySymbol(handle, "gomoku_player_get_policy"));
  plugin->cancel_inquiry_fn_ = reinterpret_cast<LoadedPlugin::CancelInquiryFn>(
      GetLibrarySymbol(handle, "gomoku_player_cancel_inquiry"));

  // Verify mandatory symbols
  if (!plugin->get_info_fn_ || !plugin->create_fn_ || !plugin->destroy_fn_ ||
      !plugin->on_match_start_fn_ || !plugin->inquire_action_fn_ ||
      !plugin->apply_action_fn_ || !plugin->on_match_end_fn_) {
    throw std::runtime_error("Plugin is missing mandatory C-ABI exports: " +
                             plugin_path.string());
  }

  plugin->info_ = plugin->get_info_fn_();

  if (plugin->info_.api_version_major != GOMOKU_PLUGIN_API_VERSION_MAJOR) {
    throw std::runtime_error(
        "Plugin API version mismatch: expected major version " +
        std::to_string(GOMOKU_PLUGIN_API_VERSION_MAJOR) + " but got " +
        std::to_string(plugin->info_.api_version_major) + " in " +
        plugin_path.string());
  }

  return plugin;
}

std::vector<std::shared_ptr<LoadedPlugin>> EnginePluginLoader::DiscoverPlugins(
    const std::filesystem::path& directory) {
  std::vector<std::shared_ptr<LoadedPlugin>> plugins;

  if (!std::filesystem::exists(directory) ||
      !std::filesystem::is_directory(directory)) {
    return plugins;
  }

  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file()) continue;

    const std::string ext = entry.path().extension().string();
    if (ext == ".so" || ext == ".dll" || ext == ".dylib") {
      try {
        auto plugin = LoadPlugin(entry.path());
        plugins.push_back(std::move(plugin));
      } catch (const std::exception& e) {
        // Skip incompatible dynamic libraries silently
      }
    }
  }

  return plugins;
}

}  // namespace gomoku::plugin
