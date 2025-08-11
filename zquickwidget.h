#ifndef ZQUICKWIDGET_H
#define ZQUICKWIDGET_H

#include <QWidget>
#include <QWindow>
#include <QMatrix4x4>
#include <QThread>
#include <QWaitCondition>
#include <QMutex>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QOpenGLWidget>

QT_FORWARD_DECLARE_CLASS(QOpenGLContext)
QT_FORWARD_DECLARE_CLASS(QOpenGLFramebufferObject)
QT_FORWARD_DECLARE_CLASS(QOffscreenSurface)
QT_FORWARD_DECLARE_CLASS(QQuickRenderControl)
QT_FORWARD_DECLARE_CLASS(QQuickWindow)
QT_FORWARD_DECLARE_CLASS(QQmlEngine)
QT_FORWARD_DECLARE_CLASS(QQmlComponent)
QT_FORWARD_DECLARE_CLASS(QQuickItem)

#ifdef Q_OS_WIN
#pragma execution_character_set("utf-8")
#endif

class QuickRenderer : public QObject
{
    Q_OBJECT

public:
    QuickRenderer();

    void requestInit();
    void requestRender();
    void requestResize();
    void requestStop();

    QWaitCondition *cond() { return &m_cond; }
    QMutex *mutex() { return &m_mutex; }

    void setContext(QOpenGLContext *ctx) { m_context = ctx; }
    void setSurface(QOffscreenSurface *s) { m_surface = s; }
    void setQuickWindow(QQuickWindow *w) { m_quickWindow = w; }
    void setRenderControl(QQuickRenderControl *r) { m_renderControl = r; }

    void setWidget(QWidget *w) {m_widget = w;}


    void aboutToQuit();

    volatile int mProcessState = 0;
    volatile bool mFinished = false;
    volatile bool mHasPostRender = false;

signals:
    void rendered(QImage img);

private:
    bool event(QEvent *e) override;
    void init();
    void cleanup();
    void ensureFbo();
    void render(QMutexLocker *lock);

    QWaitCondition m_cond;
    QMutex m_mutex;
    QOpenGLContext *m_context;
    QOffscreenSurface *m_surface;
    QOpenGLFramebufferObject *m_fbo;
    QQuickWindow *m_quickWindow;
    QQuickRenderControl *m_renderControl;
    QMutex m_quitMutex;
    bool m_quit;

    QWidget *m_widget;
};

class ZQuickWidget : public QWidget
{
    Q_OBJECT

public:
    ZQuickWidget(QWidget *parent = nullptr);
    ~ZQuickWidget();

    QQmlEngine *engine() const{return m_qmlEngine;}
    QQuickWindow *quickWindow() const{return m_quickWindow;}
    QQmlContext *rootContext() const{return m_qmlEngine->rootContext();}
    QQuickItem *rootObject() const{return m_rootItem;}
    void setResizeMode(QQuickWidget::ResizeMode mode){}

    int setSource(QUrl url);

protected:
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    bool event(QEvent *e) override;

    void paintEvent(QPaintEvent *event) override;

private slots:
    void run();
    void requestUpdate();
    void polishSyncAndRender();


private:
    void startQuick(const QString &filename);
    void updateSizes();

    QuickRenderer *m_quickRenderer;
    QThread *m_quickRendererThread;

    QOpenGLContext *m_context;
    QOffscreenSurface *m_offscreenSurface;
    QQuickRenderControl *m_renderControl;
    QQuickWindow *m_quickWindow;
    QQmlEngine *m_qmlEngine;
    QQmlComponent *m_qmlComponent;
    QQuickItem *m_rootItem;
    bool m_quickInitialized;
    bool m_psrRequested;

    QString mQmlFile;
    QImage mImg;
};

#endif // ZQUICKWIDGET_H
