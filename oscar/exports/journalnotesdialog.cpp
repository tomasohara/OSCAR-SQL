/* Journal Notes Export Dialog Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "journalnotesdialog.h"
#include "ui_journalnotesdialog.h"

#include <QBrush>
#include <QCalendarWidget>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextStream>
#include <QUrl>

#include "SleepLib/common.h"
#include "SleepLib/machine_common.h"
#include "SleepLib/profiles.h"
#include "database/database_manager.h"
#include "database/app_preferences_repository.h"
#include "database/profile_repository.h"
#include "mainwindow.h"

extern MainWindow* mainwin;

JournalNotesDialog::JournalNotesDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::JournalNotesDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setupCalendarFormatting();
    populateProfiles();
    loadSettings();

    QDate today = QDate::currentDate();
    ui->fromDate->setDate(today);
    ui->toDate->setDate(today);

    applyDateRange(ui->rangeCombo->currentText());
}

JournalNotesDialog::~JournalNotesDialog()
{
    delete ui;
}

// ---------------------------------------------------------------------------
//  Slots
// ---------------------------------------------------------------------------

void JournalNotesDialog::on_profileCombo_currentIndexChanged(int /*index*/)
{
    applyDateRange(ui->rangeCombo->currentText());
}

void JournalNotesDialog::on_rangeCombo_currentTextChanged(const QString& text)
{
    applyDateRange(text);
}

void JournalNotesDialog::on_exportButton_clicked()
{
    const QDate start = ui->fromDate->date();
    const QDate end   = ui->toDate->date();

    if (start > end) {
        QMessageBox::warning(this, tr("Export Journal Notes"),
                             tr("The start date must not be later than the end date."));
        return;
    }

    const bool useHtml = ui->htmlRadio->isChecked();
    const QString ext    = useHtml ? QStringLiteral("html") : QStringLiteral("md");
    const QString filter = useHtml ? tr("HTML Files (*.html)") : tr("Markdown Files (*.md)");

    QString dir = m_lastDir;
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

    const QString userName = selectedUserName();
    const QString suggestion = dir + QDir::separator()
        + tr("%1_journal_notes_%2_%3.%4")
              .arg(userName.isEmpty() ? QStringLiteral("journal") : userName,
                   start.toString(QStringLiteral("yyyyMMdd")),
                   end.toString(QStringLiteral("yyyyMMdd")),
                   ext);

    const QString filename = QFileDialog::getSaveFileName(
        this, tr("Export Journal Notes"), suggestion, filter);
    if (filename.isEmpty()) return;

    saveSettings(QFileInfo(filename).absolutePath());

    int count = useHtml ? exportToHtml(filename, start, end)
                        : exportToMarkdown(filename, start, end);

    if (count < 0) {
        QMessageBox::critical(this, tr("Export Journal Notes"),
                              tr("Could not write file:\n%1").arg(filename));
        return;
    }

    if (count == 0) {
        QMessageBox::information(this, tr("Export Journal Notes"),
                                 tr("No journal notes found in this date range."));
    } else {
        if (ui->openAfterExportCheck->isChecked()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(filename));
        } else {
            QMessageBox::information(this, tr("Export Journal Notes"),
                                     tr("Exported notes for %1 day(s).").arg(count));
        }
    }
    ui->closeButton->setText(tr("Close"));
}

void JournalNotesDialog::on_closeButton_clicked()
{
    reject();
}

// ---------------------------------------------------------------------------
//  Private helpers
// ---------------------------------------------------------------------------

