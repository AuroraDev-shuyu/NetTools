#include "formquicserver.h"
#include "ui_formquicserver.h"

FormQuicServer::FormQuicServer(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormQuicServer)
{
    ui->setupUi(this);
}

FormQuicServer::~FormQuicServer()
{
    delete ui;
}
