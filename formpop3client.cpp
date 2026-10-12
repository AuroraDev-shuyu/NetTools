#include "formpop3client.h"
#include "ui_formpop3client.h"

FormPop3Client::FormPop3Client(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormPop3Client)
{
    ui->setupUi(this);
}

FormPop3Client::~FormPop3Client()
{
    delete ui;
}
