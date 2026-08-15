#pragma once

#include <QObject>
#include <QStringList>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "plugin/player_interface.h"
#include "plugin/plugin_loader.h"

namespace gomoku::app {

class PluginRegistry : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList availableEngines READ availableEngines NOTIFY enginesChanged)

 public:
  static PluginRegistry& Instance();

  explicit PluginRegistry(QObject* parent = nullptr);
  ~PluginRegistry() override = default;

  QStringList availableEngines() const;

  Q_INVOKABLE void rescanPlugins();
  Q_INVOKABLE void scanDirectory(const QString& directoryPath);
  Q_INVOKABLE QString getPluginVersion(const QString& engineName) const;
  Q_INVOKABLE QString getPluginAuthor(const QString& engineName) const;
  Q_INVOKABLE QString getPluginFilePath(const QString& engineName) const;
  Q_INVOKABLE bool isEnginePlugin(const QString& engineName) const;

  // Instantiates a player. Returns nullptr if "Human" is specified.
  std::unique_ptr<gomoku::plugin::IPlayer> CreatePlayer(
      const std::string& engine_name,
      const std::string& config_json = "{}") const;

  // Dynamic registration helpers (useful for testing)
  void registerLoadedPlugin(const std::string& name,
                            std::shared_ptr<gomoku::plugin::LoadedPlugin> plugin,
                            const std::string& path = "");
  void unregisterPlugin(const std::string& name);

 signals:
  void enginesChanged();
  void pluginLoaded(const QString& name, const QString& path);
  void pluginError(const QString& path, const QString& errorMessage);

 private:
  void ScanDirectoryInternal(const std::filesystem::path& dir);

  mutable QStringList engine_names_;
  std::unordered_map<std::string, std::shared_ptr<gomoku::plugin::LoadedPlugin>>
      loaded_plugins_;
  std::unordered_map<std::string, std::string> plugin_file_paths_;
};

}  // namespace gomoku::app
