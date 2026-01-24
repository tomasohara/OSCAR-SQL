/* ExportCSV Module Header
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (C) 2011-2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef EXPORTCSV_H
#define EXPORTCSV_H

#include <QDateEdit>
#include <QDialog>
#include "SleepLib/machine_common.h"

namespace Ui {
class ExportCSV;
}


struct DumpField {
    DumpField() { code = NoChannel; mtype = MT_UNKNOWN; type = ST_CNT; }
    DumpField(ChannelID c, MachineType mt, SummaryType t) { code = c; mtype = mt; type = t; }
    DumpField(const DumpField &copy) {code = copy.code; mtype = copy.mtype; type = copy.type; }
    ChannelID code;
    MachineType mtype;
    SummaryType type;
};


/*! \class ExportCSV
    \brief Dialog for exporting SleepLib data in CSV Format
    This module still needs a lot of work
    */
class ExportCSV : public QDialog
{
    Q_OBJECT

  public:
    explicit ExportCSV(QWidget *parent = 0);
    ~ExportCSV();

  private slots:
    void on_filenameBrowseButton_clicked();

    void on_quickRangeCombo_currentTextChanged(const QString &arg1);

    void on_exportButton_clicked();
    
    void on_editSQLButton_clicked();

    void startDate_currentPageChanged(int year, int month);
    void endDate_currentPageChanged(int year, int month);


  private:
    void UpdateCalendarDay(QDateEdit *dateedit, QDate date);
    
    QString getDefaultQueryForReport(const QString &reportName, qint64 profile_id, 
                                     const QString &startDate, const QString &endDate);

    Ui::ExportCSV *ui;
    QList<DumpField> fields;
    QString m_customQuery;  ///< Custom SQL query set via Edit SQL Query dialog
    bool m_useCustomQuery;  ///< Flag indicating whether to use custom query
};

#endif // EXPORTCSV_H
