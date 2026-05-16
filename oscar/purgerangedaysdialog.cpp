/* Purge Range of Days Dialog
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "purgerangedaysdialog.h"

#include <QButtonGroup>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

PurgeRangeDaysDialog::PurgeRangeDaysDialog(QDate initialDate, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Purge Range of Days"));
    setModal(true);

    // Date range inputs
    auto *formLayout = new QFormLayout;
    m_startEdit = new QDateEdit(initialDate, this);
    m_startEdit->setCalendarPopup(true);
    m_startEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_endEdit = new QDateEdit(initialDate, this);
    m_endEdit->setCalendarPopup(true);
    m_endEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    formLayout->addRow(tr("Start date:"), m_startEdit);
    formLayout->addRow(tr("End date:"),   m_endEdit);

    // Data type — six mutually-exclusive radio buttons
    auto *typeBox    = new QGroupBox(tr("Data to purge"), this);
    auto *typeLayout = new QVBoxLayout(typeBox);
    m_typeGroup = new QButtonGroup(this);

    struct { const char *label; int id; } rows[] = {
        { QT_TR_NOOP("CPAP"),                MT_CPAP       },
        { QT_TR_NOOP("Oximetry"),            MT_OXIMETER   },
        { QT_TR_NOOP("Sleep Stage"),         MT_SLEEPSTAGE },
        { QT_TR_NOOP("Position"),            MT_POSITION   },
        { QT_TR_NOOP("All except Notes"),    MT_UNKNOWN    },
        { QT_TR_NOOP("All including Notes"), MT_JOURNAL    },
    };
    for (auto &row : rows) {
        auto *rb = new QRadioButton(tr(row.label), typeBox);
        m_typeGroup->addButton(rb, row.id);
        typeLayout->addWidget(rb);
    }
    m_typeGroup->button(MT_CPAP)->setChecked(true);

    // Validation error label (hidden until needed)
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QStringLiteral("color: red;"));
    m_errorLabel->hide();

    // OK / Cancel
    auto *buttonBox = new QDialogButtonBox(this);
    auto *okBtn     = buttonBox->addButton(QDialogButtonBox::Ok);
    auto *cancelBtn = buttonBox->addButton(QDialogButtonBox::Cancel);
    connect(okBtn,     &QPushButton::clicked, this, &PurgeRangeDaysDialog::onOkClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(typeBox);
    mainLayout->addWidget(m_errorLabel);
    mainLayout->addWidget(buttonBox);
}

QDate PurgeRangeDaysDialog::startDate()   const { return m_startEdit->date(); }
QDate PurgeRangeDaysDialog::endDate()     const { return m_endEdit->date();   }

MachineType PurgeRangeDaysDialog::machineType() const
{
    return static_cast<MachineType>(m_typeGroup->checkedId());
}

void PurgeRangeDaysDialog::onOkClicked()
{
    if (m_endEdit->date() < m_startEdit->date()) {
        m_errorLabel->setText(tr("End date must be on or after start date."));
        m_errorLabel->show();
        return;
    }
    accept();
}
