# Purge Range of Days — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add "Purge Range of Days" under Data → Advanced that deletes CPAP/device data for a user-selected date range, with the same data-type choices as the existing single-day purge.

**Architecture:** Extract per-day purge logic from `purgeDay()` into a new private helper `purgeDayData(QDate, MachineType)`. A new `PurgeRangeDaysDialog` (programmatic, no .ui file) collects the date range and type. The slot `on_actionPurgeRangeOfDays_triggered()` drives a `QProgressDialog` loop calling `purgeDayData()` day by day, then reloads the UI once.

**Tech Stack:** C++17, Qt6, SQLite (via existing repository classes), QtWidgets

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `oscar/mainwindow.ui` | Modify | Add `actionPurgeRangeOfDays` action + menu item |
| `oscar/mainwindow.h` | Modify | Declare new slot and `purgeDayData()` helper |
| `oscar/mainwindow.cpp` | Modify | Implement `purgeDayData()`, refactor `purgeDay()`, implement slot |
| `oscar/purgerangedaysdialog.h` | Create | Dialog class declaration |
| `oscar/purgerangedaysdialog.cpp` | Create | Dialog class implementation (programmatic layout) |
| `oscar/oscar.pro` | Modify | Add new dialog source + header |

---

### Task 1: Add the menu action to mainwindow.ui

**Files:**
- Modify: `oscar/mainwindow.ui`

The Advanced menu currently lists `menuPurge_Current_Selected_Day`, then `menuPurge_CPAP_Data`, then a separator, then `menuPurge_Oximetry_Data`. The new action goes immediately after `menuPurge_Current_Selected_Day`.

- [ ] **Step 1: Add the action definition**

In `mainwindow.ui`, find the block of `<action name="actionPurge_Current_Day">` definitions near the bottom of the file (around line 2722). Add this new action definition in the same area:

```xml
  <action name="actionPurgeRangeOfDays">
   <property name="text">
    <string>Purge Range of Days...</string>
   </property>
  </action>
```

- [ ] **Step 2: Wire the action into the Advanced menu**

In `mainwindow.ui`, find this section inside `menu_Advanced`:

```xml
     <addaction name="menuPurge_Current_Selected_Day"/>
     <addaction name="menuPurge_CPAP_Data"/>
```

Change it to:

```xml
     <addaction name="menuPurge_Current_Selected_Day"/>
     <addaction name="actionPurgeRangeOfDays"/>
     <addaction name="menuPurge_CPAP_Data"/>
```

- [ ] **Step 3: Build in QtCreator**

Build the project. Expected: compiles cleanly; a new "Purge Range of Days..." entry appears in Data → Advanced (it does nothing yet).

---

### Task 2: Create PurgeRangeDaysDialog

**Files:**
- Create: `oscar/purgerangedaysdialog.h`
- Create: `oscar/purgerangedaysdialog.cpp`

No `.ui` file — layout is built programmatically (dialog is simple enough).

- [ ] **Step 1: Create the header**

Create `oscar/purgerangedaysdialog.h`:

```cpp
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
```

- [ ] **Step 2: Create the implementation**

Create `oscar/purgerangedaysdialog.cpp`:

```cpp
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
#include <QHBoxLayout>
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
```

---

### Task 3: Add new files to oscar.pro

**Files:**
- Modify: `oscar/oscar.pro`

- [ ] **Step 1: Add the .cpp source**

In `oscar.pro`, find the SOURCES block. After `sharedialog.cpp`, add:

```
    purgerangedaysdialog.cpp \
```

- [ ] **Step 2: Add the .h header**

In `oscar.pro`, find the HEADERS block. After `sharedialog.h`, add:

```
    purgerangedaysdialog.h \
```

- [ ] **Step 3: Build in QtCreator**

Build the project. Expected: compiles cleanly (dialog class is compiled but not yet used).

---

### Task 4: Extract purgeDayData() and refactor purgeDay()

**Files:**
- Modify: `oscar/mainwindow.h` (lines ~434, ~465)
- Modify: `oscar/mainwindow.cpp` (lines ~2150–2240)

- [ ] **Step 1: Declare purgeDayData() in mainwindow.h**

In `oscar/mainwindow.h`, find the private section that currently reads:

```cpp
    void purgeDay(MachineType type);
    void importNonCPAP(MachineLoader &loader);
```

Change it to:

```cpp
    void purgeDay(MachineType type);
    void importNonCPAP(MachineLoader &loader);

    /*! \brief Destroy sessions for \a date matching \a type. Returns true if any data
     *         was purged. Does not update the UI; caller handles reload.
     *         Special handling: MT_JOURNAL == all data; MT_UNKNOWN == all except journal. */
    bool purgeDayData(QDate date, MachineType type);
```

- [ ] **Step 2: Add purgeDayData() implementation in mainwindow.cpp**

In `oscar/mainwindow.cpp`, find the comment line just above `purgeDay()`:

```cpp
// Purge data for a given device type.
// Special handling: MT_JOURNAL == All data. MT_UNKNOWN == All except journal
void MainWindow::purgeDay(MachineType type)
```

