/* SQL Editor Dialog Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "sqleditor.h"
#include "ui_sqleditor.h"

SQLEditor::SQLEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SQLEditor)
{
    ui->setupUi(this);
    this->setWindowFlags(this->windowFlags() & ~Qt::WindowContextHelpButtonHint);
}

SQLEditor::~SQLEditor()
{
    delete ui;
}

void SQLEditor::setQuery(const QString &query)
{
    ui->queryEdit->setPlainText(query);
}

QString SQLEditor::getQuery() const
{
    return ui->queryEdit->toPlainText();
}

void SQLEditor::setReadOnly(bool readOnly)
{
    // Set query text read-only
    ui->queryEdit->setReadOnly(readOnly);
    
    if (readOnly) {
        // Change background to indicate read-only
        ui->queryEdit->setStyleSheet("QPlainTextEdit { background-color: #f0f0f0; }");
        
        // Change OK button to Close button
        ui->okButton->setText(tr("Close"));
        
        // Hide Cancel button in read-only mode
        ui->cancelButton->setVisible(false);
    } else {
        // Reset to editable style
        ui->queryEdit->setStyleSheet("");
        
        // Reset button text
        ui->okButton->setText(tr("OK"));
        
        // Show Cancel button
        ui->cancelButton->setVisible(true);
    }
}

void SQLEditor::on_okButton_clicked()
{
    accept();
}

void SQLEditor::on_cancelButton_clicked()
{
    reject();
}
