/* DatabaseDeleteDialog — dialog for deleting a non-active OSCAR database
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DATABASE_DELETE_DIALOG_H
#define DATABASE_DELETE_DIALOG_H

#include <QDialog>
#include <QStringList>

class QListWidget;
class QLabel;
class QPushButton;

/*!
 * \class DatabaseDeleteDialog
 * \brief Dialog that lets the user select and permanently delete a non-active
 *        OSCAR database folder (oscar.db + all companion data).
 *
 * The active database is excluded from the list. The user must type the
 * folder's leaf name to confirm deletion before proceeding.
 */
class DatabaseDeleteDialog : public QDialog
{
    Q_OBJECT
public:
    /*!
     * \param candidates Absolute paths of databases eligible for deletion
     *                   (active path should already be excluded by caller).
     * \param activePath Absolute path of the currently open database (belt-and-suspenders guard).
     * \param parent     Parent widget.
     */
    explicit DatabaseDeleteDialog(const QStringList& candidates, const QString& activePath,
                                  QWidget* parent = nullptr);

private slots:
    void onSelectionChanged();
    void onDeleteClicked();

private:
    /*! \brief Calculate total size in bytes of a directory tree. */
    static qint64 dirSize(const QString& path);

    /*! \brief Count subdirectories of <path>/Profiles/. */
    static int profileCount(const QString& path);

    QListWidget*  m_list;
    QLabel*       m_detailLabel;
    QPushButton*  m_deleteButton;

    QStringList   m_candidates;
    QString       m_activePath;
};

#endif // DATABASE_DELETE_DIALOG_H