Insert the new function **before** it (before that comment):

```cpp
// Destroy sessions for a single day matching type. Returns true if any sessions were purged.
// Special handling: MT_JOURNAL == All data. MT_UNKNOWN == All except journal.
// Does not touch the UI; callers handle unload/reload.
bool MainWindow::purgeDayData(QDate date, MachineType type)
{
    Day *day = p_profile->GetDay(date, MT_UNKNOWN);
    if (!day)
        return false;

    Machine *cpap = nullptr;
    QList<Session *> list;
    for (auto s = day->begin(); s != day->end(); ++s) {
        Session *sess = *s;
        if (type == MT_JOURNAL || (type == MT_UNKNOWN && sess->type() != MT_JOURNAL) ||
                sess->type() == type) {
            list.append(sess);
            qDebug() << "Purging session from" << sess->machine()->loaderName()
                     << "ID:" << sess->session()
                     << "[" + QDateTime::fromSecsSinceEpoch(sess->session()).toString() + "]";
            if (sess->type() == MT_CPAP)
                cpap = day->machine(MT_CPAP);
        } else {
            qDebug() << "Skipping session from" << sess->machine()->loaderName()
                     << "ID:" << sess->session()
                     << "[" + QDateTime::fromSecsSinceEpoch(sess->session()).toString() + "]";
        }
    }

    if (list.isEmpty())
        return false;

    if (cpap) {
        QFile rxcache(p_profile->Get("{" + STR_GEN_DataFolder + "}/RXChanges.cache"));
        rxcache.remove();
        QFile sumfile(cpap->getDataPath() + "Summaries.xml.gz");
        sumfile.remove();
    }

    QSet<Machine *> machines;
    for (Session *sess : list) {
        machines += sess->machine();
        sess->Destroy();
        delete sess;
    }
    for (auto &mach : machines)
        mach->SaveSummaryCache();

    if (cpap) {
        QDate pd = cpap->purgeDate();
        if (pd.isNull() || date < pd)
            cpap->setPurgeDate(date);
    }

    return true;
}
```

- [ ] **Step 3: Refactor purgeDay() to call purgeDayData()**

Replace the entire body of `MainWindow::purgeDay()` (from `{` to the closing `}` at line ~2240) with the slimmed-down version that delegates to `purgeDayData()`:

```cpp
// Purge data for a given device type.
// Special handling: MT_JOURNAL == All data. MT_UNKNOWN == All except journal
void MainWindow::purgeDay(MachineType type)
{
    if (!daily)
        return;
    QDate date = daily->getDate();
    qDebug() << "Purging data from" << date;
    daily->Unload(date);

    if (!purgeDayData(date, type))
        return;

    Day *day = p_profile->GetDay(date, MT_UNKNOWN);
    {
        ProfileRepository profileRepo;
        ProfileData profileData = profileRepo.findByUsername(p_profile->user->userName());
        if (profileData.id > 0) {
            DailySummaryRepository summaryRepo;
            bool recalculated = day && summaryRepo.calculateAndStoreFromDay(day, profileData.id);
            if (!recalculated)
                summaryRepo.invalidateDate(profileData.id, date);
        }
    }

    if (type == MT_JOURNAL)
        daily->clearJournalNotesEditor();

    daily->clearLastDay();
    daily->LoadDate(date);
    if (overview)
        overview->ReloadGraphs();
    if (welcome)
        welcome->refreshPage();
    GenerateStatistics();
}
```

- [ ] **Step 4: Build and verify single-day purge still works**

Build in QtCreator. Expected: clean build. Manually test "Purge Current Selected Day → CPAP" on a day with data — it should behave identically to before.

---

### Task 5: Implement on_actionPurgeRangeOfDays_triggered()

**Files:**
- Modify: `oscar/mainwindow.h`
- Modify: `oscar/mainwindow.cpp`

- [ ] **Step 1: Add includes to mainwindow.cpp**

In `oscar/mainwindow.cpp`, in the `#include` block near the other dialog includes (around line 75–83), add:

```cpp
#include <QProgressDialog>
#include "purgerangedaysdialog.h"
```

- [ ] **Step 2: Declare the slot in mainwindow.h**

In `oscar/mainwindow.h`, in the private slots section alongside the other purge slots (around line 291), add:

```cpp
    //! \brief Open the Purge Range of Days dialog and purge data for a date range.
    void on_actionPurgeRangeOfDays_triggered();
```

- [ ] **Step 3: Implement the slot in mainwindow.cpp**

After the closing `}` of `MainWindow::purgeDay()`, add:

