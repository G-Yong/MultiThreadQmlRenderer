# MultiThreadQmlRenderer
>Qt版本:`5.15.2`

使用多线程来渲染qml，使得主线程能够具备更好的实时性   
本类的实现主要是参考了Qt自带的例程【QQuickRenderControl Example】   
相较于【QQuickRenderControl Example】，主要做了两个改动:   
* 1.将渲染的目标从`QWindow`改为`QWidget`   
* 2.在等待渲染时，不死等，而是调用`qApp->processEvents()`处理事务，使得UI线程更具实时性   

## 存在的问题
* 1.貌似无法接收到正常的刷新信号。目前只能用一个定时器来不断发出画面刷新信号
* 2.在场景加载完成后，再改变控件的尺寸，会导致渲染失效
* 3.析构时好像会崩溃