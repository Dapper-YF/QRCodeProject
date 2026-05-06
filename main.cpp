#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "QRProcessor.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // 将 QRProcessor 注册到 QML，使其可以在 QML 中使用标签
    qmlRegisterType<QRProcessor>("com.qrcode.app", 1, 0, "QRProcessor");

    QQmlApplicationEngine engine;

    // 加载 main.qml
    const QUrl url(u"qrc:/qt/qml/com/qrcode/app/main.qml"_qs);
    
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );

    engine.load(url);

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
