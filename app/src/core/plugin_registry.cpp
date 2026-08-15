#include "core/plugin_registry.h"

#include <iostream>

namespace gomoku::app {

namespace {

bool IsEnginePluginFile(const std::filesystem::path& path) {
  if (!std::filesystem::is_regular_file(path)) return false;

  const std::string filename = path.filename().string();
  const std::string ext = path.extension().string();

  if (ext != ".so" && ext != ".dll" && ext != ".dylib") {
    return false;
  }

  // Matches "engine_plugin_*" or "libengine_plugin_*"
  if (filename.rfind("engine_plugin_", 0) == 0 ||
      filename.rfind("libengine_plugin_", 0) == 0) {
    return true;
  }

  return false;
}

}  // namespace

PluginRegistry& PluginRegistry::Instance() {
  static PluginRegistry instance;
  return instance;
}

PluginRegistry::PluginRegistry(QObject* parent) : QObject(parent) {
  rescanPlugins();
}

QStringList PluginRegistry::availableEngines() const {
  QStringList list;
  list.append("Human");

  for (const auto& [name, _] : loaded_plugins_) {
    list.append(QString::fromStdString(name));
  }
  return list;
}

void PluginRegistry::rescanPlugins() {
  loaded_plugins_.clear();
  plugin_file_paths_.clear();

  // 1. Scan current working directory
  ScanDirectoryInternal(std::filesystem::current_path());

  // 2. Scan optional ./plugins/ directory
  std::filesystem::path plugins_dir = std::filesystem::current_path() / "plugins";
  if (std::filesystem::exists(plugins_dir)) {
    ScanDirectoryInternal(plugins_dir);
  }

  emit enginesChanged();
}

void PluginRegistry::scanDirectory(const QString& directoryPath) {
  std::filesystem::path dir(directoryPath.toStdString());
  ScanDirectoryInternal(dir);
  emit enginesChanged();
}

void PluginRegistry::ScanDirectoryInternal(const std::filesystem::path& dir) {
  if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (IsEnginePluginFile(entry.path())) {
      try {
        auto loaded = gomoku::plugin::EnginePluginLoader::LoadPlugin(entry.path());
        if (loaded) {
          std::string name = loaded->GetName();
          loaded_plugins_[name] = loaded;
          plugin_file_paths_[name] = entry.path().string();
          emit pluginLoaded(QString::fromStdString(name),
                            QString::fromStdString(entry.path().string()));
        }
      } catch (const std::exception& e) {
        emit pluginError(QString::fromStdString(entry.path().string()),
                         QString::fromUtf8(e.what()));
      }
    }
  }
}

QString PluginRegistry::getPluginVersion(const QString& engineName) const {
  std::string name = engineName.toStdString();
  auto it = loaded_plugins_.find(name);
  if (it != loaded_plugins_.end() && it->second) {
    return QString::fromStdString(it->second->GetVersion());
  }
  return "";
}

QString PluginRegistry::getPluginAuthor(const QString& engineName) const {
  std::string name = engineName.toStdString();
  auto it = loaded_plugins_.find(name);
  if (it != loaded_plugins_.end() && it->second) {
    const auto& info = it->second->GetInfo();
    return info.author ? QString::fromUtf8(info.author) : "";
  }
  return "";
}

QString PluginRegistry::getPluginFilePath(const QString& engineName) const {
  std::string name = engineName.toStdString();
  auto it = plugin_file_paths_.find(name);
  if (it != plugin_file_paths_.end()) {
    return QString::fromStdString(it->second);
  }
  return "";
}

bool PluginRegistry::isEnginePlugin(const QString& engineName) const {
  if (engineName.toLower() == "human") return false;
  return loaded_plugins_.find(engineName.toStdString()) != loaded_plugins_.end();
}

std::unique_ptr<gomoku::plugin::IPlayer> PluginRegistry::CreatePlayer(
    const std::string& engine_name, const std::string& config_json) const {
  if (engine_name == "Human" || engine_name.empty()) {
    return nullptr;
  }

  auto it = loaded_plugins_.find(engine_name);
  if (it != loaded_plugins_.end() && it->second) {
    try {
      return it->second->CreatePlayer(config_json);
    } catch (const std::exception& e) {
      std::cerr << "Failed to instantiate player from plugin '" << engine_name
                << "': " << e.what() << std::endl;
      return nullptr;
    }
  }
  return nullptr;
}

}  // namespace gomoku::app
