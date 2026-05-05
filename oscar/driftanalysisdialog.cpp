/* Drift Analysis Dialog Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "driftanalysisdialog.h"
#include "ui_driftanalysisdialog.h"
#include "SleepLib/profiles.h"
#include "SleepLib/machine_common.h"
#include <QMessageBox>
#include <QComboBox>
#include <QDateTime>
#include <QtMath>

DriftAnalysisDialog::DriftAnalysisDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DriftAnalysisDialog)
{
    ui->setupUi(this);

    connect(ui->driftDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DriftAnalysisDialog::onDriftDeviceChanged);
    connect(ui->btnLoadDrift, &QPushButton::clicked, this, &DriftAnalysisDialog::onLoadDriftData);
    connect(ui->btnFitDrift,  &QPushButton::clicked, this, &DriftAnalysisDialog::onFitDrift);
    connect(ui->btnUseDrift,  &QPushButton::clicked, this, &DriftAnalysisDialog::onUseDrift);
}

DriftAnalysisDialog::~DriftAnalysisDialog()
{
    delete ui;
}

void DriftAnalysisDialog::setDate(const QDate& date)
{
    m_date = date;

    if (date.isValid()) {
        ui->driftStartDate->setDate(date.addDays(-30));
        ui->driftEndDate->setDate(date);
    }

    populateDeviceCombo();

    m_driftPoints.clear();
    m_fitValid = false;
    ui->driftPlot->clear();
    ui->btnFitDrift->setEnabled(false);
    ui->btnUseDrift->setEnabled(false);
    ui->driftStatusLabel->setText(tr("Select a device and date range, then click Load Data."));
}

void DriftAnalysisDialog::populateDeviceCombo()
{
    if (!p_profile) return;
    ui->driftDeviceCombo->blockSignals(true);
    ui->driftDeviceCombo->clear();
    for (Machine* mach : p_profile->GetMachines()) {
        if (!Machine::isCorrectableType(mach->type())) continue;
        QString label = mach->brand() + " " + mach->model();
        if (label.trimmed().isEmpty()) label = mach->loaderName();
        label += " (" + mach->serial() + ")";
        ui->driftDeviceCombo->addItem(label, QVariant::fromValue(reinterpret_cast<quintptr>(mach)));
    }
    ui->driftDeviceCombo->blockSignals(false);
}

Machine* DriftAnalysisDialog::currentMachine() const
{
    int idx = ui->driftDeviceCombo->currentIndex();
    if (idx < 0) return nullptr;
    return reinterpret_cast<Machine*>(ui->driftDeviceCombo->itemData(idx).value<quintptr>());
}

void DriftAnalysisDialog::rebuildMachine(Machine* mach)
{
    DeviceTimeCorrectionRepository repo;
    QList<DeviceTimeCorrectionData> dbRows = repo.findActive(mach->getDatabaseId());
    QList<TimeCorrectionRow> rows;
    rows.reserve(dbRows.size());
    for (const auto& d : dbRows) {
        TimeCorrectionRow r;
        r.dateFrom = QDate::fromString(d.dateFrom, Qt::ISODate);
        r.dateTo   = d.dateTo.isEmpty() ? QDate() : QDate::fromString(d.dateTo, Qt::ISODate);
        r.offsetMs = d.offsetMs;
        r.c0Ms     = d.c0Ms;
        r.c1       = d.c1;
        rows.append(r);
    }
    mach->rebuildCorrections(rows);
}

void DriftAnalysisDialog::onDriftDeviceChanged(int)
{
    m_driftPoints.clear();
    m_fitValid = false;
    ui->driftPlot->clear();
    ui->btnFitDrift->setEnabled(false);
    ui->btnUseDrift->setEnabled(false);
    ui->driftStatusLabel->setText(tr("Select a device and date range, then click Load Data."));
}

void DriftAnalysisDialog::onLoadDriftData()
{
    Machine* mach = currentMachine();
    if (!mach || mach->getDatabaseId() <= 0) {
        ui->driftStatusLabel->setText(tr("No device selected."));
        return;
    }
    QDate start = ui->driftStartDate->date();
    QDate end   = ui->driftEndDate->date();
    if (!start.isValid() || !end.isValid() || start >= end) {
        ui->driftStatusLabel->setText(tr("Invalid date range."));
        return;
    }

    DeviceTimeCorrectionRepository repo;
    m_driftPoints.clear();
    QList<DriftPlotWidget::Point> plotPts;
    for (const auto& r : repo.findManualOffsetRows(mach->getDatabaseId())) {
        QDate d = QDate::fromString(r.dateFrom, Qt::ISODate);
        if (d < start || d > end) continue;
        double t = QDateTime(d, QTime(12,0,0), Qt::UTC).toMSecsSinceEpoch();
        m_driftPoints.append({d, double(r.offsetMs), t});
        plotPts.append({d, double(r.offsetMs)});
    }

    m_fitValid = false;
    ui->driftPlot->setData(plotPts);
    ui->btnFitDrift->setEnabled(m_driftPoints.size() >= 3);
    ui->btnUseDrift->setEnabled(false);

    ui->driftStatusLabel->setText(m_driftPoints.isEmpty()
        ? tr("No drift entries found in the selected range.")
        : tr("%1 drift entries loaded. Click Fit Model to compute the drift rate.").arg(m_driftPoints.size()));
}

void DriftAnalysisDialog::onFitDrift()
{
    if (m_driftPoints.size() < 3) return;

    double n = m_driftPoints.size(), sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (const auto& pt : m_driftPoints) {
        sx += pt.tNoonMs; sy += pt.offsetMs;
        sxx += pt.tNoonMs * pt.tNoonMs; sxy += pt.tNoonMs * pt.offsetMs;
    }
    double denom = n * sxx - sx * sx;
    if (qAbs(denom) < 1e-10) {
        ui->driftStatusLabel->setText(tr("Cannot fit: all observations fall on the same date."));
        return;
    }

    double slope = (n * sxy - sx * sy) / denom;
    double c0    = (sy - slope * sx) / n;

    double yMean = sy / n, ssTot = 0, ssRes = 0;
    for (const auto& pt : m_driftPoints) {
        double pred = c0 + slope * pt.tNoonMs;
        ssRes += (pt.offsetMs - pred) * (pt.offsetMs - pred);
        ssTot += (pt.offsetMs - yMean) * (pt.offsetMs - yMean);
    }
    double r2 = (ssTot > 0) ? (1.0 - ssRes / ssTot) : 1.0;

    m_fitC0Ms  = c0;
    m_fitSlope = slope;
    m_fitValid = r2 >= 0.5;

    ui->driftPlot->setModel(c0, slope);
    ui->btnUseDrift->setEnabled(m_fitValid);

    QString status = tr("R²=%1, drift rate=%2 ms/day")
        .arg(r2, 0, 'f', 3)
        .arg(slope * 86400000.0, 0, 'f', 1);
    if (r2 < 0.5)       status += tr(" — poor fit, Use Model disabled");
    else if (r2 < 0.8)  status += tr(" — moderate fit");
    ui->driftStatusLabel->setText(status);
}

void DriftAnalysisDialog::onUseDrift()
{
    if (!m_fitValid) return;
    Machine* mach = currentMachine();
    if (!mach || mach->getDatabaseId() <= 0) return;

    QDate fitStart = ui->driftStartDate->date();
    QDate fitEnd   = ui->driftEndDate->date();

    DeviceTimeCorrectionRepository repo;
    for (const auto& r : repo.findManualOffsetRows(mach->getDatabaseId())) {
        QDate d = QDate::fromString(r.dateFrom, Qt::ISODate);
        if (d >= fitStart && d <= fitEnd) repo.markUndone(r.id);
    }

    DeviceTimeCorrectionData modelRow;
    modelRow.machineId = mach->getDatabaseId();
    modelRow.dateFrom  = fitStart.toString(Qt::ISODate);
    modelRow.dateTo    = fitEnd.toString(Qt::ISODate);
    modelRow.type      = "drift";
    modelRow.c0Ms      = qint64(m_fitC0Ms);
    modelRow.c1        = m_fitSlope + 1.0;
    modelRow.reason    = tr("Fitted drift model");

    if (repo.create(modelRow) < 0) {
        QMessageBox::warning(this, tr("Drift Analysis"), tr("Failed to save drift model."));
        return;
    }

    rebuildMachine(mach);

    m_driftPoints.clear();
    m_fitValid = false;
    ui->driftPlot->clear();
    ui->btnFitDrift->setEnabled(false);
    ui->btnUseDrift->setEnabled(false);
    ui->driftStatusLabel->setText(tr("Model committed. Manual drift entries replaced."));

    emit correctionsChanged();
}
