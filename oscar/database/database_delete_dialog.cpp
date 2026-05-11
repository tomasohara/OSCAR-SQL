/* DatabaseDeleteDialog — dialog for deleting a non-active OSCAR database
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "database_delete_dialog.h"
#include "recent_databases.h"
#include <QDir>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QDirIterator>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QDebug>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

DatabaseDeleteDialog::DatabaseDeleteDialog(const QStringList& candidates,
                                           const QString& activePath,
                                           QWidget* parent)
    : QDialog(parent)
    , m_candidates(candidates)
    , m_activePath(RecentDatabases::canonicalize(activePath))
{
    setWindowTitle(tr("Delete Database"));
    setMinimumWidth(500);

    auto* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(tr("Select a database to delete. This cannot be undone.")));

    m_list = new QListWidget(this);
    for (const QString& path : candidates) {
        QListWidgetItem* item = new QListWidgetItem(QFileInfo(path).fileName(), m_list);
        item->setToolTip(path);
    }
    layout->addWidget(m_list);

    m_detailLabel = new QLabel(this);
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setText(tr("Select a database above to see details."));
    layout->addWidget(m_detailLabel);

    auto* buttonLayout = new QHBoxLayout();
    m_deleteButton = new QPushButton(tr("Delete..."), this);
    m_deleteButton->setEnabled(false);
    auto* closeButton = new QPushButton(tr("Close"), this);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_deleteButton);
    buttonLayout->addWidget(closeButton);
    layout->addLayout(buttonLayout);

    connect(m_list, &QListWidget::itemSelectionChanged, this, &DatabaseDeleteDialog::onSelectionChanged);
    connect(m_deleteButton, &QPushButton::clicked, this, &DatabaseDeleteDialog::onDeleteClicked);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void DatabaseDeleteDialog::onSelectionChanged()
{
    QList<QListWidgetItem*> sel = m_list->selectedItems();
    if (sel.isEmpty()) {
        m_detailLabel->setText(tr("Select a database above to see details."));
        m_deleteButton->setEnabled(false);
        return;
    }

    int row = m_list->row(sel.first());
    const QString& path = m_candidates.at(row);

    qint64 bytes = dirSize(path);
    QString sizeStr;
    if (bytes < 1024 * 1024)
        sizeStr = tr("%1 KB").arg(bytes / 1024);
    else
        sizeStr = tr("%1 MB").arg(bytes / (1024 * 1024));

    int profiles = profileCount(path);
    m_detailLabel->setText(tr("Path: %1\nSize: %2\nProfiles: %3\n"
                               "Note: all session data and any SD card backup data in this folder will be permanently deleted.")
                               .arg(path, sizeStr, QString::number(profiles)));
    m_deleteButton->setEnabled(true);
}

void DatabaseDeleteDialog::onDeleteClicked()
{
    QList<QListWidgetItem*> sel = m_list->selectedItems();
    if (sel.isEmpty())
        return;

    int row = m_list->row(sel.first());
    const QString path = m_candidates.at(row);   // value copy
    QString normPath = QDir::cleanPath(path);
    QString folderName = QFileInfo(path).fileName();

    qDebug() << "DatabaseDeleteDialog: delete requested for" << path
             << "active=" << m_activePath;

    // Belt-and-suspenders: never delete the active database or any of its
    // parent directories (deleting a parent would wipe the active DB too).
    if (normPath.compare(m_activePath, Qt::CaseInsensitive) == 0 ||
        m_activePath.startsWith(normPath + "/", Qt::CaseInsensitive)) {
        qDebug() << "DatabaseDeleteDialog: blocked — path is active or parent of active";
        QMessageBox::warning(this, tr("Cannot Delete"),
            tr("The selected folder contains the currently open OSCAR database "
               "and cannot be deleted."));
        return;
    }

    // Try to open the database file exclusively to confirm no other instance has it open.
    QFile dbFile(path + "/oscar.db");
    if (dbFile.open(QFile::ReadWrite)) {
        dbFile.close();
    } else {
        qDebug() << "DatabaseDeleteDialog: cannot open oscar.db exclusively";
        QMessageBox::warning(this, tr("Cannot Delete"),
            tr("The database at\n%1\ncannot be opened exclusively. "
               "Another instance of OSCAR may have it open.").arg(path));
        return;
    }

    bool ok;
    QString input = QInputDialog::getText(this, tr("Confirm Deletion"),
        tr("Type \"%1\" to permanently delete this database.\n\n"
           "All session data and SD card backup data in the folder will be deleted. "
           "This cannot be undone.").arg(folderName),
        QLineEdit::Normal, QString(), &ok);

    if (!ok || input != folderName) {
        qDebug() << "DatabaseDeleteDialog: deletion cancelled";
        return;
    }

    // Run the deletion on a background thread so the UI stays responsive.
    // Show a "please wait" dialog only if the operation takes more than 2 seconds
    // (fast on local SSD, potentially slow on NAS).
    QFuture<bool> future = QtConcurrent::run([path]() {
        return QDir(path).removeRecursively();
    });

    auto* busyDialog = new QDialog(this,
        Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
    busyDialog->setWindowTitle(tr("Deleting Database"));
    busyDialog->setWindowModality(Qt::WindowModal);
    auto* busyLayout = new QVBoxLayout(busyDialog);
    auto* busyLabel = new QLabel(
        tr("Deleting \"%1\", please wait…").arg(folderName), busyDialog);
    busyLabel->setAlignment(Qt::AlignCenter);
    busyLabel->setContentsMargins(20, 10, 20, 10);
    busyLayout->addWidget(busyLabel);

    QTimer showTimer;
    showTimer.setSingleShot(true);
    connect(&showTimer, &QTimer::timeout, busyDialog, [busyDialog]() {
        busyDialog->adjustSize();
        busyDialog->show();
        busyDialog->raise();
    });
    showTimer.start(2000);

    QFutureWatcher<bool> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    loop.exec();

    showTimer.stop();
    busyDialog->hide();
    delete busyDialog;

    bool removed = future.result();
    qDebug() << "DatabaseDeleteDialog: removeRecursively returned" << removed;

    if (!removed) {
        if (QFileInfo::exists(path + "/oscar.db")) {
            // Database is still intact — don't remove it from the list.
            qDebug() << "DatabaseDeleteDialog: delete failed, oscar.db still present";
            QMessageBox::warning(this, tr("Delete Failed"),
                tr("Could not delete\n%1\n"
                   "The database file is still present. "
                   "Check for open file locks and try again.").arg(path));
            return;
        }
        // Partial delete: oscar.db is gone but some files remain.
        qDebug() << "DatabaseDeleteDialog: partial delete, oscar.db gone";
        QMessageBox::warning(this, tr("Partial Delete"),
            tr("Deleted the database but could not remove all files from\n%1\n"
               "Some files may still be present.").arg(path));
    }

    // Remove from recent list — database is gone (or sufficiently gone).
    QSettings settings;
    QStringList recent = settings.value("RecentDatabases").toStringList();
    recent.removeAll(path);
    settings.setValue("RecentDatabases", recent);

    // Disconnect during cleanup: takeItem() fires itemSelectionChanged while
    // the list count hasn't decremented yet, but m_candidates was already
    // shrunk by removeAt — the resulting row lookup would be out of bounds.
    disconnect(m_list, &QListWidget::itemSelectionChanged,
               this, &DatabaseDeleteDialog::onSelectionChanged);

    m_candidates.removeAt(row);
    delete m_list->takeItem(row);

    // Reconnect and refresh — list and candidates are back in sync.
    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &DatabaseDeleteDialog::onSelectionChanged);
    onSelectionChanged();

    qDebug() << "DatabaseDeleteDialog: deletion complete";
    QMessageBox::information(this, tr("Database Deleted"),
        tr("The database \"%1\" has been deleted.").arg(folderName));
    accept();
}

qint64 DatabaseDeleteDialog::dirSize(const QString& path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

int DatabaseDeleteDialog::profileCount(const QString& path)
{
    QDir profilesDir(path + "/Profiles");
    if (!profilesDir.exists())
        return 0;
    return (int)profilesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();
}
