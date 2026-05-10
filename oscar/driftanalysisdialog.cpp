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

    connect(ui->dutDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DriftAnalysisDialog::onDutDeviceChanged);
    connect(ui->refDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DriftAnalysisDialog::onRefDeviceChanged);
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

    populateDutCombo();
    populateRefCombo();
    resetPlotState();
}

void DriftAnalysisDialog::resetPlotState()
{
    m_driftPoints.clear();
    m_fitValid           = false;
    m_hasExistingModel   = false;
    m_existingModelRowId = -1;
    ui->driftPlot->clear();
    ui->btnFitDrift->setEnabled(false);
    ui->btnUseDrift->setEnabled(false);
    ui->driftStatusLabel->setText(tr("Select a device and date range, then click Load Data."));
}

void DriftAnalysisDialog::populateDutCombo()
{
    if (!p_profile) return;
    ui->dutDeviceCombo->blockSignals(true);
    ui->dutDeviceCombo->clear();
    for (Machine* mach : p_profile->GetMachines()) {
        if (mach->type() != MT_CPAP) continue;
        QString label = mach->brand() + " " + mach->model();
        if (label.trimmed().isEmpty()) label = mach->loaderName();
        label += " (" + mach->serial() + ")";
        ui->dutDeviceCombo->addItem(label, QVariant::fromValue(reinterpret_cast<quintptr>(mach)));
    }
    if (ui->dutDeviceCombo->count() == 0) {
        ui->dutDeviceCombo->addItem(tr("No CPAP devices found"));
        ui->btnLoadDrift->setEnabled(false);
    } else {
        ui->btnLoadDrift->setEnabled(true);
    }
    ui->dutDeviceCombo->blockSignals(false);
}

void DriftAnalysisDialog::populateRefCombo()
{
    if (!p_profile) return;
    ui->refDeviceCombo->blockSignals(true);
    ui->refDeviceCombo->clear();
    ui->refDeviceCombo->addItem(tr("— none —"), QVariant::fromValue(quintptr(0)));
    for (Machine* mach : p_profile->GetMachines()) {
        if (mach->type() == MT_CPAP) continue;
        if (!Machine::isCorrectableType(mach->type())) continue;
        QString label = mach->brand() + " " + mach->model();
        if (label.trimmed().isEmpty()) label = mach->loaderName();
        label += " (" + mach->serial() + ")";
        ui->refDeviceCombo->addItem(label, QVariant::fromValue(reinterpret_cast<quintptr>(mach)));
    }
    ui->refDeviceCombo->blockSignals(false);
}

Machine* DriftAnalysisDialog::currentDutMachine() const
{
    int idx = ui->dutDeviceCombo->currentIndex();
    if (idx < 0) return nullptr;
    quintptr ptr = ui->dutDeviceCombo->itemData(idx).value<quintptr>();
    return ptr ? reinterpret_cast<Machine*>(ptr) : nullptr;
}

