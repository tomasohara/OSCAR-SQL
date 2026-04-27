/* Journal Notes Export Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Modal dialog that exports journal notes for a date range to an HTML
 * or Markdown file.  Reads entirely from the database so no profile
 * needs to be open when the dialog is invoked.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef JOURNALNOTESDIALOG_H
#define JOURNALNOTESDIALOG_H

#include <QDate>
#include <QDialog>

namespace Ui {
class JournalNotesDialog;
}

/*!
 * \class JournalNotesDialog
 * \brief Exports journal notes for a selected date range to HTML or Markdown.
 *
 * Presents a profile selector, date-range picker, and format selector.
 * All data is read directly from the database so the profile does not need
 * to be open. Clicking Export opens a Save File dialog; the file is written
 * immediately.  Days with no content are silently skipped.
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
    /*! \brief Re-apply date range when the selected profile changes. */
    void on_profileCombo_currentIndexChanged(int index);

    /*! \brief Adjust From/To date edits when the range preset changes. */
    void on_rangeCombo_currentTextChanged(const QString& text);

    /*! \brief Open a Save File dialog and write the export. */
    void on_exportButton_clicked();

    /*! \brief Close the dialog. */
    void on_closeButton_clicked();

private:
    /*! \brief Populate profileCombo from active profiles, pre-selecting the open one. */
    void populateProfiles();

    /*! \brief Return the database ID of the currently selected profile, or -1 if none. */
    qint64 selectedProfileId() const;

    /*! \brief Return the username of the currently selected profile. */
    QString selectedUserName() const;

    /*! \brief Set fromDate/toDate from the named range preset. */
    void applyDateRange(const QString& rangeText);

    /*! \brief Apply locale-aware formatting and remove weekend red-highlight. */
    void setupCalendarFormatting();

    /*! \brief Persist the last-used directory and checkbox state to QSettings. */
    void saveSettings(const QString& dir);

    /*! \brief Restore settings from QSettings into member variables and checkboxes. */
    void loadSettings();

    /*!
     * \brief Query the earliest journal session date for the selected profile.
     * \return Valid QDate, or invalid QDate() if no journal data exists.
     */
    QDate getFirstJournalDate() const;

    /*!
     * \brief Query the most recent journal session date for the selected profile.
     * \return Valid QDate, or invalid QDate() if no journal data exists.
     */
    QDate getLastJournalDate() const;

    /*!
     * \brief Write an HTML file containing notes for dates in [start, end].
     * \param filename  Absolute path of the output file.
     * \param start     First date to include.
     * \param end       Last date to include.
     * \return Number of days written, or -1 on file error.
     */
    int exportToHtml(const QString& filename, QDate start, QDate end);

    /*!
     * \brief Write a Markdown file containing notes for dates in [start, end].
     * \param filename  Absolute path of the output file.
     * \param start     First date to include.
     * \param end       Last date to include.
     * \return Number of days written, or -1 on file error.
     */
    int exportToMarkdown(const QString& filename, QDate start, QDate end);

    /*!
     * \brief Build a metrics display string from raw DB values.
     * \param feelings  Raw 0–100 feelings value (0 = not set).
     * \param weight_kg Raw weight in kg (0 = not set).
     * \param zombieMode  True if feelings are displayed on a /100 scale.
     * \param metric      True if weight should be shown in kg, false for lbs.
     * \return Formatted string, or empty if neither value is set.
     */
    static QString metricsString(int feelings, double weight_kg,
                                 bool zombieMode, bool metric);

    /*!
     * \brief Extract the inner body content from a Qt QTextEdit HTML document.
     * \param html  Full HTML string from QTextEdit::toHtml().
     * \return Content between <body ...> and </body>, or \a html unchanged.
     */
    static QString extractBodyContent(const QString& html);

    Ui::JournalNotesDialog* ui;
    QList<qint64> m_profileIds;  ///< DB IDs parallel to profileCombo entries.
    QString       m_lastDir;     ///< Last-used output directory.
};

#endif // JOURNALNOTESDIALOG_H
