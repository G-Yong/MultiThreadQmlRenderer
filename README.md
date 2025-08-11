# MultiThreadQmlRenderer
使用多线程来渲染qml，使得主线程能够具备更好的实时性   
本类的实现主要是参考了Qt自带的例程【QQuickRenderControl Example】实现的   
相较于【QQuickRenderControl Example】，主要做了两个改动   
-1.将渲染的目标从`QWindow`改为`QWidget`   
-2.在等待渲染时，不死等，而是调用`qApp->processEvents()`处理事务，使得UI线程更具实时性   