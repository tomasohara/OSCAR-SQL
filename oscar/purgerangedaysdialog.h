/* Purge Range of Days Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Modal dialog for selecting a date range and data type for a range purge.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PURGERANGEDAYSDIALOG_H
#define PURGERANGEDAYSDIALOG_H

#include <QDate>
#include <QDialog>
#include "SleepLib/machine_common.h"

class QDateEdit;
class QButtonGroup;
class QLabel;

/*!
 * \class PurgeRangeDaysDialog
 * \brief Modal dialog for selecting a date range and data type to purge.
 *
 * Both date fields default to \a initialDate (the currently viewed day).
 * Validates end >= start before accepting. The six radio buttons mirror the
 * "Purge Current Selected Day" menu options.
 *
 * Usage:
 * \code
 * PurgeRangeDaysDialog dlg(daily->getDate(), this);
 * if (dlg.exec() == QDialog::Accepted) {
 *     QDate start      = dlg.startDate();
 *     QDate end        = dlg.endDate();
 *     MachineType type = dlg.machineType();
 * }
 * \endcode
 */
class PurgeRangeDaysDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PurgeRangeDaysDialog(QDate initialDate, QWidget *parent = nullptr);

    //! \brief Returns the selected start date.
    QDate startDate() const;

    //! \brief Returns the selected end date.
    QDate endDate() const;

    //! \brief Returns the selected machine type to purge.
    MachineType machineType() const;

private slots:
    void onOkClicked();

private:
    QDateEdit    *m_startEdit;
    QDateEdit    *m_endEdit;
    QButtonGroup *m_typeGroup;
    QLabel       *m_errorLabel;
};

#endif // PURGERANGEDAYSDIALOG_H
