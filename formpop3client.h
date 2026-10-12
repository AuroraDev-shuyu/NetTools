#ifndef FORMPOP3CLIENT_H
#define FORMPOP3CLIENT_H

#include <QWidget>

namespace Ui {
class FormPop3Client;
}

class FormPop3Client : public QWidget
{
    Q_OBJECT

public:
    explicit FormPop3Client(QWidget *parent = nullptr);
    ~FormPop3Client();

private:
    Ui::FormPop3Client *ui;
};

#endif // FORMPOP3CLIENT_H
