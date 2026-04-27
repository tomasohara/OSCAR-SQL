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
#include <QDesktopServices>
#include <QUrl>
#include <QCalendarWidget>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextStream>

#include "SleepLib/common.h"
#include "SleepLib/day.h"
#include "SleepLib/machine_common.h"
#include "SleepLib/profiles.h"
#include "SleepLib/session.h"

JournalNotesDialog::JournalNotesDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::JournalNotesDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setupCalendarFormatting();

    QDate today = QDate::currentDate();
    ui->fromDate->setDate(today);
    ui->toDate->setDate(today);

    applyDateRange(ui->rangeCombo->currentText());
    loadSettings();
}

JournalNotesDialog::~JournalNotesDialog()
{
    delete ui;
}

// ---------------------------------------------------------------------------
//  Slots
// ---------------------------------------------------------------------------

void JournalNotesDialog::on_rangeCombo_currentTextChanged(const QString& text)
{
    applyDateRange(text);
}

void JournalNotesDialog::on_exportButton_clicked()
{
    const QDate start = ui->fromDate->date();
    const QDate end   = ui->toDate->date();

    if (start > end) {
        QMessageBox::warning(this, tr("Journal Report"),
                             tr("The start date must not be later than the end date."));
        return;
    }

    const bool useHtml = ui->htmlRadio->isChecked();
    const QString ext    = useHtml ? QStringLiteral("html") : QStringLiteral("md");
    const QString filter = useHtml ? tr("HTML Files (*.html)") : tr("Markdown Files (*.md)");

    QString dir = m_lastDir;
    if (dir.isEmpty() || !QDir(dir).exists()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }

    const QString userName = (p_profile && p_profile->user)
        ? p_profile->user->userName()
        : QStringLiteral("journal");

    const QString suggestion = dir + QDir::separator()
        + tr("%1_journal_notes_%2_%3.%4")
              .arg(userName,
                   start.toString(QStringLiteral("yyyyMMdd")),
                   end.toString(QStringLiteral("yyyyMMdd")),
                   ext);

    const QString filename = QFileDialog::getSaveFileName(
        this, tr("Journal Report"), suggestion, filter);

    if (filename.isEmpty()) return;

    saveSettings(QFileInfo(filename).absolutePath());

    int count = useHtml ? exportToHtml(filename, start, end)
                        : exportToMarkdown(filename, start, end);

    if (count < 0) {
        QMessageBox::critical(this, tr("Journal Report"),
                              tr("Could not write file:\n%1").arg(filename));
        return;
    }

    if (count == 0) {
        QMessageBox::information(this, tr("Journal Report"),
                                 tr("No journal notes found in this date range."));
    } else {
        if (ui->openAfterExportCheck->isChecked()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(filename));
        } else {
            QMessageBox::information(this, tr("Journal Report"),
                                     tr("Exported notes for %n day(s).", "", count));
        }
    }
}

void JournalNotesDialog::on_closeButton_clicked()
{
    reject();
}

// ---------------------------------------------------------------------------
//  Private helpers
// ---------------------------------------------------------------------------

