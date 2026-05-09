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
    // Explicitly set light-mode colors so KDE dark palette doesn't bleed through.
    // OSCAR is a light-mode-only app but Qt's ColorScheme::Light hint is not
    // guaranteed on KDE Plasma, which can still supply a dark text color.
    ui->queryEdit->setStyleSheet("QPlainTextEdit { background-color: white; color: black; }");
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
        ui->queryEdit->setStyleSheet("QPlainTextEdit { background-color: #f0f0f0; color: black; }");
        ui->okButton->setText(tr("Close"));
        ui->cancelButton->setVisible(false);
    } else {
        ui->queryEdit->setStyleSheet("QPlainTextEdit { background-color: white; color: black; }");
        ui->okButton->setText(tr("OK"));
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
