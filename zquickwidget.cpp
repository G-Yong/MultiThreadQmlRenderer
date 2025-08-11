#include "zquickwidget.h"

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

#include <QTimer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPainter>

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
    m_quit(false),
    m_widget(nullptr)
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
    QMutexLocker lock(&m_mutex);

    switch (int(e->type())) {
    case INIT:
        init();
        return true;
    case RENDER:{
        // 之所以主线程需要等那么久，是因为本线程还在处理，
        // 无法进入事件处理，因此一直在等待
        render(&lock);
    }
        return true;
    case RESIZE:
        return true;
    case STOP:
        cleanup();
        return true;
    default:
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

    m_renderControl->initialize(m_context);
}

void QuickRenderer::cleanup()
{
    m_context->makeCurrent(m_surface);

    m_renderControl->invalidate();

    delete m_fbo;
    m_fbo = nullptr;

    m_context->doneCurrent();
    m_context->moveToThread(QCoreApplication::instance()->thread());

    m_cond.wakeOne();
}

void QuickRenderer::ensureFbo()
{
    // qDebug() << m_window->size();

    if (m_fbo && m_fbo->size() != m_widget->size() * m_widget->devicePixelRatio()) {
        delete m_fbo;
        m_fbo = nullptr;
    }

    if (!m_fbo) {
        m_fbo = new QOpenGLFramebufferObject(m_widget->size() * m_widget->devicePixelRatio(),
                                             QOpenGLFramebufferObject::CombinedDepthStencil);
        m_quickWindow->setRenderTarget(m_fbo);
    }
}

void QuickRenderer::render(QMutexLocker *lock)
{
    mProcessState = 1;
    mFinished = false;

    QElapsedTimer timer;
    timer.start();

    // qDebug() << "m window:" << m_window;
    // qDebug() << "===" << m_surface << m_window->size() << m_window->devicePixelRatio();

    Q_ASSERT(QThread::currentThread() != m_window->thread());

    if (!m_context->makeCurrent(m_surface)) {
        qWarning("Failed to make context current on render thread");
        mFinished = true;
        return;
    }

    ensureFbo();

    // Synchronization and rendering happens here on the render thread.
    m_renderControl->sync();


    // ui线程目前可以继续操作了
    // The gui thread can now continue.
    m_cond.wakeOne();
    lock->unlock();

    mFinished = true;
    mProcessState = 2;

    // qDebug() << "渲染同步到ui耗时：" << timer.elapsed();

    // Meanwhile on this thread continue with the actual rendering (into the FBO first).
    m_renderControl->render();
    m_context->functions()->glFlush();

    // m_renderControl->grab().save("123.png");
    emit rendered(m_renderControl->grab().copy());

    // qDebug() << "子线程渲染耗时：" << timer.elapsed();

    mProcessState = 3;
}

void QuickRenderer::aboutToQuit()
{
    QMutexLocker lock(&m_quitMutex);
    m_quit = true;
}

// 主要是这里要用到window
class RenderControl : public QQuickRenderControl
{
public:
    explicit RenderControl(QObject *parent = nullptr){}
    void setWindow(QWindow *w)
    {
        m_window = w;
    }
    QWindow *renderWindow(QPoint *offset) override;

private:
    QWindow *m_window = nullptr;
};
QWindow *RenderControl::renderWindow(QPoint *offset)
{
    if (offset)
        *offset = QPoint(0, 0);

    return m_window;
}


ZQuickWidget::ZQuickWidget(QWidget *parent)
    :
    QWidget(parent),
    m_qmlComponent(nullptr),
    m_rootItem(nullptr),
    m_quickInitialized(false),
    m_psrRequested(false)
{
    // setSurfaceType(QSurface::OpenGLSurface);

    // QSurfaceFormat format;
    // // Qt Quick may need a depth and stencil buffer. Always make sure these are available.
    // format.setDepthBufferSize(16);
    // format.setStencilBufferSize(8);
    // setFormat(format);

    m_context = new QOpenGLContext;
    m_context->setFormat(QSurfaceFormat::defaultFormat());
    m_context->create();

    m_offscreenSurface = new QOffscreenSurface;
    // Pass m_context->format(), not format. Format does not specify and color buffer
    // sizes, while the context, that has just been created, reports a format that has
    // these values filled in. Pass this to the offscreen surface to make sure it will be
    // compatible with the context's configuration.
    m_offscreenSurface->setFormat(m_context->format());
    m_offscreenSurface->create();

    // m_renderControl = new RenderControl(this);
    m_renderControl = new RenderControl();


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

    connect(m_quickRenderer, &QuickRenderer::rendered, this, [=](QImage img){
        mImg = img;
        update();
    }, Qt::QueuedConnection);

    // These live on the gui thread. Just give access to them on the render thread.
    m_quickRenderer->setSurface(m_offscreenSurface);
    // m_quickRenderer->setWindow(this->windowHandle()); // 此时还没创建
    m_quickRenderer->setWidget(this);
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
    connect(m_renderControl, &QQuickRenderControl::renderRequested, this, &ZQuickWidget::requestUpdate);
    connect(m_renderControl, &QQuickRenderControl::sceneChanged, this, &ZQuickWidget::requestUpdate);
}

