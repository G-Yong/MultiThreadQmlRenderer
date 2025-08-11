#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QApplication>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QCalendarWidget>
#include <QTimer>

#include <QQuickWidget>

#include "../zquickwidget.h"

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QTGLESSTREAM_DISPLAY")) {
        qputenv("QT_QPA_EGLFS_PHYSICAL_WIDTH", QByteArray("213"));
        qputenv("QT_QPA_EGLFS_PHYSICAL_HEIGHT", QByteArray("120"));

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    }
    QApplication app(argc, argv);


#define USE_CUSTOM
#ifdef USE_CUSTOM
    // 自行实现的qml渲染器
    ZQuickWidget *qWidget = new ZQuickWidget();
#else
    // Qt自带的渲染引擎
    QQuickWidget *qWidget = new QQuickWidget();
#endif
    qWidget->setSource(QUrl("qrc:/main.qml"));
    qWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);


    // 主界面元素，可以查看UI线程是否保持正常的实时性
    QProgressBar *bar = new QProgressBar(qWidget);
    bar->resize(500, 50);
    bar->setValue(50);
    bar->show();
    QTimer pTimer;
    QObject::connect(&pTimer, &QTimer::timeout, &app, [&](){
        int val = bar->value();
        val++;
        if(val > 100)
        {
            val = 0;
        }
        bar->setValue(val);
    });
    pTimer.start(10);

    QWidget widget;
    QHBoxLayout *layout = new QHBoxLayout();
    layout->addWidget(new QCalendarWidget(), 1);
    layout->addWidget(qWidget, 2);
    widget.setLayout(layout);
    widget.resize(800, 600);
    widget.show();


    return app.exec();
}