void JournalNotesDialog::populateProfiles()
{
    ui->profileCombo->clear();
    m_profileIds.clear();

    ProfileRepository repo;
    const QList<ProfileData> profiles = repo.findActive();

    QString targetUsername;
    if (p_profile && p_profile->user)
        targetUsername = p_profile->user->userName();
    else if (mainwin)
        targetUsername = mainwin->selectedProfileName();

    int preSelectIndex = -1;
    for (int i = 0; i < profiles.size(); ++i) {
        ui->profileCombo->addItem(profiles[i].username);
        m_profileIds.append(profiles[i].id);
        if (!targetUsername.isEmpty() && profiles[i].username == targetUsername)
            preSelectIndex = i;
    }

    if (preSelectIndex >= 0)
        ui->profileCombo->setCurrentIndex(preSelectIndex);
    else
        ui->profileCombo->setCurrentIndex(-1);
}

qint64 JournalNotesDialog::selectedProfileId() const
{
    const int idx = ui->profileCombo->currentIndex();
    if (idx < 0 || idx >= m_profileIds.size()) return -1;
    return m_profileIds[idx];
}

QString JournalNotesDialog::selectedUserName() const
{
    const int idx = ui->profileCombo->currentIndex();
    if (idx < 0) return QString();
    return ui->profileCombo->itemText(idx);
}

void JournalNotesDialog::applyDateRange(const QString& rangeText)
{
    const bool isCustom = (rangeText == tr("Custom"));
    ui->fromDate->setEnabled(isCustom);
    ui->toDate->setEnabled(isCustom);

    if (isCustom) {
        ui->exportButton->setEnabled(true);
        ui->statusLabel->clear();
        return;
    }

    if (rangeText == tr("All")) {
        const QDate first = getFirstJournalDate();
        const QDate last  = getLastJournalDate();
        if (!first.isValid() || !last.isValid()) {
            ui->exportButton->setEnabled(false);
            ui->statusLabel->setText(tr("No journal notes found for this profile."));
            ui->fromDate->setDate(ui->fromDate->minimumDate());
            ui->toDate->setDate(ui->toDate->minimumDate());
            return;
        }
        ui->exportButton->setEnabled(true);
        ui->statusLabel->clear();
        ui->fromDate->setDate(first);
        ui->toDate->setDate(last);
        return;
    }

    const QDate last = getLastJournalDate();
    if (!last.isValid()) {
        ui->exportButton->setEnabled(false);
        ui->statusLabel->setText(tr("No journal notes found for this profile."));
        ui->fromDate->setDate(ui->fromDate->minimumDate());
        ui->toDate->setDate(ui->toDate->minimumDate());
        return;
    }
    ui->exportButton->setEnabled(true);
    ui->statusLabel->clear();

    if (rangeText == tr("Last Week")) {
        ui->fromDate->setDate(last.addDays(-6));
    } else if (rangeText == tr("Last Month")) {
        ui->fromDate->setDate(last.addMonths(-1).addDays(1));
    } else if (rangeText == tr("Last 6 Months")) {
        ui->fromDate->setDate(last.addMonths(-6).addDays(1));
    } else if (rangeText == tr("Last Year")) {
        ui->fromDate->setDate(last.addYears(-1).addDays(1));
    }
    ui->toDate->setDate(last);
}

void JournalNotesDialog::setupCalendarFormatting()
{
    QLocale locale = QLocale::system();
    QString fmt = locale.dateFormat(QLocale::ShortFormat);
    if (!fmt.toLower().contains(QLatin1String("yyyy")))
        fmt.replace(QLatin1String("yy"), QLatin1String("yyyy"));
    ui->fromDate->setDisplayFormat(fmt);
    ui->toDate->setDisplayFormat(fmt);

    for (QDateEdit* de : { ui->fromDate, ui->toDate }) {
        de->setSpecialValueText(QLatin1String(" "));
        QCalendarWidget* cal = de->calendarWidget();
        QTextCharFormat format = cal->weekdayTextFormat(Qt::Saturday);
        format.setForeground(QBrush(Qt::black, Qt::SolidPattern));
        cal->setWeekdayTextFormat(Qt::Saturday, format);
        cal->setWeekdayTextFormat(Qt::Sunday, format);
    }
}

