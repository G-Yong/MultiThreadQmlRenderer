#include "mtwindow.h"

#include "mtwindow.h"
#include "planerenderer.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOffscreenSurface>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickRenderControl>
#include <QCoreApplication>

#include <QDateTime>
#include <QElapsedTimer>
#include <QtConcurrentRun>

/*
  This implementation runs the Qt Quick scenegraph's sync and render phases on a
  separate, dedicated thread.  Rendering the cube using our custom OpenGL engine
  happens on that thread as well.  This is similar to the built-in threaded
  render loop, but does not support all the features. There is no support for
  getting Animators running on the render thread for example.

  We choose to use QObject's event mechanism to communicate with the QObject
  living on the render thread. An alternative would be to subclass QThread and
  reimplement run() with a custom event handling approach, like
  QSGThreadedRenderLoop does. That would potentially lead to better results but
  is also more complex.
*/

static const QEvent::Type INIT = QEvent::Type(QEvent::User + 1);
static const QEvent::Type RENDER = QEvent::Type(QEvent::User + 2);
static const QEvent::Type RESIZE = QEvent::Type(QEvent::User + 3);
static const QEvent::Type STOP = QEvent::Type(QEvent::User + 4);

static const QEvent::Type UPDATE = QEvent::Type(QEvent::User + 5);

QuickRenderer::QuickRenderer()
    : m_context(nullptr),
    m_surface(nullptr),
    m_fbo(nullptr),
    m_window(nullptr),
    m_quickWindow(nullptr),
    m_renderControl(nullptr),
    m_planeRenderer(nullptr),
    m_quit(false)
{
}

void QuickRenderer::requestInit()
{
    QCoreApplication::postEvent(this, new QEvent(INIT));
}

void QuickRenderer::requestRender()
{
    QCoreApplication::postEvent(this, new QEvent(RENDER));
}

void QuickRenderer::requestResize()
{
    QCoreApplication::postEvent(this, new QEvent(RESIZE));
}

void QuickRenderer::requestStop()
{
    QCoreApplication::postEvent(this, new QEvent(STOP));
}

bool QuickRenderer::event(QEvent *e)
{
    mProcessing = true;

    QMutexLocker lock(&m_mutex);

    switch (int(e->type())) {
    case INIT:
        init();
        // mProcessing = false;
        return true;
    case RENDER:
        // 之所以主线程需要等那么久，是因为本线程还在处理渲染的事情，
        // 无法进入事件处理，因此一直在等待
        // mProcessing = true;
        render(&lock);
        mProcessing = false;
        return true;
    case RESIZE:
        if (m_planeRenderer)
            m_planeRenderer->resize(m_window->width(), m_window->height());
        mProcessing = false;
        return true;
    case STOP:
        cleanup();
        mProcessing = false;
        return true;
    default:
        mProcessing = false;
        return QObject::event(e);
    }
}

void QuickRenderer::init()
{
    m_context->makeCurrent(m_surface);

    // Pass our offscreen surface to the cube renderer just so that it will
    // have something is can make current during cleanup. QOffscreenSurface,
    // just like QWindow, must always be created on the gui thread (as it might
    // be backed by an actual QWindow).
    m_planeRenderer = new PlaneRenderer(m_surface);
    m_planeRenderer->resize(m_window->width(), m_window->height());

    m_renderControl->initialize(m_context);
}

void QuickRenderer::cleanup()
{
    m_context->makeCurrent(m_surface);

    m_renderControl->invalidate();

    delete m_fbo;
    m_fbo = nullptr;

    delete m_planeRenderer;
    m_planeRenderer = nullptr;

    m_context->doneCurrent();
    m_context->moveToThread(QCoreApplication::instance()->thread());

    m_cond.wakeOne();
}

void QuickRenderer::ensureFbo()
{
    if (m_fbo && m_fbo->size() != m_window->size() * m_window->devicePixelRatio()) {
        delete m_fbo;
        m_fbo = nullptr;
    }

    if (!m_fbo) {
        m_fbo = new QOpenGLFramebufferObject(m_window->size() * m_window->devicePixelRatio(),
                                             QOpenGLFramebufferObject::CombinedDepthStencil);
        m_quickWindow->setRenderTarget(m_fbo);
    }
}

void QuickRenderer::render(QMutexLocker *lock)
{
    QElapsedTimer timer;
    timer.start();

    Q_ASSERT(QThread::currentThread() != m_window->thread());

    if (!m_context->makeCurrent(m_surface)) {
        qWarning("Failed to make context current on render thread");
        return;
    }

    ensureFbo();

    // Synchronization and rendering happens here on the render thread.
    m_renderControl->sync();

    // ui线程目前可以继续操作了
    // The gui thread can now continue.
    m_cond.wakeOne();
    lock->unlock();

    // qDebug() << "渲染同步到ui耗时：" << timer.elapsed();

    // Meanwhile on this thread continue with the actual rendering (into the FBO first).
    m_renderControl->render();
    m_context->functions()->glFlush();

    // The cube renderer uses its own context, no need to bother with the state here.

    // Get something onto the screen using our custom OpenGL engine.
    QMutexLocker quitLock(&m_quitMutex);
    if (!m_quit)
    {
        QElapsedTimer timer;
        timer.start();
        m_planeRenderer->render(m_window, m_context, m_fbo->texture());
        qDebug() << "绘制 耗时：" << timer.elapsed();
    }

    // qDebug() << "子线程渲染耗时：" << timer.elapsed();
}

