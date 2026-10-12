#ifndef FORMQUICCLIENT_H
#define FORMQUICCLIENT_H

#include <QWidget>

namespace Ui {
class FormQuicClient;
}

class FormQuicClient : public QWidget
{
    Q_OBJECT

public:
    explicit FormQuicClient(QWidget *parent = nullptr);
    ~FormQuicClient();

private:
    Ui::FormQuicClient *ui;
};

#endif // FORMQUICCLIENT_H
