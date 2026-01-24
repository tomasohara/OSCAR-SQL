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

void SQLEditor::on_okButton_clicked()
{
    accept();
}

void SQLEditor::on_cancelButton_clicked()
{
    reject();
}