Machine* DriftAnalysisDialog::currentRefMachine() const
{
    int idx = ui->refDeviceCombo->currentIndex();
    if (idx < 0) return nullptr;
    quintptr ptr = ui->refDeviceCombo->itemData(idx).value<quintptr>();
    return ptr ? reinterpret_cast<Machine*>(ptr) : nullptr;
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

void DriftAnalysisDialog::onDutDeviceChanged(int)
{
    resetPlotState();
}

void DriftAnalysisDialog::onRefDeviceChanged(int)
{
    resetPlotState();
}

void DriftAnalysisDialog::onLoadDriftData()
{
    Machine* mach = currentDutMachine();
    if (!mach || mach->getDatabaseId() <= 0) {
        ui->driftStatusLabel->setText(tr("No CPAP device selected."));
        return;
    }
    Machine* ref = currentRefMachine();
    if (!ref || ref->getDatabaseId() <= 0) {
        ui->driftStatusLabel->setText(tr("Select a reference device to measure CPAP drift against."));
        return;
    }
    QDate start = ui->driftStartDate->date();
    QDate end   = ui->driftEndDate->date();
    if (!start.isValid() || !end.isValid() || start >= end) {
        ui->driftStatusLabel->setText(tr("Invalid date range."));
        return;
    }

    DeviceTimeCorrectionRepository repo;

    // Find the most recent active drift row on the CPAP device covering [start, end].
    m_hasExistingModel   = false;
    m_existingModelRowId = -1;
    for (const auto& row : repo.findActive(mach->getDatabaseId())) {
        if (row.type != "drift") continue;
        QDate rowFrom = QDate::fromString(row.dateFrom, Qt::ISODate);
        QDate rowTo   = row.dateTo.isEmpty() ? QDate(9999, 12, 31)
                                             : QDate::fromString(row.dateTo, Qt::ISODate);
        if (rowFrom > end || rowTo < start) continue;
        if (!m_hasExistingModel || rowFrom > m_existingModelFrom) {
            m_hasExistingModel   = true;
            m_existingModelC0Ms  = double(row.c0Ms);
            m_existingModelSlope = row.c1 - 1.0;
            m_existingModelRowId = row.id;
            m_existingModelFrom  = rowFrom;
        }
    }

    // The fitting data is the reference device's nightly offset entries — each entry
    // is the measured CPAP-to-reference offset for that night. When a CPAP drift model
    // is already active, add its value so the new fit targets total corrections.
    m_driftPoints.clear();
    QList<DriftPlotWidget::Point> plotPts;
    for (const auto& r : repo.findManualOffsetRows(ref->getDatabaseId())) {
        QDate d = QDate::fromString(r.dateFrom, Qt::ISODate);
        if (d < start || d > end) continue;
        double tNoonMs = QDateTime(d, QTime(12, 0, 0), Qt::UTC).toMSecsSinceEpoch();
        double plotVal = double(r.offsetMs);
        if (m_hasExistingModel)
            plotVal += m_existingModelC0Ms + m_existingModelSlope * tNoonMs;
        m_driftPoints.append({d, plotVal, tNoonMs});
        plotPts.append({d, plotVal});
    }

    if (m_hasExistingModel)
        ui->driftPlot->setReferenceModel(m_existingModelC0Ms, m_existingModelSlope);
    else
        ui->driftPlot->clearReferenceModel();

    ui->driftPlot->setDateRange(start, end);
    ui->driftPlot->setData(plotPts);
    ui->driftPlot->clearRefPoints();

    m_fitValid = false;
    ui->btnFitDrift->setEnabled(m_driftPoints.size() >= 3);
    ui->btnUseDrift->setEnabled(false);

    QString refName = ref->brand() + " " + ref->model();
    if (refName.trimmed().isEmpty()) refName = ref->loaderName();

    if (m_driftPoints.isEmpty() && m_hasExistingModel) {
        ui->driftStatusLabel->setText(
            tr("Drift model active since %1 — no new %2 offset entries in range. "
               "Add offset entries for the reference device, then reload.")
            .arg(m_existingModelFrom.toString(Qt::ISODate))
            .arg(refName));
    } else if (m_driftPoints.isEmpty()) {
        ui->driftStatusLabel->setText(
            tr("No offset entries found for %1 in the selected range.").arg(refName));
    } else if (m_hasExistingModel) {
        ui->driftStatusLabel->setText(
            tr("Drift model active since %1. Showing total %2 corrections "
               "(existing model + residuals). %3 points loaded. New fit replaces "
               "the existing model from %4 onward.")
            .arg(m_existingModelFrom.toString(Qt::ISODate))
            .arg(refName)
            .arg(m_driftPoints.size())
            .arg(start.toString(Qt::ISODate)));
    } else {
        ui->driftStatusLabel->setText(
            tr("%1 %2 offset entries loaded. Click Fit Model to compute drift rate.")
            .arg(m_driftPoints.size())
            .arg(refName));
    }
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
    if (r2 < 0.5)
        status += tr(" — poor fit, Use Model disabled");
    else if (r2 < 0.8)
        status += tr(" — moderate fit");
    if (m_hasExistingModel)
        status += tr(" — will replace model active since %1").arg(m_existingModelFrom.toString(Qt::ISODate));
    ui->driftStatusLabel->setText(status);
}

void DriftAnalysisDialog::onUseDrift()
{
    if (!m_fitValid) return;
    Machine* mach = currentDutMachine();
    if (!mach || mach->getDatabaseId() <= 0) return;
    Machine* ref = currentRefMachine();
    if (!ref || ref->getDatabaseId() <= 0) return;

    QDate fitStart = ui->driftStartDate->date();
    QDate fitEnd   = ui->driftEndDate->date();

    DeviceTimeCorrectionRepository repo;

    // Close the existing CPAP drift row the day before the new fit starts
    if (m_hasExistingModel && m_existingModelRowId >= 0) {
        QString closedTo = fitStart.addDays(-1).toString(Qt::ISODate);
        repo.updateDateTo(m_existingModelRowId, closedTo);
    }

    // Mark the reference device's constant entries in the fit range as undone — they
    // are absorbed into the CPAP drift model and are no longer needed for correction.
    for (const auto& r : repo.findManualOffsetRows(ref->getDatabaseId())) {
        QDate d = QDate::fromString(r.dateFrom, Qt::ISODate);
        if (d >= fitStart && d <= fitEnd) repo.markUndone(r.id);
    }

    // Write new open-ended drift row on the CPAP (scenario b: extends forward indefinitely).
    // Values are stored un-negated (positive = CPAP is ahead by that many ms).
    // correctionMs() subtracts drift rows, so the CPAP is shifted backward to align.
    DeviceTimeCorrectionData modelRow;
    modelRow.machineId = mach->getDatabaseId();
    modelRow.dateFrom  = fitStart.toString(Qt::ISODate);
    modelRow.dateTo    = "";   // open-ended
    modelRow.type      = "drift";
    modelRow.c0Ms      = qint64(m_fitC0Ms);
    modelRow.c1        = m_fitSlope + 1.0;
    modelRow.reason    = tr("Fitted drift model");

    if (repo.create(modelRow) < 0) {
        QMessageBox::warning(this, tr("Drift Analysis"), tr("Failed to save drift model."));
        return;
    }

    rebuildMachine(mach);

    QString status;
    if (m_hasExistingModel) {
        status = tr("CPAP drift model refined. Prior model closed %1; new model active from %2 onward.")
                 .arg(fitStart.addDays(-1).toString(Qt::ISODate))
                 .arg(fitStart.toString(Qt::ISODate));
    } else {
        status = tr("CPAP drift model committed. Reference device entries in fit range replaced.");
    }

    resetPlotState();
    ui->driftStatusLabel->setText(status);

    emit correctionsChanged();
}
