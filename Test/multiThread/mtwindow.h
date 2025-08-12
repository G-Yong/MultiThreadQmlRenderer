#ifndef MTWINDOW_H
#define MTWINDOW_H

#include <QWindow>
#include <QMatrix4x4>
#include <QThread>
#include <QWaitCondition>
#include <QMutex>

QT_FORWARD_DECLARE_CLASS(QOpenGLContext)
QT_FORWARD_DECLARE_CLASS(QOpenGLFramebufferObject)
QT_FORWARD_DECLARE_CLASS(QOffscreenSurface)
QT_FORWARD_DECLARE_CLASS(QQuickRenderControl)
QT_FORWARD_DECLARE_CLASS(QQuickWindow)
QT_FORWARD_DECLARE_CLASS(QQmlEngine)
QT_FORWARD_DECLARE_CLASS(QQmlComponent)
QT_FORWARD_DECLARE_CLASS(QQuickItem)

#pragma execution_character_set("utf-8")

class PlaneRenderer;

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
    void setWindow(QWindow *w) { m_window = w; }
    void setQuickWindow(QQuickWindow *w) { m_quickWindow = w; }
    void setRenderControl(QQuickRenderControl *r) { m_renderControl = r; }

    void aboutToQuit();

    volatile bool mProcessing = false;

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
    QWindow *m_window;
    QQuickWindow *m_quickWindow;
    QQuickRenderControl *m_renderControl;
    PlaneRenderer *m_planeRenderer;
    QMutex m_quitMutex;
    bool m_quit;
};

class MTWindow : public QWindow
{
    Q_OBJECT

public:
    MTWindow(QString qmlFile);
    ~MTWindow();

protected:
    void exposeEvent(QExposeEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    bool event(QEvent *e) override;

private slots:
    void run();
    void requestUpdate();
    void polishSyncAndRender();

public:
    QQmlEngine *m_qmlEngine;

private:
    void startQuick(const QString &filename);
    void updateSizes();

    QuickRenderer *m_quickRenderer;
    QThread *m_quickRendererThread;

    QOpenGLContext *m_context;
    QOffscreenSurface *m_offscreenSurface;
    QQuickRenderControl *m_renderControl;
    QQuickWindow *m_quickWindow;
    // QQmlEngine *m_qmlEngine;
    QQmlComponent *m_qmlComponent;
    QQuickItem *m_rootItem;
    bool m_quickInitialized;
    bool m_psrRequested;

    QString mQmlFile;
};
#endif // MTWINDOW_H
