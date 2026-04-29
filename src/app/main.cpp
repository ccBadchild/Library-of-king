#include "mainwindow.h"
#include "protocol_qt.h"

#include <QApplication>
#include <QFile>
#include <QMetaType>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  qRegisterMetaType<rdqt::QualityPreset>("rdqt::QualityPreset");
  qRegisterMetaType<rdqt::VideoCodec>("rdqt::VideoCodec");
  qRegisterMetaType<rdqt::PatchBlock>("rdqt::PatchBlock");
  qRegisterMetaType<QVector<rdqt::PatchBlock>>("QVector<rdqt::PatchBlock>");
  qRegisterMetaType<rdqt::RemoteInputEvent>("rdqt::RemoteInputEvent");

  QFile qssFile(QStringLiteral(":/styles/dark.qss"));
  if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    app.setStyleSheet(QString::fromUtf8(qssFile.readAll()));
  }

  MainWindow w;
  w.show();
  return app.exec();
}
