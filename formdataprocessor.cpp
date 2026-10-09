#include "formdataprocessor.h"
#include "ui_formdataprocessor.h"

FormDataProcessor::FormDataProcessor(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormDataProcessor)
{
    ui->setupUi(this);
}

FormDataProcessor::~FormDataProcessor()
{
    delete ui;
}

// 保存日志（公共接口，提供主窗口调用）
void FormDataProcessor::saveLog()
{
    //检查ui对象和标签页控件是否存在，防止访问空指针
    if(ui && ui->tabWidget_DataConversion){
        try {
            FormChildDataConversion *conversionTab=qobject_cast<FormChildDataConversion*>(
                ui->tabWidget_DataConversion->widget(0));  // 获取索引0的窗口

            if(conversionTab){
                conversionTab->saveLog();
            }

            FormChildDataValidation *validationTab=qobject_cast<FormChildDataValidation*>(
                ui->tabWidget_DataConversion->widget(1));  // 获取索引1的窗口

            if(validationTab){
                validationTab->saveLog();
            }

        } catch (const std::exception& e) {
            qWarning()<<"保存数据转换工具日志失败："<<e.what();
        }
    }

}