void JournalNotesDialog::saveSettings(const QString& dir)
{
    m_lastDir = dir;
    AppPreferencesRepository repo;
    repo.save("JournalNotesDialog", "lastDir",         dir);
    repo.save("JournalNotesDialog", "openAfterExport", ui->openAfterExportCheck->isChecked());
    repo.save("JournalNotesDialog", "extraData",       ui->extraDataCheck->isChecked());
}

void JournalNotesDialog::loadSettings()
{
    AppPreferencesRepository repo;
    const auto rows = repo.loadByCategory("JournalNotesDialog");
    for (const AppPrefData& row : rows) {
        if      (row.key == "lastDir")         m_lastDir = row.value;
        else if (row.key == "openAfterExport") ui->openAfterExportCheck->setChecked(row.value == "true" || row.value == "1");
        else if (row.key == "extraData")       ui->extraDataCheck->setChecked(row.value == "true" || row.value == "1");
    }
}

QDate JournalNotesDialog::getFirstJournalDate() const
{
    const qint64 pid = selectedProfileId();
    if (pid < 0) return QDate();
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral(
        "SELECT MIN(DATE(s.start_time/1000 - 43200, 'unixepoch', 'localtime')) "
        "FROM sessions s JOIN machines m ON s.machine_id = m.id "
        "WHERE m.profile_id = :pid AND m.machine_type = 4"));
    q.bindValue(QStringLiteral(":pid"), pid);
    if (q.exec() && q.next() && !q.value(0).isNull()) {
        QDate d = QDate::fromString(q.value(0).toString(), Qt::ISODate);
        if (d.isValid()) return d;
    }
    return QDate();
}

QDate JournalNotesDialog::getLastJournalDate() const
{
    const qint64 pid = selectedProfileId();
    if (pid < 0) return QDate();
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral(
        "SELECT MAX(DATE(s.start_time/1000 - 43200, 'unixepoch', 'localtime')) "
        "FROM sessions s JOIN machines m ON s.machine_id = m.id "
        "WHERE m.profile_id = :pid AND m.machine_type = 4"));
    q.bindValue(QStringLiteral(":pid"), pid);
    if (q.exec() && q.next() && !q.value(0).isNull()) {
        QDate d = QDate::fromString(q.value(0).toString(), Qt::ISODate);
        if (d.isValid()) return d;
    }
    return QDate();
}

// ---------------------------------------------------------------------------
//  Export helpers
// ---------------------------------------------------------------------------

QString JournalNotesDialog::metricsString(int feelings, double weight_kg,
                                          bool zombieMode, bool metric)
{
    QStringList parts;

    if (feelings > 0) {
        if (zombieMode)
            parts << QObject::tr("Feelings: %1/100").arg(feelings);
        else
            parts << QObject::tr("Feelings: %1/10").arg(feelings / 10.0, 0, 'f', 1);
    }

    if (weight_kg > 0.0001) {
        if (metric)
            parts << QObject::tr("Weight: %1 kg").arg(weight_kg, 0, 'f', 1);
        else
            parts << QObject::tr("Weight: %1 lbs").arg(weight_kg * pounds_per_kg, 0, 'f', 1);
    }

    return parts.join(QStringLiteral("  "));
}

QString JournalNotesDialog::extractBodyContent(const QString& html)
{
    const int bodyTag     = html.indexOf(QLatin1String("<body"));
    if (bodyTag == -1) return html;
    const int contentStart = html.indexOf(QLatin1Char('>'), bodyTag) + 1;
    const int bodyEnd      = html.lastIndexOf(QLatin1String("</body>"));
    if (contentStart <= 0 || bodyEnd <= contentStart) return html;
    return html.mid(contentStart, bodyEnd - contentStart).trimmed();
}

// ---------------------------------------------------------------------------
//  Shared DB query used by both export methods
// ---------------------------------------------------------------------------
//
// Returns one row per journal day in [start,end]:
//   col 0 = day_date  (ISO text)
//   col 1 = notes     (HTML, from json_value; NULL if no notes)
//   col 2 = feelings  (integer 0–100, from value; NULL if not set)
//   col 3 = weight_kg (real; NULL if not set)

