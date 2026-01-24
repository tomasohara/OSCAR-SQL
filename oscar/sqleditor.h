/* SQL Editor Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef SQLEDITOR_H
#define SQLEDITOR_H

#include <QDialog>

namespace Ui {
class SQLEditor;
}

/*!
 * \class SQLEditor
 * \brief Dialog for editing SQL queries before export
 * 
 * This dialog allows users to view and modify the SQL query that will be
 * used to generate CSV export data. It provides syntax highlighting and
 * basic validation.
 */
class SQLEditor : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \brief Constructor
     * \param parent Parent widget
     */
    explicit SQLEditor(QWidget *parent = nullptr);
    
    /*!
     * \brief Destructor
     */
    ~SQLEditor();

    /*!
     * \brief Set the SQL query text to edit
     * \param query SQL query string
     */
    void setQuery(const QString &query);
    
    /*!
     * \brief Get the edited SQL query
     * \return Edited SQL query string
     */
    QString getQuery() const;

private slots:
    /*!
     * \brief Handle OK button click
     */
    void on_okButton_clicked();
    
    /*!
     * \brief Handle Cancel button click
     */
    void on_cancelButton_clicked();

private:
    Ui::SQLEditor *ui;
};

#endif // SQLEDITOR_H
