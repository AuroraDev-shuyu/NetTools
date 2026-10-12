#ifndef FORMQUICSERVER_H
#define FORMQUICSERVER_H

#include <QWidget>

namespace Ui {
class FormQuicServer;
}

class FormQuicServer : public QWidget
{
    Q_OBJECT

public:
    explicit FormQuicServer(QWidget *parent = nullptr);
    ~FormQuicServer();

private:
    Ui::FormQuicServer *ui;
};

#endif // FORMQUICSERVER_H