ZQuickWidget::~ZQuickWidget()
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

int ZQuickWidget::setSource(QUrl url)
{
    mQmlFile = url.url();

    QTimer::singleShot(2000, [=](){
        // qDebug() << "start quick:" << mQmlFile;

        // 查找到主窗口句柄
        QWindow *wh = nullptr;
        QWidget *w = this;
        do{
            if(w == nullptr)
            {
                break;
            }

            wh = w->windowHandle();
            if(wh != nullptr)
            {
                break;
            }
            else
            {
                w = w->parentWidget();
            }
        }while(1);

        ((RenderControl*)m_renderControl)->setWindow(wh);
        m_quickRenderer->setWindow(wh);
        startQuick(mQmlFile);
    });

    QTimer *uTimer = new QTimer();
    connect(uTimer, &QTimer::timeout, this, [=](){
        requestUpdate();
    });
    uTimer->start(30);

    return 0;
}

void ZQuickWidget::requestUpdate()
{
    if (m_quickInitialized && !m_psrRequested) {
        m_psrRequested = true;
        QCoreApplication::postEvent(this, new QEvent(UPDATE));
    }
}

bool ZQuickWidget::event(QEvent *e)
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

    return QWidget::event(e);
}

void ZQuickWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    if(mImg.isNull() == false)
    {
        painter.drawImage(0, 0, mImg);
    }

    // qDebug() << "painter";
}

void ZQuickWidget::polishSyncAndRender()
{
    // qDebug() << "processing:" << m_quickRenderer->mProcessState;

    // 假如还在渲染中，就等待渲染完成后再处理
    // 期间不断处理其他事件（键盘、鼠标、绘制等）
    while (m_quickRenderer->mProcessState > 0 && m_quickRenderer->mProcessState != 3) {
        qApp->processEvents();
        // return;
    }

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
    qApp->processEvents();

    // qDebug() << "主窗口渲染耗时-->b:" << timer.elapsed();

    // Wait until sync is complete.
    m_quickRenderer->cond()->wait(m_quickRenderer->mutex());
    // while (m_quickRenderer->mFinished == false) {
    //     qApp->processEvents();

    //     // qDebug() << m_quickRenderer->mProcessState;
    //     QThread::msleep(100);
    // }

    // qDebug() << "主窗口渲染耗时:" << timer.elapsed();

    // without blocking？ 前面的wait不是已经bloking了吗？

    // Rendering happens on the render thread without blocking the gui (main)
    // thread. This is good because the blocking swap (waiting for vsync)
    // happens on the render thread, not blocking other work.
}

void ZQuickWidget::run()
{
    disconnect(m_qmlComponent, &QQmlComponent::statusChanged, this, &ZQuickWidget::run);

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

void ZQuickWidget::updateSizes()
{
    // Behave like SizeRootObjectToView.
    m_rootItem->setWidth(width());
    m_rootItem->setHeight(height());

    m_quickWindow->setGeometry(0, 0, width(), height());
}

void ZQuickWidget::startQuick(const QString &filename)
{
    m_qmlComponent = new QQmlComponent(m_qmlEngine, QUrl(filename));
    if (m_qmlComponent->isLoading())
        connect(m_qmlComponent, &QQmlComponent::statusChanged, this, &ZQuickWidget::run);
    else
        run();
}

void ZQuickWidget::resizeEvent(QResizeEvent *)
{
    // If this is a resize after the scene is up and running, recreate the fbo and the
    // Quick item and scene.
    if (m_rootItem) {
        updateSizes();
        m_quickRenderer->requestResize();
        polishSyncAndRender();
    }
}

void ZQuickWidget::mousePressEvent(QMouseEvent *e)
{
    // Use the constructor taking localPos and screenPos. That puts localPos into the
    // event's localPos and windowPos, and screenPos into the event's screenPos. This way
    // the windowPos in e is ignored and is replaced by localPos. This is necessary
    // because QQuickWindow thinks of itself as a top-level window always.
    QMouseEvent mappedEvent(e->type(), e->localPos(), e->screenPos(), e->button(), e->buttons(), e->modifiers());
    QCoreApplication::sendEvent(m_quickWindow, &mappedEvent);
}

void ZQuickWidget::mouseReleaseEvent(QMouseEvent *e)
{
    QMouseEvent mappedEvent(e->type(), e->localPos(), e->screenPos(), e->button(), e->buttons(), e->modifiers());
    QCoreApplication::sendEvent(m_quickWindow, &mappedEvent);
}
