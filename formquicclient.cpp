#include "formquicclient.h"
#include "ui_formquicclient.h"

FormQuicClient::FormQuicClient(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormQuicClient)
{
    ui->setupUi(this);
}

FormQuicClient::~FormQuicClient()
{
    delete ui;
}
