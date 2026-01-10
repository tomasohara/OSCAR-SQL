/* Import Profile Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the ImportProfile dialog for importing profiles
 * from file-based OSCAR (OSCAR_Data) to SQL-based OSCAR 2.0 (OSCAR20_Data).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef IMPORTPROFILE_H
#define IMPORTPROFILE_H

#include <QDialog>

namespace Ui {
class ImportProfile;
}

class ImportProfile : public QDialog
{
    Q_OBJECT

public:
    explicit ImportProfile(QWidget *parent = nullptr);
    ~ImportProfile();
    
    QString selectedProfilePath() const { return m_selectedPath; }
    QString newProfileName() const { return m_newProfileName; }
    
private slots:
    void on_sourcePathButton_clicked();
    void on_importButton_clicked();
    void on_cancelButton_clicked();
    void on_profileName_textChanged(const QString &text);
    
private:
    Ui::ImportProfile *ui;
    QString m_selectedPath;
    QString m_newProfileName;
    QString m_lastImportPath;  // Remember last folder
    
    void loadSettings();
    void saveSettings();
    bool validateSelection();
    void updateStatus(const QString &message);
    qint64 calculateProfileSize(const QString &path);
    QString getDefaultImportPath();
};

#endif // IMPORTPROFILE_H
