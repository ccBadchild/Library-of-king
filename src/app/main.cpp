/**
 * @file main.cpp
 * @brief 程序入口：注册跨线程元类型、加载全局深色样式表、关闭「最后一窗即退出」以便托盘常驻。
 */

#include "mainwindow.h"
#include "protocol_qt.h"

#include <QApplication>
#include <QFile>
#include <QMetaType>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  // 以下类型会在 QObject::connect 跨线程队列参数中使用，必须先注册。
  qRegisterMetaType<rdqt::QualityPreset>("rdqt::QualityPreset");
  qRegisterMetaType<rdqt::VideoCodec>("rdqt::VideoCodec");
  qRegisterMetaType<rdqt::PatchBlock>("rdqt::PatchBlock");
  qRegisterMetaType<QVector<rdqt::PatchBlock>>("QVector<rdqt::PatchBlock>");
  qRegisterMetaType<rdqt::RemoteInputEvent>("rdqt::RemoteInputEvent");

  QFile qssFile(QStringLiteral(":/styles/dark.qss"));
  if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    app.setStyleSheet(QString::fromUtf8(qssFile.readAll()));
  }

  // 默认 true：最后一个 QWidget 关闭即退出。此处设为 false：主窗口 hide 后仍可依赖托盘图标存活。
  app.setQuitOnLastWindowClosed(false);

  MainWindow w;
  w.show();
  return app.exec();
}