```cpp
void MainWindow::on_actionPurgeRangeOfDays_triggered()
{
    if (!daily)
        return;

    PurgeRangeDaysDialog dlg(daily->getDate(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QDate startDate  = dlg.startDate();
    const QDate endDate    = dlg.endDate();
    const MachineType type = dlg.machineType();
    const int numDays      = startDate.daysTo(endDate) + 1;

    QString typeName;
    switch (type) {
    case MT_CPAP:       typeName = tr("CPAP");               break;
    case MT_OXIMETER:   typeName = tr("Oximetry");           break;
    case MT_SLEEPSTAGE: typeName = tr("Sleep Stage");        break;
    case MT_POSITION:   typeName = tr("Position");           break;
    case MT_UNKNOWN:    typeName = tr("All except Notes");   break;
    case MT_JOURNAL:    typeName = tr("All including Notes"); break;
    default:            typeName = tr("Unknown");            break;
    }

    QLocale loc;
    if (staticQMessageBox::question(this,
            tr("Confirm Purge"),
            tr("<p>Purge <b>%1</b> data from <b>%2</b> to <b>%3</b> (%4 day(s)).</p>"
               "<p>Are you <b>absolutely sure</b> you want to proceed?</p>")
               .arg(typeName,
                    loc.toString(startDate, QLocale::ShortFormat),
                    loc.toString(endDate,   QLocale::ShortFormat),
                    QString::number(numDays)),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    const QDate viewDate = daily->getDate();
    daily->Unload(viewDate);

    QProgressDialog progress(tr("Purging data..."), tr("Cancel"), 0, numDays, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QList<QDate> purgedDates;
    QDate cur = startDate;
    int step  = 0;
    while (cur <= endDate) {
        if (progress.wasCanceled())
            break;
        progress.setValue(step);
        progress.setLabelText(tr("Purging %1...").arg(loc.toString(cur, QLocale::ShortFormat)));
        QApplication::processEvents();

        if (purgeDayData(cur, type))
            purgedDates.append(cur);

        cur = cur.addDays(1);
        ++step;
    }
    progress.setValue(numDays);

    if (purgedDates.isEmpty()) {
        staticQMessageBox::information(this,
            tr("Purge Range of Days"),
            tr("No data was found in the selected date range."),
            QMessageBox::Ok);
        return;
    }

    // Recalculate daily summaries for all affected dates
    {
        ProfileRepository profileRepo;
        ProfileData profileData = profileRepo.findByUsername(p_profile->user->userName());
        if (profileData.id > 0) {
            DailySummaryRepository summaryRepo;
            for (const QDate &d : purgedDates) {
                Day *day = p_profile->GetDay(d, MT_UNKNOWN);
                bool recalculated = day && summaryRepo.calculateAndStoreFromDay(day, profileData.id);
                if (!recalculated)
                    summaryRepo.invalidateDate(profileData.id, d);
            }
        }
    }

    if (type == MT_JOURNAL)
        daily->clearJournalNotesEditor();

    daily->clearLastDay();
    daily->LoadDate(viewDate);
    if (overview)
        overview->ReloadGraphs();
    if (welcome)
        welcome->refreshPage();
    GenerateStatistics();
}
```

- [ ] **Step 4: Build in QtCreator**

Build. Expected: clean build, no warnings.

- [ ] **Step 5: Manual test — happy path**

1. Open OSCAR, navigate to a day with CPAP data.
2. Data → Advanced → Purge Range of Days...
3. Set start = end = that day, select CPAP, click OK.
4. Confirm the warning dialog. Verify the progress dialog appears briefly.
5. Verify the day now shows no CPAP data in the Daily view.
6. Verify Overview and Statistics have updated.

- [ ] **Step 6: Manual test — multi-day range**

1. Set start and end to a range spanning 3+ days with CPAP data.
2. Click OK, confirm. Verify progress bar increments.
3. Navigate to each purged day — all should show no CPAP data.

- [ ] **Step 7: Manual test — no data in range**

1. Set dates to a range with no data.
2. Click OK, confirm. Verify the "No data was found" message appears.

- [ ] **Step 8: Manual test — validation and cancel**

1. Open dialog, set end date earlier than start date, click OK.
   Expected: red error message appears, dialog stays open.
2. Click Cancel — dialog closes, no data is changed.
3. Open dialog, click OK with valid dates, then click Cancel on the
   progress dialog mid-purge. Verify already-purged days remain purged
   but subsequent days are intact.

---

### Task 6: Commit

- [ ] **Step 1: Stage and commit**

```
git add oscar/mainwindow.ui
git add oscar/mainwindow.h
git add oscar/mainwindow.cpp
git add oscar/purgerangedaysdialog.h
git add oscar/purgerangedaysdialog.cpp
git add oscar/oscar.pro
```

Commit message:

```
Feat: Add Purge Range of Days under Data/Advanced (#NNN)

New menu item Data → Advanced → "Purge Range of Days..." lets the user
purge a contiguous date range in one operation. A dialog collects start/end
dates (defaulting to the currently viewed day) and a data-type selection
mirroring the single-day purge options. A progress bar tracks the loop
and supports mid-range cancel. The per-day destruction logic was extracted
from purgeDay() into a new purgeDayData() helper; single-day purge
behaviour is unchanged.

Closes #NNN
```

*(Replace `#NNN` with the GitLab issue number created before committing.)*