void QuickRenderer::aboutToQuit()
{
    QMutexLocker lock(&m_quitMutex);
    m_quit = true;
}

class RenderControl : public QQuickRenderControl
{
public:
    RenderControl(QWindow *w) : m_window(w) { }
    QWindow *renderWindow(QPoint *offset) override;

private:
    QWindow *m_window;
};

QWindow *RenderControl::renderWindow(QPoint *offset)
{
    if (offset)
        *offset = QPoint(0, 0);
    return m_window;
}


MTWindow::MTWindow(QString qmlFile)
    : m_qmlComponent(nullptr),
    m_rootItem(nullptr),
    m_quickInitialized(false),
    m_psrRequested(false)
{
    mQmlFile = qmlFile;

    setSurfaceType(QSurface::OpenGLSurface);

    QSurfaceFormat format;
    // Qt Quick may need a depth and stencil buffer. Always make sure these are available.
    format.setDepthBufferSize(16);
    format.setStencilBufferSize(8);
    setFormat(format);

    m_context = new QOpenGLContext;
    m_context->setFormat(format);
    m_context->create();

    m_offscreenSurface = new QOffscreenSurface;
    // Pass m_context->format(), not format. Format does not specify and color buffer
    // sizes, while the context, that has just been created, reports a format that has
    // these values filled in. Pass this to the offscreen surface to make sure it will be
    // compatible with the context's configuration.
    m_offscreenSurface->setFormat(m_context->format());
    m_offscreenSurface->create();

    m_renderControl = new RenderControl(this);

    // Create a QQuickWindow that is associated with out render control. Note that this
    // window never gets created or shown, meaning that it will never get an underlying
    // native (platform) window.
    m_quickWindow = new QQuickWindow(m_renderControl);

    // Create a QML engine.
    m_qmlEngine = new QQmlEngine;
    if (!m_qmlEngine->incubationController())
        m_qmlEngine->setIncubationController(m_quickWindow->incubationController());

    m_quickRenderer = new QuickRenderer;
    m_quickRenderer->setContext(m_context);

    // These live on the gui thread. Just give access to them on the render thread.
    m_quickRenderer->setSurface(m_offscreenSurface);
    m_quickRenderer->setWindow(this);
    m_quickRenderer->setQuickWindow(m_quickWindow);
    m_quickRenderer->setRenderControl(m_renderControl);

    m_quickRendererThread = new QThread;

    // Notify the render control that some scenegraph internals have to live on
    // m_quickRenderThread.
    m_renderControl->prepareThread(m_quickRendererThread);

    // The QOpenGLContext and the QObject representing the rendering logic on
    // the render thread must live on that thread.
    m_context->moveToThread(m_quickRendererThread);
    m_quickRenderer->moveToThread(m_quickRendererThread);

    m_quickRendererThread->start();

    // Now hook up the signals. For simplicy we don't differentiate
    // between renderRequested (only render is needed, no sync) and
    // sceneChanged (polish and sync is needed too).
    connect(m_renderControl, &QQuickRenderControl::renderRequested, this, &MTWindow::requestUpdate);
    connect(m_renderControl, &QQuickRenderControl::sceneChanged, this, &MTWindow::requestUpdate);
}

MTWindow::~MTWindow()
{
    // Release resources and move the context ownership back to this thread.
    m_quickRenderer->mutex()->lock();
    m_quickRenderer->requestStop();
    m_quickRenderer->cond()->wait(m_quickRenderer->mutex());
    m_quickRenderer->mutex()->unlock();

    m_quickRendererThread->quit();
    m_quickRendererThread->wait();

    delete m_renderControl;
    delete m_qmlComponent;
    delete m_quickWindow;
    delete m_qmlEngine;

    delete m_offscreenSurface;
    delete m_context;
}

void MTWindow::requestUpdate()
{
    if (m_quickInitialized && !m_psrRequested) {
        m_psrRequested = true;
        QCoreApplication::postEvent(this, new QEvent(UPDATE));
    }
}

#include <QTimer>
bool MTWindow::event(QEvent *e)
{
    if (e->type() == UPDATE) {
        polishSyncAndRender();
        m_psrRequested = false;
        return true;
    } else if (e->type() == QEvent::Close) {
        // Avoid rendering on the render thread when the window is about to
        // close. Once a QWindow is closed, the underlying platform window will
        // go away, even though the QWindow instance itself is still
        // valid. Operations like swapBuffers() are futile and only result in
        // warnings afterwards. Prevent this.
        m_quickRenderer->aboutToQuit();
    }

    return QWindow::event(e);
}