static QSqlQuery buildExportQuery(qint64 pid, QDate start, QDate end)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral(
        "SELECT "
        "  DATE(s.start_time/1000 - 43200, 'unixepoch', 'localtime') AS day_date, "
        "  MAX(CASE WHEN ss.channel_id = :notes_id   THEN ss.json_value END) AS notes, "
        "  MAX(CASE WHEN ss.channel_id = :zombie_id  THEN CAST(ss.value AS INTEGER) END) AS feelings, "
        "  MAX(CASE WHEN ss.channel_id = :weight_id  THEN ss.value END) AS weight "
        "FROM sessions s "
        "JOIN machines m ON s.machine_id = m.id "
        "LEFT JOIN session_settings ss ON ss.session_id = s.id "
        "WHERE m.profile_id = :pid AND m.machine_type = 4 "
        "  AND DATE(s.start_time/1000 - 43200, 'unixepoch', 'localtime') "
        "      BETWEEN :start AND :end "
        "GROUP BY day_date "
        "ORDER BY day_date"));
    q.bindValue(QStringLiteral(":notes_id"),  (int)Journal_Notes);
    q.bindValue(QStringLiteral(":zombie_id"), (int)Journal_ZombieMeter);
    q.bindValue(QStringLiteral(":weight_id"), (int)Journal_Weight);
    q.bindValue(QStringLiteral(":pid"),   pid);
    q.bindValue(QStringLiteral(":start"), start.toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":end"),   end.toString(Qt::ISODate));
    q.exec();
    return q;
}

// Query a single boolean/integer profile preference value (default 0).
static int profilePref(qint64 pid, const QString& key)
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(QStringLiteral(
        "SELECT value FROM profile_preferences "
        "WHERE profile_id = :pid AND key = :key LIMIT 1"));
    q.bindValue(QStringLiteral(":pid"), pid);
    q.bindValue(QStringLiteral(":key"), key);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

// ---------------------------------------------------------------------------
//  Export
// ---------------------------------------------------------------------------

int JournalNotesDialog::exportToHtml(const QString& filename, QDate start, QDate end)
{
    const qint64 pid       = selectedProfileId();
    const QString userName = selectedUserName();
    const bool withExtras  = ui->extraDataCheck->isChecked();

    bool zombieMode = false;
    bool metric     = true;
    if (pid >= 0 && withExtras) {
        zombieMode = profilePref(pid, QStringLiteral("ZombieMode")) != 0;
        metric     = profilePref(pid, QStringLiteral("UnitSystem")) == US_Metric;
    }

    QLocale locale = QLocale::system();

    QString html;
    html += QStringLiteral("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
                           "<meta charset=\"utf-8\">\n");
    html += QStringLiteral("<title>") + tr("Journal Notes") + QStringLiteral("</title>\n");
    html += QStringLiteral(
        "<style>\n"
        "body  { font-family: sans-serif; font-size: 10pt; max-width: 800px; margin: 2em auto; padding: 0 1em; }\n"
        "h1    { border-bottom: 2px solid #555; padding-bottom: 0.3em; }\n"
        ".day  { font-weight: bold; font-size: 11pt; margin-top: 2em; margin-bottom: 0.2em; }\n"
        ".meta { color: #777; font-size: 0.9em; margin-bottom: 1.5em; }\n"
        ".notes   { margin-left: 1.5em; margin-top: 0; }\n"
        ".metrics { text-align: right; font-size: 0.85em; color: #555; margin-top: 0.1em; }\n"
        "@media print { .meta { display: none; } }\n"
        "</style>\n</head>\n<body>\n");

    if (!userName.isEmpty())
        html += QStringLiteral("<h1>") + tr("Journal Notes — %1").arg(userName.toHtmlEscaped()) + QStringLiteral("</h1>\n");
    else
        html += QStringLiteral("<h1>") + tr("Journal Notes") + QStringLiteral("</h1>\n");

    html += QStringLiteral("<p class=\"meta\">")
          + tr("From %1 to %2").arg(locale.toString(start, QLocale::LongFormat),
                                    locale.toString(end,   QLocale::LongFormat))
          + QStringLiteral("</p>\n");

    QSqlQuery q = buildExportQuery(pid, start, end);
    int count = 0;

    while (q.next()) {
        const QDate   date     = QDate::fromString(q.value(0).toString(), Qt::ISODate);
        const QString notesHtml = q.value(1).toString();
        const int     feelings  = q.value(2).toInt();
        const double  weight    = q.value(3).toDouble();

        const bool hasNotes   = !notesHtml.trimmed().isEmpty();
        const QString metrics = withExtras ? metricsString(feelings, weight, zombieMode, metric) : QString();
        const bool hasMetrics = !metrics.isEmpty();
        if (!hasNotes && !hasMetrics) continue;

        html += QStringLiteral("<p class=\"day\">")
              + locale.toString(date, QLocale::LongFormat).toHtmlEscaped()
              + QStringLiteral("</p>\n");
        if (hasNotes)
            html += QStringLiteral("<div class=\"notes\">") + extractBodyContent(notesHtml) + QStringLiteral("</div>\n");
        if (hasMetrics)
            html += QStringLiteral("<p class=\"metrics\">") + metrics.toHtmlEscaped() + QStringLiteral("</p>\n");
        ++count;
    }

    html += QStringLiteral("</body>\n</html>\n");

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return -1;
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << html;
    return count;
}

