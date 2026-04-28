#include "mainwindow.h"
#include "protocol_qt.h"

#include <QApplication>
#include <QMetaType>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  qRegisterMetaType<rdqt::QualityPreset>("rdqt::QualityPreset");
  qRegisterMetaType<rdqt::PatchBlock>("rdqt::PatchBlock");
  qRegisterMetaType<QVector<rdqt::PatchBlock>>("QVector<rdqt::PatchBlock>");
  qRegisterMetaType<rdqt::RemoteInputEvent>("rdqt::RemoteInputEvent");
  MainWindow w;
  w.show();
  return app.exec();
}
