/* Device Time Correction Dialog Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "devicetimecorrectiondialog.h"
#include "ui_devicetimecorrectiondialog.h"
#include "SleepLib/profiles.h"
#include <QMessageBox>
#include <QHeaderView>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QtMath>

static const qint64 kLargeOffsetThresholdMs = 15LL * 60 * 1000;

static const QStringList kTypeKeys = { "offset", "travel", "dst", "timezone", "reset" };

DeviceTimeCorrectionDialog::DeviceTimeCorrectionDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DeviceTimeCorrectionDialog)
{
    ui->setupUi(this);

    connect(ui->deviceSidebar, &QTreeWidget::currentItemChanged,
            this, &DeviceTimeCorrectionDialog::onDeviceChanged);
    connect(ui->btnMinus1h,       &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onNudgeMinus1h);
    connect(ui->btnMinus1m,       &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onNudgeMinus1m);
    connect(ui->btnMinus1s,       &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onNudgeMinus1s);
    connect(ui->btnPlus1s,        &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onNudgePlus1s);
    connect(ui->btnPlus1m,        &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onNudgePlus1m);
    connect(ui->btnPlus1h,        &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onNudgePlus1h);
    connect(ui->btnResetToZero,    &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onResetToZero);
    connect(ui->btnSaveCorrection, &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onSaveStaged);
    connect(ui->btnDiscardChanges, &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onDiscardStaged);
    connect(ui->btnApplyLastNight, &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onApplyLastNight);
    connect(ui->btnDeleteRow,      &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onDeleteRow);
    connect(ui->correctionTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DeviceTimeCorrectionDialog::refreshCurrentOffset);
    connect(ui->advEndDateCheck, &QCheckBox::toggled, this, [this](bool noEnd) {
        ui->advEndDate->setEnabled(!noEnd);
        if (noEnd)
            ui->advEndDate->setDate(QDate(2099, 12, 31));
    });

    ui->historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->historyTable->horizontalHeader()->setStretchLastSection(true);
}

DeviceTimeCorrectionDialog::~DeviceTimeCorrectionDialog()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void DeviceTimeCorrectionDialog::setDate(const QDate& date)
{
    if (m_hasStagedChange)
        clearStagedAndRevert();

    m_date = date;
    ui->activeDateLabel->setText(date.isValid() ? date.toString("yyyy-MM-dd") : tr("—"));

    if (date.isValid()) {
        ui->advStartDate->setDate(date);
        ui->advEndDate->setDate(date);
    }

    populateSidebar();
    Machine* mach = currentMachine();
    refreshTitleArea(mach);
    refreshCurrentOffset();
    refreshHistory();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void DeviceTimeCorrectionDialog::populateSidebar()
{
    if (!p_profile) return;

    QTreeWidget* tree = ui->deviceSidebar;
    Machine* prevMachine = currentMachine();

    tree->blockSignals(true);
    tree->clear();

    auto* refHeader = new QTreeWidgetItem(tree, QStringList{tr("Reference")});
    refHeader->setFlags(Qt::ItemIsEnabled);
    QFont hf = refHeader->font(0);
    hf.setBold(true);
    refHeader->setFont(0, hf);

    auto* otherHeader = new QTreeWidgetItem(tree, QStringList{tr("Other Devices")});
    otherHeader->setFlags(Qt::ItemIsEnabled);
    otherHeader->setFont(0, hf);

    QTreeWidgetItem* firstCpap  = nullptr;
    QTreeWidgetItem* firstOther = nullptr;
    QTreeWidgetItem* prevItem   = nullptr;

    for (Machine* mach : p_profile->GetMachines()) {
        if (!Machine::isCorrectableType(mach->type())) continue;
        QString label = mach->brand() + " " + mach->model();
        if (label.trimmed().isEmpty()) label = mach->loaderName();
        label += " (" + mach->serial() + ")";

        QTreeWidgetItem* item;
        if (mach->type() == MT_CPAP) {
            item = new QTreeWidgetItem(refHeader, QStringList{label});
            if (!firstCpap) firstCpap = item;
        } else {
            item = new QTreeWidgetItem(otherHeader, QStringList{label});
            if (!firstOther) firstOther = item;
        }
        item->setData(0, Qt::UserRole, QVariant::fromValue(reinterpret_cast<quintptr>(mach)));
        if (mach == prevMachine) prevItem = item;
    }

    tree->expandAll();
    tree->blockSignals(false);

    if (prevItem)
        tree->setCurrentItem(prevItem);
    else if (firstCpap)
        tree->setCurrentItem(firstCpap);
    else if (firstOther)
        tree->setCurrentItem(firstOther);
}

Machine* DeviceTimeCorrectionDialog::currentMachine() const
{
    QTreeWidgetItem* item = ui->deviceSidebar->currentItem();
    if (!item) return nullptr;
    QVariant v = item->data(0, Qt::UserRole);
    if (!v.isValid()) return nullptr;
    return reinterpret_cast<Machine*>(v.value<quintptr>());
}

void DeviceTimeCorrectionDialog::refreshTitleArea(Machine* mach)
{
    if (!mach) {
        ui->deviceNameLabel->setText(tr("—"));
        return;
    }
    QString name = mach->brand() + " " + mach->model();
    if (name.trimmed().isEmpty()) name = mach->loaderName();
    ui->deviceNameLabel->setText(name);
}

QString DeviceTimeCorrectionDialog::currentTypeName() const
{
    return kTypeKeys.value(ui->correctionTypeCombo->currentIndex(), "offset");
}

QString DeviceTimeCorrectionDialog::formatOffset(qint64 ms) const
{
    bool neg = ms < 0;
    qint64 abs = neg ? -ms : ms;
    qint64 s   = abs / 1000;
    return QString("%1%2:%3:%4")
        .arg(neg ? "-" : "+")
        .arg(s / 3600)
        .arg((s / 60) % 60, 2, 10, QChar('0'))
        .arg(s % 60,         2, 10, QChar('0'));
}

void DeviceTimeCorrectionDialog::rebuildMachine(Machine* mach)
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

void DeviceTimeCorrectionDialog::commitAndRefresh(Machine* mach)
{
    rebuildMachine(mach);
    refreshCurrentOffset();
    refreshHistory();
    emit correctionsChanged();
}

// ---------------------------------------------------------------------------
// Corrections panel
// ---------------------------------------------------------------------------

void DeviceTimeCorrectionDialog::onDeviceChanged(QTreeWidgetItem* current, QTreeWidgetItem*)
{
    if (!current || !current->data(0, Qt::UserRole).isValid()) return;
    if (m_hasStagedChange) clearStagedAndRevert();
    Machine* mach = currentMachine();
    refreshTitleArea(mach);
    refreshCurrentOffset();
    refreshHistory();
}

void DeviceTimeCorrectionDialog::refreshCurrentOffset()
{
    Machine* mach = currentMachine();
    if (!mach || !m_date.isValid()) {
        ui->currentOffsetValue->setText("N/A");
        ui->largeDriftWarning->setText("");
        return;
    }
    qint64 total = mach->correctionMs(m_date);
    if (m_hasStagedChange) {
        ui->currentOffsetValue->setText(formatOffset(total) + " *");
        ui->currentOffsetValue->setStyleSheet("color: #cc6600; font-weight: bold;");
    } else {
        ui->currentOffsetValue->setText(formatOffset(total));
        ui->currentOffsetValue->setStyleSheet("font-weight: bold;");
    }
    bool isOffset = (ui->correctionTypeCombo->currentIndex() == 0);
    bool showWarning = isOffset && qAbs(total) > kLargeOffsetThresholdMs;
    ui->largeDriftWarning->setText(showWarning
        ? tr("Large offset — consider using a different correction type.") : "");
}

void DeviceTimeCorrectionDialog::refreshHistory()
{
    ui->historyTable->setRowCount(0);
    m_historyRows.clear();
    m_historyMachines.clear();
    ui->historyLabel->setText(m_date.isValid()
        ? tr("Corrections on %1:").arg(m_date.toString("yyyy-MM-dd"))
        : tr("Active corrections for this date:"));
    if (!p_profile || !m_date.isValid()) return;

    DeviceTimeCorrectionRepository repo;
    for (Machine* mach : p_profile->GetMachines()) {
        if (!Machine::isCorrectableType(mach->type()) || mach->getDatabaseId() <= 0) continue;
        for (const auto& r : repo.findActive(mach->getDatabaseId())) {
            QDate from = QDate::fromString(r.dateFrom, Qt::ISODate);
            QDate to   = r.dateTo.isEmpty() ? QDate(9999,12,31) : QDate::fromString(r.dateTo, Qt::ISODate);
            if (m_date >= from && m_date <= to) {
                m_historyRows.append(r);
                m_historyMachines.append(mach);
            }
        }
    }

    ui->historyTable->setRowCount(m_historyRows.size());
    for (int i = 0; i < m_historyRows.size(); ++i) {
        const auto& r = m_historyRows[i];
        Machine* mach = m_historyMachines[i];

        QString deviceName = mach->brand() + " " + mach->model();
        if (deviceName.trimmed().isEmpty()) deviceName = mach->loaderName();

        QString range = r.dateFrom;
        if (!r.dateTo.isEmpty() && r.dateTo != r.dateFrom) range += " – " + r.dateTo;
        else if (r.dateTo.isEmpty())                        range += " – open";

        QString offsetStr = (r.c1 != 0.0)
            ? QString("model (c1=%1)").arg(r.c1, 0, 'g', 4)
            : formatOffset(r.offsetMs);

        ui->historyTable->setItem(i, 0, new QTableWidgetItem(deviceName));
        ui->historyTable->setItem(i, 1, new QTableWidgetItem(range));
        ui->historyTable->setItem(i, 2, new QTableWidgetItem(r.type));
        ui->historyTable->setItem(i, 3, new QTableWidgetItem(offsetStr));
        ui->historyTable->setItem(i, 4, new QTableWidgetItem(r.appliedAt.left(10)));
        ui->historyTable->item(i, 0)->setData(Qt::UserRole, i);

        QColor bg;
        if      (r.type == "offset")   bg = QColor(255, 255, 210);
        else if (r.type == "timezone") bg = QColor(210, 225, 255);
        else if (r.type == "travel")   bg = QColor(210, 255, 225);
        else if (r.type == "dst")      bg = QColor(255, 225, 210);
        else if (r.type == "reset")    bg = QColor(240, 210, 255);
        if (bg.isValid())
            for (int c = 0; c < 5; ++c)
                if (ui->historyTable->item(i, c))
                    ui->historyTable->item(i, c)->setBackground(bg);
    }
}

void DeviceTimeCorrectionDialog::effectiveDateRange(QString& dateFrom, QString& dateTo) const
{
    QString type = currentTypeName();
    bool advanced = ui->advancedGroup->isChecked();

    if (type == "timezone") {
        dateFrom = m_date.toString(Qt::ISODate);
        dateTo   = "";
        return;
    }

    if (advanced) {
        dateFrom = ui->advStartDate->date().toString(Qt::ISODate);
        dateTo   = ui->advEndDateCheck->isChecked()
                   ? "2099-12-31"
                   : ui->advEndDate->date().toString(Qt::ISODate);
    } else {
        dateFrom = m_date.toString(Qt::ISODate);
        dateTo   = m_date.toString(Qt::ISODate);
    }
}

// ---------------------------------------------------------------------------
// Staged preview helpers
// ---------------------------------------------------------------------------

void DeviceTimeCorrectionDialog::setStagedButtonsEnabled(bool enabled)
{
    ui->btnSaveCorrection->setEnabled(enabled);
    ui->btnDiscardChanges->setEnabled(enabled);
}

void DeviceTimeCorrectionDialog::previewStaged(Machine* mach)
{
    DeviceTimeCorrectionRepository repo;
    QList<DeviceTimeCorrectionData> dbRows = repo.findActive(mach->getDatabaseId());

    QList<TimeCorrectionRow> rows;
    rows.reserve(dbRows.size() + 1);
    for (const auto& d : dbRows) {
        if (d.type == m_staged.type && d.c1 == 0.0 &&
            d.dateFrom == m_staged.dateFrom &&
            (m_staged.dateTo.isEmpty() ? d.dateTo.isEmpty() : d.dateTo == m_staged.dateTo))
            continue;
        TimeCorrectionRow r;
        r.dateFrom = QDate::fromString(d.dateFrom, Qt::ISODate);
        r.dateTo   = d.dateTo.isEmpty() ? QDate() : QDate::fromString(d.dateTo, Qt::ISODate);
        r.offsetMs = d.offsetMs;
        r.c0Ms     = d.c0Ms;
        r.c1       = d.c1;
        rows.append(r);
    }

    if (m_staged.offsetMs != 0) {
        TimeCorrectionRow staged;
        staged.dateFrom = QDate::fromString(m_staged.dateFrom, Qt::ISODate);
        staged.dateTo   = m_staged.dateTo.isEmpty()
                          ? QDate()
                          : QDate::fromString(m_staged.dateTo, Qt::ISODate);
        staged.offsetMs = m_staged.offsetMs;
        rows.append(staged);
    }

    mach->rebuildCorrections(rows);
    refreshCurrentOffset();
    refreshHistory();
    emit correctionsChanged();
}

void DeviceTimeCorrectionDialog::clearStagedAndRevert()
{
    Machine* mach = currentMachine();
    m_hasStagedChange = false;
    m_staged = {};
    setStagedButtonsEnabled(false);
    if (mach) {
        rebuildMachine(mach);
        emit correctionsChanged();
    }
}

static void closeOpenTimezoneRows(DeviceTimeCorrectionRepository& repo, qint64 machineId,
                                  const QString& newFrom)
{
    QDate newDate = QDate::fromString(newFrom, Qt::ISODate);
    for (const auto& r : repo.findActive(machineId)) {
        if (r.type != "timezone") continue;
        if (!r.dateTo.isEmpty()) continue;
        QDate from = QDate::fromString(r.dateFrom, Qt::ISODate);
        if (from >= newDate)
            repo.markUndone(r.id);
        else
            repo.updateDateTo(r.id, newDate.addDays(-1).toString(Qt::ISODate));
    }
}

void DeviceTimeCorrectionDialog::onSaveStaged()
{
    if (!m_hasStagedChange) return;
    Machine* mach = currentMachine();
    if (!mach || mach->getDatabaseId() <= 0) return;

    DeviceTimeCorrectionRepository repo;

    if (m_staged.type == "timezone")
        closeOpenTimezoneRows(repo, mach->getDatabaseId(), m_staged.dateFrom);

    repo.upsertTyped(mach->getDatabaseId(),
                     m_staged.dateFrom, m_staged.dateTo,
                     m_staged.type, m_staged.offsetMs);

    m_hasStagedChange = false;
    m_staged = {};
    setStagedButtonsEnabled(false);
    commitAndRefresh(mach);
}

void DeviceTimeCorrectionDialog::onDiscardStaged()
{
    clearStagedAndRevert();
    refreshCurrentOffset();
    refreshHistory();
}

void DeviceTimeCorrectionDialog::applyNudge(qint64 deltaMs)
{
    Machine* mach = currentMachine();
    if (!mach || !m_date.isValid() || mach->getDatabaseId() <= 0) return;

    QString type = currentTypeName();
    QString dateFrom, dateTo;
    effectiveDateRange(dateFrom, dateTo);

    if (!m_hasStagedChange) {
        m_staged.machineId = mach->getDatabaseId();
        m_staged.dateFrom  = dateFrom;
        m_staged.dateTo    = dateTo;
        m_staged.type      = type;
        m_staged.offsetMs  = 0;

        DeviceTimeCorrectionRepository repo;
        for (const auto& r : repo.findActive(mach->getDatabaseId())) {
            if (r.type != type || r.c1 != 0.0 || r.dateFrom != dateFrom) continue;
            bool sameTo = dateTo.isEmpty() ? r.dateTo.isEmpty() : (r.dateTo == dateTo);
            if (sameTo) { m_staged.offsetMs = r.offsetMs; break; }
        }
        m_hasStagedChange = true;
        setStagedButtonsEnabled(true);
    }

    m_staged.offsetMs += deltaMs;
    previewStaged(mach);
}

void DeviceTimeCorrectionDialog::onNudgeMinus1h() { applyNudge(-3600000LL); }
void DeviceTimeCorrectionDialog::onNudgeMinus1m() { applyNudge(-60000LL);   }
void DeviceTimeCorrectionDialog::onNudgeMinus1s() { applyNudge(-1000LL);    }
void DeviceTimeCorrectionDialog::onNudgePlus1s()  { applyNudge(1000LL);     }
void DeviceTimeCorrectionDialog::onNudgePlus1m()  { applyNudge(60000LL);    }
void DeviceTimeCorrectionDialog::onNudgePlus1h()  { applyNudge(3600000LL);  }

void DeviceTimeCorrectionDialog::onResetToZero()
{
    Machine* mach = currentMachine();
    if (!mach || !m_date.isValid() || mach->getDatabaseId() <= 0) return;

    QString type = currentTypeName();
    QString dateFrom, dateTo;
    effectiveDateRange(dateFrom, dateTo);

    m_staged.machineId = mach->getDatabaseId();
    m_staged.dateFrom  = dateFrom;
    m_staged.dateTo    = dateTo;
    m_staged.type      = type;
    m_staged.offsetMs  = 0;
    m_hasStagedChange  = true;
    setStagedButtonsEnabled(true);
    previewStaged(mach);
}

void DeviceTimeCorrectionDialog::onApplyLastNight()
{
    Machine* mach = currentMachine();
    if (!mach || !m_date.isValid() || mach->getDatabaseId() <= 0) return;

    QString prevStr  = m_date.addDays(-1).toString(Qt::ISODate);
    QString todayStr = m_date.toString(Qt::ISODate);

    DeviceTimeCorrectionRepository repo;
    QList<DeviceTimeCorrectionData> prevRows;
    for (const auto& r : repo.findActive(mach->getDatabaseId())) {
        if (r.dateFrom == prevStr && r.dateTo == prevStr && r.c1 == 0.0)
            prevRows.append(r);
    }

    if (prevRows.isEmpty()) {
        QMessageBox::information(this, tr("Apply Last Offset"),
            tr("No single-night corrections found for %1.").arg(prevStr));
        return;
    }

    for (const auto& r : prevRows)
        repo.upsertTyped(mach->getDatabaseId(), todayStr, todayStr, r.type, r.offsetMs);

    commitAndRefresh(mach);
}

DeviceTimeCorrectionDialog::DeleteChoice
DeviceTimeCorrectionDialog::showDeleteRangeDialog(const DeviceTimeCorrectionData& row)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Delete Correction"));
    auto* layout = new QVBoxLayout(&dlg);

    QString toStr = row.dateTo.isEmpty() ? tr("open-ended") : row.dateTo;
    layout->addWidget(new QLabel(
        tr("Delete correction spanning %1 – %2").arg(row.dateFrom, toStr)));

    auto* rbClose = new QRadioButton(
        tr("Close from %1 onward (ends %2, keeps prior dates)")
        .arg(m_date.toString(Qt::ISODate),
             m_date.addDays(-1).toString(Qt::ISODate)));
    auto* rbSplit = new QRadioButton(
        tr("Remove only %1 (splits into two rows)").arg(m_date.toString(Qt::ISODate)));
    auto* rbAll   = new QRadioButton(tr("Remove entirely (marks all dates undone)"));

    rbClose->setChecked(true);
    layout->addWidget(rbClose);
    layout->addWidget(rbSplit);
    layout->addWidget(rbAll);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return DeleteCancelled;
    if (rbSplit->isChecked()) return SplitOut;
    if (rbAll->isChecked())   return RemoveEntirely;
    return CloseForward;
}

void DeviceTimeCorrectionDialog::splitOutDate(const DeviceTimeCorrectionData& row)
{
    DeviceTimeCorrectionRepository repo;
    QDate from = QDate::fromString(row.dateFrom, Qt::ISODate);
    QDate to   = row.dateTo.isEmpty() ? QDate() : QDate::fromString(row.dateTo, Qt::ISODate);

    repo.markUndone(row.id);

    if (from < m_date) {
        DeviceTimeCorrectionData left = row;
        left.id     = 0;
        left.dateTo = m_date.addDays(-1).toString(Qt::ISODate);
        repo.create(left);
    }

    QDate afterDate = m_date.addDays(1);
    if (!to.isValid() || afterDate <= to) {
        DeviceTimeCorrectionData right = row;
        right.id       = 0;
        right.dateFrom = afterDate.toString(Qt::ISODate);
        repo.create(right);
    }
}

void DeviceTimeCorrectionDialog::onDeleteRow()
{
    int tableRow = ui->historyTable->currentRow();
    if (tableRow < 0 || tableRow >= m_historyRows.size()) return;
    Machine* mach = m_historyMachines[tableRow];
    if (!mach || mach->getDatabaseId() <= 0) return;

    const DeviceTimeCorrectionData& row = m_historyRows[tableRow];
    DeviceTimeCorrectionRepository repo;

    if (row.dateFrom == row.dateTo) {
        if (!repo.markUndone(row.id)) {
            QMessageBox::warning(this, tr("Time Corrections"), tr("Failed to delete correction."));
            return;
        }
        commitAndRefresh(mach);
        return;
    }

    DeleteChoice choice = showDeleteRangeDialog(row);
    switch (choice) {
    case CloseForward:
        if (row.dateFrom == m_date.toString(Qt::ISODate))
            repo.markUndone(row.id);
        else
            repo.updateDateTo(row.id, m_date.addDays(-1).toString(Qt::ISODate));
        break;
    case SplitOut:
        splitOutDate(row);
        break;
    case RemoveEntirely:
        repo.markUndone(row.id);
        break;
    case DeleteCancelled:
        return;
    }
    commitAndRefresh(mach);
}

void DeviceTimeCorrectionDialog::closeEvent(QCloseEvent* event)
{
    if (m_hasStagedChange)
        clearStagedAndRevert();
    QDialog::closeEvent(event);
}