int JournalNotesDialog::exportToMarkdown(const QString& filename, QDate start, QDate end)
{
    const qint64 pid       = selectedProfileId();
    const QString userName = selectedUserName();
    const bool withExtras  = ui->extraDataCheck->isChecked();

    bool zombieMode = false;
    bool metric     = true;
    if (pid >= 0 && withExtras) {
        zombieMode = profilePref(pid, QStringLiteral("ZombieMode")) != 0;
        metric     = profilePref(pid, QStringLiteral("UnitSystem")) == US_Metric;
    }

    QLocale locale = QLocale::system();

    QString md;
    if (!userName.isEmpty())
        md += QStringLiteral("# ") + tr("Journal Notes — %1").arg(userName) + QStringLiteral("\n\n");
    else
        md += QStringLiteral("# ") + tr("Journal Notes") + QStringLiteral("\n\n");

    md += tr("From %1 to %2").arg(locale.toString(start, QLocale::LongFormat),
                                  locale.toString(end,   QLocale::LongFormat))
        + QStringLiteral("\n\n");

    QSqlQuery q = buildExportQuery(pid, start, end);
    int count = 0;

    while (q.next()) {
        const QDate   date      = QDate::fromString(q.value(0).toString(), Qt::ISODate);
        const QString notesHtml = q.value(1).toString();
        const int     feelings  = q.value(2).toInt();
        const double  weight    = q.value(3).toDouble();

        const QString metrics = withExtras ? metricsString(feelings, weight, zombieMode, metric) : QString();
        const bool hasMetrics = !metrics.isEmpty();

        QString notesMd;
        if (!notesHtml.trimmed().isEmpty()) {
            QTextDocument doc;
            doc.setHtml(notesHtml);
            notesMd = doc.toMarkdown().trimmed();
        }
        const bool hasNotes = !notesMd.isEmpty();
        if (!hasNotes && !hasMetrics) continue;

        md += QStringLiteral("##### ") + locale.toString(date, QLocale::LongFormat) + QStringLiteral("\n\n");
        if (hasNotes)
            md += notesMd + QStringLiteral("\n\n");
        if (hasMetrics)
            md += QStringLiteral("*") + metrics + QStringLiteral("*\n\n");
        ++count;
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return -1;
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << md;
    return count;
}
