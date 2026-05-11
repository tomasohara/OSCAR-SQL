/* Device Time Correction Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DEVICETIMECORRECTIONDIALOG_H
#define DEVICETIMECORRECTIONDIALOG_H

#include <QDialog>
#include <QDate>
#include <QTreeWidget>
#include <QCloseEvent>
#include "SleepLib/machine.h"
#include "database/device_time_correction_repository.h"

namespace Ui {
class DeviceTimeCorrectionDialog;
}

class DeviceTimeCorrectionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DeviceTimeCorrectionDialog(QWidget *parent = nullptr);
    ~DeviceTimeCorrectionDialog();

    void setDate(const QDate& date);

signals:
    void correctionsChanged();

private slots:
    void onDeviceChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onNudgeMinus1h();
    void onNudgeMinus1m();
    void onNudgeMinus1s();
    void onNudgePlus1s();
    void onNudgePlus1m();
    void onNudgePlus1h();
    void onResetToZero();
    void onNewCorrection();
    void onSaveStaged();
    void onDiscardStaged();
    void onApplyLastNight();
    void onDeleteRow();
    void onHistoryRowSelected();
    void onAnyControlChanged();

private:
    Ui::DeviceTimeCorrectionDialog *ui;
    QDate m_date;

    // Staged (preview) correction — not yet written to DB
    struct StagedRow {
        qint64  id        = 0;   // DB id of the row being replaced; 0 in new-entry mode
        qint64  machineId = 0;
        QString dateFrom, dateTo;
        QString type;
        qint64  offsetMs  = 0;
    };
    StagedRow m_staged;
    bool      m_hasStagedChange = false;

    Machine* currentMachine() const;

    void populateSidebar();
    void refreshTitleArea(Machine* mach);
    void refreshCurrentOffset();
    void refreshHistory();

    void applyNudge(qint64 deltaMs);
    void effectiveDateRange(QString& dateFrom, QString& dateTo) const;
    void previewStaged(Machine* mach);
    void clearStagedAndRevert();
    void resetToNewMode();
    void refreshModeLabel();
    void updateControlStates();

    QString currentTypeName() const;
    QString formatOffset(qint64 ms) const;
    void rebuildMachine(Machine* mach);
    void commitAndRefresh(Machine* mach);

    void populateControlsFromRow(const DeviceTimeCorrectionData& row);

    // Delete correction helpers
    enum DeleteChoice { CloseForward, SplitOut, RemoveEntirely, DeleteCancelled };
    DeleteChoice showDeleteRangeDialog(const DeviceTimeCorrectionData& row);
    void splitOutDate(const DeviceTimeCorrectionData& row);

    // History rows cached for delete access (parallel lists, same index)
    QList<DeviceTimeCorrectionData> m_historyRows;
    QList<Machine*>                 m_historyMachines;

protected:
    void closeEvent(QCloseEvent* event) override;
};

#endif // DEVICETIMECORRECTIONDIALOG_H
