#ifndef FORMDATAPROCESSOR_H
#define FORMDATAPROCESSOR_H

#include <QWidget>

#include "formchilddataconversion.h"
#include "formchilddatavalidation.h"

namespace Ui {
class FormDataProcessor;
}

class FormDataProcessor : public QWidget
{
    Q_OBJECT

public:
    explicit FormDataProcessor(QWidget *parent = nullptr);
    ~FormDataProcessor();
    void saveLog();
private:
    Ui::FormDataProcessor *ui;
};

#endif // FORMDATAPROCESSOR_H
