#ifndef MAINWIDGET_H
#define MAINWIDGET_H
#include <QWidget>
#include <QCloseEvent>
#include "formtcpserver.h"
#include "formtcpclient.h"
#include "formudpserver.h"
#include "formudpclient.h"
#include "formpop3client.h"
#include "formavanalyzer.h"
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWidget;
}
QT_END_NAMESPACE
class MainWidget : public QWidget
{
    Q_OBJECT
public:
    MainWidget(QWidget *parent = nullptr);
    ~MainWidget();
protected:
    // 窗口关闭事件，保存所有子窗口的日志
    void closeEvent(QCloseEvent *event) override;
private:
    Ui::MainWidget *ui;
};
#endif // MAINWIDGET_H
