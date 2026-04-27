/* Journal Notes Export Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Modal dialog that exports journal notes for a date range to an HTML
 * or Markdown file.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef JOURNALNOTESDIALOG_H
#define JOURNALNOTESDIALOG_H

#include <QDate>
#include <QDialog>

class Session;

namespace Ui {
class JournalNotesDialog;
}

/*!
 * \class JournalNotesDialog
 * \brief Exports journal notes for a selected date range to HTML or Markdown.
 *
 * Presents a date-range picker (mirroring BackupDialog) and a format selector.
 * Clicking Export opens a Save File dialog; the file is written immediately.
 * Days with no notes are silently skipped.
 *
 * Usage:
 * \code
 * JournalNotesDialog dlg(this);
 * dlg.exec();
 * \endcode
 */
class JournalNotesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit JournalNotesDialog(QWidget* parent = nullptr);
    ~JournalNotesDialog() override;

private slots:
    /*! \brief Adjust From/To date edits when the range preset changes. */
    void on_rangeCombo_currentTextChanged(const QString& text);

    /*! \brief Open a Save File dialog and write the export. */
    void on_exportButton_clicked();

    /*! \brief Close the dialog. */
    void on_closeButton_clicked();

private:
    /*! \brief Set fromDate/toDate from the named range preset. */
    void applyDateRange(const QString& rangeText);

    /*! \brief Apply locale-aware formatting and remove weekend red-highlight. */
    void setupCalendarFormatting();

    /*! \brief Persist the last-used directory and checkbox state to QSettings. */
    void saveSettings(const QString& dir);

    /*! \brief Restore settings from QSettings into member variables and checkboxes. */
    void loadSettings();

    /*!
     * \brief Query the earliest date that has a journal session for this profile.
     * \return Earliest journal date, or today as a fallback.
     */
    QDate getFirstJournalDate() const;

    /*!
     * \brief Query the most recent date that has a journal session for this profile.
     * \return Latest journal date, or today as a fallback.
     */
    QDate getLastJournalDate() const;

    /*!
     * \brief Write an HTML file containing notes for dates in [start, end].
     * \param filename  Absolute path of the output file.
     * \param start     First date to include.
     * \param end       Last date to include.
     * \return Number of days with notes written, or -1 on file error.
     */
    int exportToHtml(const QString& filename, QDate start, QDate end);

    /*!
     * \brief Write a Markdown file containing notes for dates in [start, end].
     * \param filename  Absolute path of the output file.
     * \param start     First date to include.
     * \param end       Last date to include.
     * \return Number of days with notes written, or -1 on file error.
     */
    int exportToMarkdown(const QString& filename, QDate start, QDate end);

    /*!
     * \brief Build a display string for feelings and weight from a journal session.
     * \return Formatted string, e.g. "Feelings: 7.5/10  Weight: 82.5 kg", or empty if neither is set.
     */
    QString metricsString(Session* sess) const;

    /*!
     * \brief Extract the inner body content from a Qt QTextEdit HTML document.
     *
     * Qt's QTextEdit::toHtml() produces a full HTML document.  This helper
     * strips the outer <html>/<head>/<body> wrapper so the content can be
     * embedded inside a composite export document.
     *
     * \param html  Full HTML string from QTextEdit::toHtml().
     * \return Content between <body ...> and </body>, or \a html unchanged if
     *         the tags cannot be found.
     */
    static QString extractBodyContent(const QString& html);

    Ui::JournalNotesDialog* ui;
    QString m_lastDir;  ///< Last-used output directory, loaded once at construction.
};

#endif // JOURNALNOTESDIALOG_H