void JournalNotesDialog::applyDateRange(const QString& rangeText)
{
    const bool isCustom = (rangeText == tr("Custom"));
    ui->fromDate->setEnabled(isCustom);
    ui->toDate->setEnabled(isCustom);

    if (isCustom) return;

    if (rangeText == tr("All")) {
        const QDate first = getFirstJournalDate();
        const QDate last  = getLastJournalDate();
        if (!first.isValid() || !last.isValid()) {
            ui->exportButton->setEnabled(false);
            return;
        }
        ui->exportButton->setEnabled(true);
        ui->fromDate->setDate(first);
        ui->toDate->setDate(last);
        return;
    }

    const QDate last = getLastJournalDate();
    if (!last.isValid()) {
        ui->exportButton->setEnabled(false);
        return;
    }
    ui->exportButton->setEnabled(true);

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
    if (!fmt.toLower().contains(QLatin1String("yyyy"))) {
        fmt.replace(QLatin1String("yy"), QLatin1String("yyyy"));
    }
    ui->fromDate->setDisplayFormat(fmt);
    ui->toDate->setDisplayFormat(fmt);

    for (QDateEdit* de : { ui->fromDate, ui->toDate }) {
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
    QSettings s;
    s.beginGroup(QStringLiteral("JournalNotesDialog"));
    s.setValue(QStringLiteral("lastDir"),         dir);
    s.setValue(QStringLiteral("openAfterExport"), ui->openAfterExportCheck->isChecked());
    s.setValue(QStringLiteral("extraData"),       ui->extraDataCheck->isChecked());
    s.endGroup();
}

void JournalNotesDialog::loadSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("JournalNotesDialog"));
    m_lastDir = s.value(QStringLiteral("lastDir")).toString();
    ui->openAfterExportCheck->setChecked(s.value(QStringLiteral("openAfterExport"), false).toBool());
    ui->extraDataCheck->setChecked(      s.value(QStringLiteral("extraData"),       false).toBool());
    s.endGroup();
}

QDate JournalNotesDialog::getFirstJournalDate() const
{
    // FirstDay(MT_JOURNAL) returns m_last as a fallback when no MT_JOURNAL day
    // exists, so verify the returned date actually has journal data.
    if (p_profile && p_profile->GetMachine(MT_JOURNAL)) {
        QDate d = p_profile->FirstDay(MT_JOURNAL);
        if (d.isValid() && p_profile->FindDay(d, MT_JOURNAL)) return d;
    }
    return QDate();
}

QDate JournalNotesDialog::getLastJournalDate() const
{
    // LastDay(MT_JOURNAL) returns m_first as a fallback when no MT_JOURNAL day
    // exists, so verify the returned date actually has journal data.
    if (p_profile && p_profile->GetMachine(MT_JOURNAL)) {
        QDate d = p_profile->LastDay(MT_JOURNAL);
        if (d.isValid() && p_profile->FindDay(d, MT_JOURNAL)) return d;
    }
    return QDate();
}

// ---------------------------------------------------------------------------
//  Export
// ---------------------------------------------------------------------------

QString JournalNotesDialog::metricsString(Session* sess) const
{
    QStringList parts;

    if (sess->settings.contains(Journal_ZombieMeter)) {
        int value = sess->settings[Journal_ZombieMeter].toInt();
        if (value > 0) {
            if (p_profile->appearance->zombieMode()) {
                parts << tr("Feelings: %1/100").arg(value);
            } else {
                parts << tr("Feelings: %1/10").arg(value / 10.0, 0, 'f', 1);
            }
        }
    }

    if (sess->settings.contains(Journal_Weight)) {
        double kg = sess->settings[Journal_Weight].toDouble();
        if (kg > 0.0001) {
            if (p_profile->general->unitSystem() == US_Metric) {
                parts << tr("Weight: %1 kg").arg(kg, 0, 'f', 1);
            } else {
                parts << tr("Weight: %1 lbs").arg(kg * pounds_per_kg, 0, 'f', 1);
            }
        }
    }

    return parts.join(QStringLiteral("  "));
}

QString JournalNotesDialog::extractBodyContent(const QString& html)
{
    const int bodyTag = html.indexOf(QLatin1String("<body"));
    if (bodyTag == -1) return html;
    const int contentStart = html.indexOf(QLatin1Char('>'), bodyTag) + 1;
    const int bodyEnd      = html.lastIndexOf(QLatin1String("</body>"));
    if (contentStart <= 0 || bodyEnd <= contentStart) return html;
    return html.mid(contentStart, bodyEnd - contentStart).trimmed();
}

