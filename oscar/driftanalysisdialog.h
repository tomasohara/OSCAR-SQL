/* Drift Analysis Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DRIFTANALYSISDIALOG_H
#define DRIFTANALYSISDIALOG_H

#include <QDialog>
#include <QDate>
#include "SleepLib/machine.h"
#include "database/device_time_correction_repository.h"

namespace Ui {
class DriftAnalysisDialog;
}

class DriftAnalysisDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DriftAnalysisDialog(QWidget *parent = nullptr);
    ~DriftAnalysisDialog();

    void setDate(const QDate& date);

signals:
    void correctionsChanged();

private slots:
    void onDutDeviceChanged(int);
    void onRefDeviceChanged(int);
    void onLoadDriftData();
    void onFitDrift();
    void onUseDrift();

private:
    Ui::DriftAnalysisDialog *ui;
    QDate m_date;

    struct DriftPoint { QDate date; double offsetMs; double tNoonMs; };
    QList<DriftPoint> m_driftPoints;
    double  m_fitC0Ms  = 0.0;
    double  m_fitSlope = 0.0;
    bool    m_fitValid = false;

    // Active drift model state for the current DUT, loaded on Load Data
    bool    m_hasExistingModel   = false;
    double  m_existingModelC0Ms  = 0.0;
    double  m_existingModelSlope = 0.0;  // raw slope in ms/ms
    qint64  m_existingModelRowId = -1;
    QDate   m_existingModelFrom;

    void     populateDutCombo();
    void     populateRefCombo();
    Machine* currentDutMachine() const;
    Machine* currentRefMachine() const;  // nullptr when "None" is selected
    void     resetPlotState();
    void     rebuildMachine(Machine* mach);
    void     refreshCurrentModelLabel();
};

#endif // DRIFTANALYSISDIALOG_H