void MTWindow::polishSyncAndRender()
{
    // qDebug() << "processing:" << m_quickRenderer->mProcessing;

    // // 假如qml画面还在渲染中，就等待其渲染完成后再处理
    // // 期间不断处理其他事件（键盘、鼠标、绘制等）
    // while (m_quickRenderer->mProcessing == true) {
    //     qApp->processEvents();
    // }

    QElapsedTimer timer;
    timer.start();

    // qDebug() << "主窗口请求渲染";

    Q_ASSERT(QThread::currentThread() == thread());

    // Polishing happens on the gui thread.
    m_renderControl->polishItems();

    // qDebug() << "主窗口渲染耗时-->a:" << timer.elapsed();

    // Sync happens on the render thread with the gui thread (this one) blocked.
    QMutexLocker lock(m_quickRenderer->mutex());
    m_quickRenderer->requestRender();

    // qDebug() << "主窗口渲染耗时-->b:" << timer.elapsed();

    // Wait until sync is complete.
    m_quickRenderer->cond()->wait(m_quickRenderer->mutex());

    // qDebug() << "主窗口渲染耗时:" << timer.elapsed();

    // without blocking？ 前面的wait不是已经bloking了吗？

    // Rendering happens on the render thread without blocking the gui (main)
    // thread. This is good because the blocking swap (waiting for vsync)
    // happens on the render thread, not blocking other work.
}

void MTWindow::run()
{
    disconnect(m_qmlComponent, &QQmlComponent::statusChanged, this, &MTWindow::run);

    if (m_qmlComponent->isError()) {
        const QList<QQmlError> errorList = m_qmlComponent->errors();
        for (const QQmlError &error : errorList)
            qWarning() << error.url() << error.line() << error;
        return;
    }

    QObject *rootObject = m_qmlComponent->create();
    if (m_qmlComponent->isError()) {
        const QList<QQmlError> errorList = m_qmlComponent->errors();
        for (const QQmlError &error : errorList)
            qWarning() << error.url() << error.line() << error;
        return;
    }

    m_rootItem = qobject_cast<QQuickItem *>(rootObject);
    if (!m_rootItem) {
        qWarning("run: Not a QQuickItem");
        delete rootObject;
        return;
    }

    // The root item is ready. Associate it with the window.
    m_rootItem->setParentItem(m_quickWindow->contentItem());

    // Update item and rendering related geometries.
    updateSizes();

    m_quickInitialized = true;

    // Initialize the render thread and perform the first polish/sync/render.
    m_quickRenderer->requestInit();
    polishSyncAndRender();
}

void MTWindow::updateSizes()
{
    // Behave like SizeRootObjectToView.
    m_rootItem->setWidth(width());
    m_rootItem->setHeight(height());

    m_quickWindow->setGeometry(0, 0, width(), height());
}

void MTWindow::startQuick(const QString &filename)
{
    m_qmlComponent = new QQmlComponent(m_qmlEngine, QUrl(filename));
    if (m_qmlComponent->isLoading())
        connect(m_qmlComponent, &QQmlComponent::statusChanged, this, &MTWindow::run);
    else
        run();
}

void MTWindow::exposeEvent(QExposeEvent *)
{
    if (isExposed()) {
        if (!m_quickInitialized)
        {
            // startQuick(QStringLiteral("qrc:/qml/MyScene.qml"));
            startQuick(mQmlFile);
        }
    }
}

void MTWindow::resizeEvent(QResizeEvent *)
{
    // If this is a resize after the scene is up and running, recreate the fbo and the
    // Quick item and scene.
    if (m_rootItem) {
        updateSizes();
        m_quickRenderer->requestResize();
        polishSyncAndRender();
    }
}

void MTWindow::mousePressEvent(QMouseEvent *e)
{
    // Use the constructor taking localPos and screenPos. That puts localPos into the
    // event's localPos and windowPos, and screenPos into the event's screenPos. This way
    // the windowPos in e is ignored and is replaced by localPos. This is necessary
    // because QQuickWindow thinks of itself as a top-level window always.
    QMouseEvent mappedEvent(e->type(), e->localPos(), e->screenPos(), e->button(), e->buttons(), e->modifiers());
    QCoreApplication::sendEvent(m_quickWindow, &mappedEvent);
}

void MTWindow::mouseReleaseEvent(QMouseEvent *e)
{
    QMouseEvent mappedEvent(e->type(), e->localPos(), e->screenPos(), e->button(), e->buttons(), e->modifiers());
    QCoreApplication::sendEvent(m_quickWindow, &mappedEvent);
}

void MTWindow::mouseMoveEvent(QMouseEvent *e)
{
    QMouseEvent mappedEvent(e->type(),
                            e->localPos(),
                            e->screenPos(),
                            e->button(),
                            e->buttons(),
                            e->modifiers());
    QCoreApplication::sendEvent(m_quickWindow, &mappedEvent);
}

void MTWindow::wheelEvent(QWheelEvent *e)
{
    QWheelEvent mappedEvent(e->position(),
                            e->globalPosition(),
                            e->pixelDelta(),
                            e->angleDelta(),
                            e->buttons(),
                            e->modifiers(),
                            e->phase(),
                            e->inverted(),
                            e->source());
    QCoreApplication::sendEvent(m_quickWindow, &mappedEvent);
}
