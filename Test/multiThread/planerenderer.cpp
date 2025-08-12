#include "planerenderer.h"
#include "planerenderer.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOffscreenSurface>
#include <QWindow>
#include <QThread>

PlaneRenderer::PlaneRenderer(QOffscreenSurface *offscreenSurface)
    : m_offscreenSurface(offscreenSurface),
    m_context(nullptr),
    m_program(nullptr),
    m_vbo(nullptr),
    m_vao(nullptr),
    m_matrixLoc(0)
{
}

PlaneRenderer::~PlaneRenderer()
{
    // Use a temporary offscreen surface to do the cleanup.
    // There may not be a native window surface available anymore at this stage.
    m_context->makeCurrent(m_offscreenSurface);

    delete m_program;
    delete m_vbo;
    delete m_vao;

    m_context->doneCurrent();
    delete m_context;
}

void PlaneRenderer::init(QWindow *w, QOpenGLContext *share)
{
    m_context = new QOpenGLContext;
    m_context->setShareContext(share);
    m_context->setFormat(w->requestedFormat());
    m_context->create();
    if (!m_context->makeCurrent(w))
        return;

    QOpenGLFunctions *f = m_context->functions();
    f->glClearColor(0.0f, 0.1f, 0.25f, 1.0f);
    f->glViewport(0, 0, w->width() * w->devicePixelRatio(), w->height() * w->devicePixelRatio());

    static const char *vertexShaderSource =
        "attribute highp vec4 vertex;\n"
        "attribute lowp vec2 coord;\n"
        "varying lowp vec2 v_coord;\n"
        "void main() {\n"
        "   v_coord = coord;\n"
        "   gl_Position = vertex;\n"  // 直接使用顶点坐标，不再需要矩阵变换
        "}\n";
    static const char *fragmentShaderSource =
        "varying lowp vec2 v_coord;\n"
        "uniform sampler2D sampler;\n"
        "void main() {\n"
        "   gl_FragColor = vec4(texture2D(sampler, v_coord).rgb, 1.0);\n"
        "}\n";

    m_program = new QOpenGLShaderProgram;
    m_program->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    m_program->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    m_program->bindAttributeLocation("vertex", 0);
    m_program->bindAttributeLocation("coord", 1);
    m_program->link();
    m_matrixLoc = m_program->uniformLocation("matrix");

    m_vao = new QOpenGLVertexArrayObject;
    m_vao->create();
    QOpenGLVertexArrayObject::Binder vaoBinder(m_vao);

    m_vbo = new QOpenGLBuffer;
    m_vbo->create();
    m_vbo->bind();

    // 定义一个简单的四边形（两个三角形组成）
    GLfloat v[] = {
        -1.0f, -1.0f, 0.0f,  // 左下
        1.0f, -1.0f, 0.0f,  // 右下
        -1.0f,  1.0f, 0.0f,  // 左上

        -1.0f,  1.0f, 0.0f, // 左上
        1.0f, -1.0f, 0.0f,  // 右下
        1.0f,  1.0f, 0.0f   // 右上
    };

    // 对应的纹理坐标
    GLfloat texCoords[] = {
        0.0f, 0.0f,  // 左下
        1.0f, 0.0f,  // 右下
        0.0f, 1.0f,  // 左上

        0.0f, 1.0f,  // 左上
        1.0f, 0.0f,  // 右下
        1.0f, 1.0f   // 右上
    };

    const int vertexCount = 6;  // 现在只有6个顶点（两个三角形）
    m_vbo->allocate(sizeof(GLfloat) * vertexCount * 5);
    m_vbo->write(0, v, sizeof(GLfloat) * vertexCount * 3);
    m_vbo->write(sizeof(GLfloat) * vertexCount * 3, texCoords, sizeof(GLfloat) * vertexCount * 2);
    m_vbo->release();

    if (m_vao->isCreated())
        setupVertexAttribs();
}

void PlaneRenderer::resize(int w, int h)
{
    // 不需要投影矩阵了
    m_proj.setToIdentity();
}

void PlaneRenderer::setupVertexAttribs()
{
    m_vbo->bind();
    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);
    m_context->functions()->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    m_context->functions()->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0,
                                                  (const void *)(6 * 3 * sizeof(GLfloat)));  // 注意偏移量改为6*3
    m_vbo->release();
}

void PlaneRenderer::render(QWindow *w, QOpenGLContext *share, uint texture)
{
    if (!m_context)
        init(w, share);

    if (!m_context->makeCurrent(w))
        return;

    QOpenGLFunctions *f = m_context->functions();
    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (texture) {
        f->glBindTexture(GL_TEXTURE_2D, texture);

        // 移除不必要的3D渲染设置
        f->glDisable(GL_CULL_FACE);
        f->glDisable(GL_DEPTH_TEST);

        m_program->bind();
        QOpenGLVertexArrayObject::Binder vaoBinder(m_vao);
        if (!m_vao->isCreated())
            setupVertexAttribs();

        // 移除3D变换
        m_program->setUniformValue(m_matrixLoc, m_proj);

        // 绘制两个三角形（6个顶点）
        f->glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    m_context->swapBuffers(w);
}