int JournalNotesDialog::exportToHtml(const QString& filename, QDate start, QDate end)
{
    const QString userName = (p_profile && p_profile->user)
        ? p_profile->user->userName() : QString();

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
        "@media print {\n"
        "  .meta { display: none; }\n"
        "}\n"
        "</style>\n</head>\n<body>\n");

    if (!userName.isEmpty()) {
        html += QStringLiteral("<h1>") + tr("Journal Notes — %1").arg(userName.toHtmlEscaped())
              + QStringLiteral("</h1>\n");
    } else {
        html += QStringLiteral("<h1>") + tr("Journal Notes") + QStringLiteral("</h1>\n");
    }
    html += QStringLiteral("<p class=\"meta\">")
          + tr("From %1 to %2")
                .arg(locale.toString(start, QLocale::LongFormat),
                     locale.toString(end,   QLocale::LongFormat))
          + QStringLiteral("</p>\n");

    const bool withExtras = ui->extraDataCheck->isChecked();

    int count = 0;
    for (QDate date = start; date <= end; date = date.addDays(1)) {
        Day* day = p_profile->GetDay(date, MT_JOURNAL);
        if (!day) continue;
        Session* sess = day->firstSession(MT_JOURNAL);
        if (!sess) continue;

        const QString notesHtml = sess->settings.contains(Journal_Notes)
            ? sess->settings[Journal_Notes].toString() : QString();
        const bool hasNotes = !notesHtml.trimmed().isEmpty();
        const QString metrics = withExtras ? metricsString(sess) : QString();
        const bool hasMetrics = !metrics.isEmpty();
        if (!hasNotes && !hasMetrics) continue;

        html += QStringLiteral("<p class=\"day\">")
              + locale.toString(date, QLocale::LongFormat).toHtmlEscaped()
              + QStringLiteral("</p>\n");
        if (hasNotes) {
            html += QStringLiteral("<div class=\"notes\">")
                  + extractBodyContent(notesHtml)
                  + QStringLiteral("</div>\n");
        }
        if (hasMetrics) {
            html += QStringLiteral("<p class=\"metrics\">")
                  + metrics.toHtmlEscaped()
                  + QStringLiteral("</p>\n");
        }
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
    const QString userName = (p_profile && p_profile->user)
        ? p_profile->user->userName() : QString();

    QLocale locale = QLocale::system();

    QString md;
    if (!userName.isEmpty()) {
        md += QStringLiteral("# ") + tr("Journal Notes — %1").arg(userName) + QStringLiteral("\n\n");
    } else {
        md += QStringLiteral("# ") + tr("Journal Notes") + QStringLiteral("\n\n");
    }
    md += tr("From %1 to %2")
              .arg(locale.toString(start, QLocale::LongFormat),
                   locale.toString(end,   QLocale::LongFormat))
        + QStringLiteral("\n\n");

    const bool withExtras = ui->extraDataCheck->isChecked();

    int count = 0;
    for (QDate date = start; date <= end; date = date.addDays(1)) {
        Day* day = p_profile->GetDay(date, MT_JOURNAL);
        if (!day) continue;
        Session* sess = day->firstSession(MT_JOURNAL);
        if (!sess) continue;

        const QString notesHtml = sess->settings.contains(Journal_Notes)
            ? sess->settings[Journal_Notes].toString() : QString();
        const QString metrics = withExtras ? metricsString(sess) : QString();
        const bool hasMetrics = !metrics.isEmpty();

        QString notesMd;
        if (!notesHtml.trimmed().isEmpty()) {
            QTextDocument doc;
            doc.setHtml(notesHtml);
            notesMd = doc.toMarkdown().trimmed();
        }
        const bool hasNotes = !notesMd.isEmpty();
        if (!hasNotes && !hasMetrics) continue;

        md += QStringLiteral("##### ") + locale.toString(date, QLocale::LongFormat)
            + QStringLiteral("\n\n");
        if (hasNotes) {
            md += notesMd + QStringLiteral("\n\n");
        }
        if (hasMetrics) {
            md += QStringLiteral("*") + metrics + QStringLiteral("*\n\n");
        }
        ++count;
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return -1;
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << md;
    return count;
}
