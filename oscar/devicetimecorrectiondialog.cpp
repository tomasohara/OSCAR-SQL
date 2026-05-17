/* Device Time Correction Dialog Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "devicetimecorrectiondialog.h"
#include "ui_devicetimecorrectiondialog.h"
#include "borrowingtimeedit.h"
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
    connect(ui->btnResetToZero,    &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onResetToZero);
    connect(ui->btnNewCorrection,  &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onNewCorrection);
    connect(ui->btnSaveCorrection, &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onSaveStaged);
    connect(ui->btnDiscardChanges, &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onDiscardStaged);
    connect(ui->btnApplyLastNight, &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onApplyLastNight);
    connect(ui->btnDeleteRow,      &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onDeleteRow);
    connect(ui->btnToggleHistory,  &QPushButton::clicked, this, &DeviceTimeCorrectionDialog::onToggleHistory);
    connect(ui->historyTable, &QTableWidget::itemSelectionChanged,
            this, &DeviceTimeCorrectionDialog::onHistoryRowSelected);
    connect(ui->correctionTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DeviceTimeCorrectionDialog::refreshCurrentOffset);
    connect(ui->correctionTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DeviceTimeCorrectionDialog::onAnyControlChanged);
    connect(ui->offsetTimeEdit, &QTimeEdit::timeChanged,
            this, &DeviceTimeCorrectionDialog::onOffsetTimeChanged);
    connect(ui->btnOffsetSign,  &QPushButton::toggled,
            this, &DeviceTimeCorrectionDialog::onOffsetSignToggled);
    connect(ui->offsetTimeEdit, &BorrowingTimeEdit::steppedPastZero, this, [this](int overshootSecs) {
        bool flippedTo = !ui->btnOffsetSign->isChecked();
        {
            QSignalBlocker b(ui->btnOffsetSign);
            ui->btnOffsetSign->setChecked(flippedTo);
            ui->btnOffsetSign->setText(flippedTo ? "−" : "+");
        }
        ui->offsetTimeEdit->setReversed(flippedTo);
        // timeChanged fires here and onOffsetTimeChanged reads the already-flipped sign
        ui->offsetTimeEdit->setTime(QTime(overshootSecs / 3600, (overshootSecs % 3600) / 60, overshootSecs % 60));
    });
    connect(ui->chkDateRange,   &QCheckBox::toggled, ui->dateRangeWidget, &QWidget::setVisible);
    connect(ui->chkDateRange,   &QCheckBox::toggled, this, &DeviceTimeCorrectionDialog::onAnyControlChanged);
    connect(ui->advStartDate,    &QDateEdit::dateChanged,   this, &DeviceTimeCorrectionDialog::onAnyControlChanged);
    connect(ui->advEndDate,      &QDateEdit::dateChanged,   this, &DeviceTimeCorrectionDialog::onAnyControlChanged);
    connect(ui->advEndDateCheck, &QCheckBox::toggled,       this, &DeviceTimeCorrectionDialog::onAnyControlChanged);
    connect(ui->advEndDateCheck, &QCheckBox::toggled, this, [this](bool noEnd) {
        if (noEnd) ui->advEndDate->setDate(QDate(2099, 12, 31));
    });

    ui->offsetTimeEdit->setSelectedSection(QDateTimeEdit::SecondSection);

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
        if (m_date.isValid() && !mach->day.contains(m_date)) continue;
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
    m_staged = {};
    m_hasStagedChange = false;
    Machine* mach = currentMachine();
    refreshTitleArea(mach);
    refreshHistory();   // resetToNewMode inside calls refreshCurrentOffset
}

void DeviceTimeCorrectionDialog::refreshCurrentOffset()
{
    Machine* mach = currentMachine();
    if (!mach || !m_date.isValid()) {
        ui->largeDriftWarning->setText("");
        return;
    }
    // Show the row's offset when one is loaded (viewing or staging).
    // In new-correction mode show 0 — the historyTable lists existing corrections.
    qint64 displayMs = (m_staged.machineId != 0) ? m_staged.offsetMs : 0;
    bool negative = (displayMs < 0);

    // QSignalBlockers on both to prevent re-entrant loops:
    // setChecked() → onOffsetSignToggled() → onOffsetTimeChanged() → previewStaged() → here
    // setTime()    → onOffsetTimeChanged() → previewStaged() → here
    {
        QSignalBlocker bs(ui->btnOffsetSign);
        ui->btnOffsetSign->setChecked(negative);
        ui->btnOffsetSign->setText(negative ? "−" : "+");
    }
    {
        QSignalBlocker bt(ui->offsetTimeEdit);
        ui->offsetTimeEdit->setTime(QTime(0, 0, 0).addMSecs(qAbs(displayMs)));
    }
    ui->offsetTimeEdit->setReversed(negative);

    bool isOffset = (ui->correctionTypeCombo->currentIndex() == 0);
    bool showWarning = isOffset && qAbs(displayMs) > kLargeOffsetThresholdMs;
    ui->largeDriftWarning->setText(showWarning
        ? tr("Large offset — consider using a different correction type.") : "");
}

void DeviceTimeCorrectionDialog::refreshHistory()
{
    // Block signals for the entire population so Qt's auto-selection of row 0
    // (fired by setRowCount when the current index was invalid) cannot trigger
    // onHistoryRowSelected before autoPopulateForCurrentDevice runs.
    ui->historyTable->blockSignals(true);
    ui->historyTable->setRowCount(0);
    m_historyRows.clear();
    m_historyMachines.clear();
    if (!p_profile || !m_date.isValid()) {
        ui->historyTable->blockSignals(false);
        return;
    }

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

    ui->historyTable->blockSignals(false);
    if (!m_hasStagedChange)
        resetToNewMode();
}

void DeviceTimeCorrectionDialog::effectiveDateRange(QString& dateFrom, QString& dateTo) const
{
    QString type = currentTypeName();
    bool advanced = ui->chkDateRange->isChecked();

    if (type == "timezone") {
        if (advanced) {
            dateFrom = ui->advStartDate->date().toString(Qt::ISODate);
            // Keep dateTo="" (NULL in DB) when open-ended so closeOpenTimezoneRows
            // can still find this row via its dateTo.isEmpty() guard.
            dateTo   = ui->advEndDateCheck->isChecked()
                       ? ""
                       : ui->advEndDate->date().toString(Qt::ISODate);
        } else {
            dateFrom = m_date.toString(Qt::ISODate);
            dateTo   = "";
        }
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

void DeviceTimeCorrectionDialog::updateControlStates()
{
    ui->btnSaveCorrection->setEnabled(m_hasStagedChange);
    ui->btnDiscardChanges->setEnabled(m_hasStagedChange);

    bool rangeExpanded = ui->chkDateRange->isChecked();
    ui->advEndDate->setEnabled(rangeExpanded && !ui->advEndDateCheck->isChecked());

    ui->btnDeleteRow->setEnabled(ui->historyTable->currentRow() >= 0);
}

void DeviceTimeCorrectionDialog::previewStaged(Machine* mach)
{
    DeviceTimeCorrectionRepository repo;
    QList<DeviceTimeCorrectionData> dbRows = repo.findActive(mach->getDatabaseId());

    QString previewFrom, previewTo;
    effectiveDateRange(previewFrom, previewTo);
    QString previewType = currentTypeName();

    QList<TimeCorrectionRow> rows;
    rows.reserve(dbRows.size() + 1);
    for (const auto& d : dbRows) {
        bool exclude = (m_staged.id != 0)
            ? (d.id == m_staged.id)
            : (d.type == previewType && d.c1 == 0.0 &&
               d.dateFrom == previewFrom &&
               (previewTo.isEmpty() ? d.dateTo.isEmpty() : d.dateTo == previewTo));
        if (exclude) continue;
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
        staged.dateFrom = QDate::fromString(previewFrom, Qt::ISODate);
        staged.dateTo   = previewTo.isEmpty() ? QDate() : QDate::fromString(previewTo, Qt::ISODate);
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
    resetToNewMode();
    Machine* mach = currentMachine();
    if (mach) {
        rebuildMachine(mach);
        emit correctionsChanged();
    }
}

void DeviceTimeCorrectionDialog::resetToNewMode()
{
    m_hasStagedChange = false;
    m_staged = {};

    {
        QSignalBlocker b(ui->historyTable);
        ui->historyTable->clearSelection();
        ui->historyTable->setCurrentItem(nullptr);
    }

    {
        QSignalBlocker b1(ui->correctionTypeCombo);
        QSignalBlocker b2(ui->chkDateRange);
        QSignalBlocker b3(ui->advStartDate);
        QSignalBlocker b4(ui->advEndDate);
        QSignalBlocker b5(ui->advEndDateCheck);

        ui->correctionTypeCombo->setCurrentIndex(0);
        ui->chkDateRange->setChecked(false);
        if (m_date.isValid()) {
            ui->advStartDate->setDate(m_date);
            ui->advEndDate->setDate(m_date);
        }
        ui->advEndDateCheck->setChecked(false);
    }
    // toggled was suppressed by the blocker — drive visibility explicitly
    ui->dateRangeWidget->setVisible(false);

    refreshCurrentOffset();
    refreshModeLabel();
    updateControlStates();
}

void DeviceTimeCorrectionDialog::refreshModeLabel()
{
    if (m_staged.id == 0) {
        ui->modeLabel->setText(tr("New correction"));
        ui->modeLabel->setStyleSheet("color: #2a7a2a; font-style: italic;");
    } else {
        QString toStr = m_staged.dateTo.isEmpty() ? tr("open") : m_staged.dateTo;
        QString desc = QString("%1  %2 – %3").arg(m_staged.type, m_staged.dateFrom, toStr);
        ui->modeLabel->setText(tr("Editing: %1").arg(desc));
        ui->modeLabel->setStyleSheet("color: #b85c00; font-style: italic;");
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

    // Always read date range and type from the current UI state at save time.
    // The staged row may have been created before the user set the date range.
    QString dateFrom, dateTo;
    effectiveDateRange(dateFrom, dateTo);
    QString type = currentTypeName();

    DeviceTimeCorrectionRepository repo;

    if (type == "timezone")
        closeOpenTimezoneRows(repo, mach->getDatabaseId(), dateFrom);

    if (m_staged.id != 0)
        repo.markUndone(m_staged.id);

    repo.upsertTyped(mach->getDatabaseId(), dateFrom, dateTo, type, m_staged.offsetMs);

    m_hasStagedChange = false;
    m_staged = {};
    updateControlStates();
    commitAndRefresh(mach);
}

void DeviceTimeCorrectionDialog::onDiscardStaged()
{
    clearStagedAndRevert();
    refreshHistory();
}

void DeviceTimeCorrectionDialog::onNewCorrection()
{
    clearStagedAndRevert();
    resetToNewMode();
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
        if (m_staged.id == 0) {
            // New-entry mode: initialize staged position and seed offset from any
            // existing row at the same position.
            m_staged.dateFrom = dateFrom;
            m_staged.dateTo   = dateTo;
            m_staged.type     = type;
            m_staged.offsetMs = 0;
            DeviceTimeCorrectionRepository repo;
            for (const auto& r : repo.findActive(mach->getDatabaseId())) {
                if (r.type != type || r.c1 != 0.0 || r.dateFrom != dateFrom) continue;
                bool sameTo = dateTo.isEmpty() ? r.dateTo.isEmpty() : (r.dateTo == dateTo);
                if (sameTo) { m_staged.offsetMs = r.offsetMs; break; }
            }
        }
        // Edit mode (id != 0): dateFrom/dateTo/type/offsetMs already seeded by
        // populateControlsFromRow — preserve them.
        m_hasStagedChange = true;
        updateControlStates();
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

void DeviceTimeCorrectionDialog::onOffsetTimeChanged(const QTime &time)
{
    Machine* mach = currentMachine();
    if (!mach || !m_date.isValid() || mach->getDatabaseId() <= 0) return;

    qint64 absMs = static_cast<qint64>(QTime(0, 0, 0).msecsTo(time));
    bool negative = ui->btnOffsetSign->isChecked();

    if (!m_hasStagedChange) {
        m_staged.machineId = mach->getDatabaseId();
        if (m_staged.id == 0) {
            QString df, dt;
            effectiveDateRange(df, dt);
            m_staged.dateFrom = df;
            m_staged.dateTo   = dt;
            m_staged.type     = currentTypeName();
        }
        m_hasStagedChange = true;
        updateControlStates();
    }

    m_staged.offsetMs = negative ? -absMs : absMs;
    previewStaged(mach);
}

void DeviceTimeCorrectionDialog::onOffsetSignToggled(bool checked)
{
    ui->btnOffsetSign->setText(checked ? "−" : "+");
    ui->offsetTimeEdit->setReversed(checked);
    qint64 absMs = static_cast<qint64>(QTime(0, 0, 0).msecsTo(ui->offsetTimeEdit->time()));
    if (absMs == 0) return;
    onOffsetTimeChanged(ui->offsetTimeEdit->time());
}

void DeviceTimeCorrectionDialog::onToggleHistory()
{
    bool nowVisible = !ui->historyTable->isVisible();
    ui->historyTable->setVisible(nowVisible);
    ui->btnToggleHistory->setText(nowVisible ? "Corrections ▼" : "Corrections ▶");
}

void DeviceTimeCorrectionDialog::onResetToZero()
{
    Machine* mach = currentMachine();
    if (!mach || !m_date.isValid() || mach->getDatabaseId() <= 0) return;

    m_staged.machineId = mach->getDatabaseId();
    if (m_staged.id == 0) {
        QString dateFrom, dateTo;
        effectiveDateRange(dateFrom, dateTo);
        m_staged.dateFrom = dateFrom;
        m_staged.dateTo   = dateTo;
        m_staged.type     = currentTypeName();
    }
    m_staged.offsetMs = 0;
    m_hasStagedChange = true;
    updateControlStates();
    {
        QSignalBlocker b(ui->offsetTimeEdit);
        ui->offsetTimeEdit->setTime(QTime(0, 0, 0));
    }
    {
        QSignalBlocker b(ui->btnOffsetSign);
        ui->btnOffsetSign->setChecked(false);
        ui->btnOffsetSign->setText("+");
    }
    ui->offsetTimeEdit->setReversed(false);
    previewStaged(mach);
}

void DeviceTimeCorrectionDialog::onApplyLastNight()
{
    Machine* mach = currentMachine();
    if (!mach || !m_date.isValid() || mach->getDatabaseId() <= 0) return;

    QString todayStr = m_date.toString(Qt::ISODate);

    DeviceTimeCorrectionRepository repo;
    QList<DeviceTimeCorrectionData> allActive = repo.findActive(mach->getDatabaseId());

    QList<DeviceTimeCorrectionData> sourceRows;
    for (int d = 1; d <= 7 && sourceRows.isEmpty(); ++d) {
        QString dayStr = m_date.addDays(-d).toString(Qt::ISODate);
        for (const auto& r : allActive) {
            if (r.dateFrom == dayStr && r.dateTo == dayStr && r.c1 == 0.0)
                sourceRows.append(r);
        }
    }

    if (sourceRows.isEmpty()) {
        QMessageBox::information(this, tr("Apply Last Offset"),
            tr("No single-night corrections found in the past 7 days."));
        return;
    }

    for (const auto& r : sourceRows)
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

// ---------------------------------------------------------------------------
// Click-to-load helpers
// ---------------------------------------------------------------------------

void DeviceTimeCorrectionDialog::populateControlsFromRow(const DeviceTimeCorrectionData& row)
{
    // Seed m_staged so onAnyControlChanged and refreshCurrentOffset use this row's values.
    Machine* mach = currentMachine();
    if (mach && mach->getDatabaseId() > 0) {
        m_staged.id        = row.id;
        m_staged.machineId = mach->getDatabaseId();
        m_staged.dateFrom  = row.dateFrom;
        m_staged.dateTo    = row.dateTo;
        m_staged.type      = row.type;
        m_staged.offsetMs  = row.offsetMs;
    }

    {
        QSignalBlocker b1(ui->correctionTypeCombo);
        QSignalBlocker b2(ui->chkDateRange);
        QSignalBlocker b3(ui->advStartDate);
        QSignalBlocker b4(ui->advEndDate);
        QSignalBlocker b5(ui->advEndDateCheck);

        int typeIdx = kTypeKeys.indexOf(row.type);
        ui->correctionTypeCombo->setCurrentIndex(typeIdx < 0 ? 0 : typeIdx);

        QDate from = QDate::fromString(row.dateFrom, Qt::ISODate);
        QDate to   = row.dateTo.isEmpty() ? QDate() : QDate::fromString(row.dateTo, Qt::ISODate);
        bool isSingleNight = (row.dateFrom == row.dateTo);

        if (isSingleNight) {
            ui->chkDateRange->setChecked(false);
            ui->advStartDate->setDate(from);
            ui->advEndDate->setDate(from);
            ui->advEndDateCheck->setChecked(false);
        } else {
            ui->chkDateRange->setChecked(true);
            ui->advStartDate->setDate(from);
            if (row.dateTo.isEmpty()) {
                ui->advEndDateCheck->setChecked(true);
                ui->advEndDate->setDate(QDate(2099, 12, 31));
            } else {
                ui->advEndDateCheck->setChecked(false);
                ui->advEndDate->setDate(to);
            }
        }
    }
    // toggled was suppressed by the blocker — drive visibility explicitly
    ui->dateRangeWidget->setVisible(ui->chkDateRange->isChecked());

    refreshCurrentOffset();
    refreshModeLabel();
    updateControlStates();
}

void DeviceTimeCorrectionDialog::onAnyControlChanged()
{
    if (!m_hasStagedChange && m_staged.machineId != 0) {
        m_hasStagedChange = true;
        refreshCurrentOffset();
    }
    updateControlStates();
}

void DeviceTimeCorrectionDialog::onHistoryRowSelected()
{
    int tableRow = ui->historyTable->currentRow();
    if (tableRow < 0 || tableRow >= m_historyRows.size()) return;

    Machine* rowMachine = m_historyMachines[tableRow];
    Machine* curMachine = currentMachine();

    if (rowMachine != curMachine) {
        qint64 targetId = m_historyRows[tableRow].id;

        QTreeWidget* tree = ui->deviceSidebar;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* header = tree->topLevelItem(i);
            for (int j = 0; j < header->childCount(); ++j) {
                QTreeWidgetItem* child = header->child(j);
                QVariant v = child->data(0, Qt::UserRole);
                if (v.isValid() && reinterpret_cast<Machine*>(v.value<quintptr>()) == rowMachine) {
                    // Switch device; onDeviceChanged → refreshHistory → autoPopulateForCurrentDevice
                    // runs synchronously and selects the first row for the device.
                    tree->setCurrentItem(child);

                    // Now override that selection with the specific row that was clicked.
                    // Block itemSelectionChanged to avoid re-entering this slot.
                    for (int k = 0; k < m_historyRows.size(); ++k) {
                        if (m_historyRows[k].id == targetId) {
                            ui->historyTable->blockSignals(true);
                            ui->historyTable->setCurrentCell(k, 0);
                            ui->historyTable->blockSignals(false);
                            populateControlsFromRow(m_historyRows[k]);
                            refreshModeLabel();
                            break;
                        }
                    }
                    return;
                }
            }
        }
        return;
    }

    // Same device: discard any staged change, then load from this row
    if (m_hasStagedChange) {
        Machine* mach = currentMachine();
        resetToNewMode();
        if (mach) {
            rebuildMachine(mach);
            emit correctionsChanged();
        }
    }
    populateControlsFromRow(m_historyRows[tableRow]);
}

void DeviceTimeCorrectionDialog::closeEvent(QCloseEvent* event)
{
    if (m_hasStagedChange)
        clearStagedAndRevert();
    QDialog::closeEvent(event);
}
