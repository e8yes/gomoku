#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <iostream>

#include "core/board_view_model.h"
#include "core/database_manager.h"
#include "core/game_controller.h"
#include "core/plugin_registry.h"
#include "core/replay_controller.h"
#include "models/match_history_model.h"
#include "models/move_history_model.h"

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  app.setOrganizationName("GomokuTeam");
  app.setApplicationName("GomokuArena");

  QQuickStyle::setStyle("Basic");

  // Open SQLite database
  if (!gomoku::app::DatabaseManager::Instance().Open()) {
    std::cerr << "Warning: Failed to open SQLite match database." << std::endl;
  }

  // Register C++ Types with QML
  qmlRegisterUncreatableType<gomoku::app::BoardViewModel>(
      "Gomoku.App", 1, 0, "BoardViewModel", "Provided by GameController");
  qmlRegisterUncreatableType<gomoku::app::MoveHistoryModel>(
      "Gomoku.App", 1, 0, "MoveHistoryModel", "Provided by GameController");
  qmlRegisterUncreatableType<gomoku::app::MatchHistoryModel>(
      "Gomoku.App", 1, 0, "MatchHistoryModel", "Provided by GameController");

  gomoku::app::GameController game_controller;
  gomoku::app::ReplayController replay_controller;
  auto& plugin_registry = gomoku::app::PluginRegistry::Instance();

  QQmlApplicationEngine engine;

  engine.rootContext()->setContextProperty("gameControllerInstance",
                                           &game_controller);
  engine.rootContext()->setContextProperty("replayControllerInstance",
                                           &replay_controller);
  engine.rootContext()->setContextProperty("pluginRegistryInstance",
                                           &plugin_registry);

  const QUrl url(QStringLiteral("qrc:/Main.qml"));
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreated, &app,
      [url](QObject* obj, const QUrl& objUrl) {
        if (!obj && url == objUrl) QCoreApplication::exit(-1);
      },
      Qt::QueuedConnection);

  engine.load(url);

  return app.exec();
}
