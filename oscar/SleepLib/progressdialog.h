/* SleepLib Progress Dialog Header
 *
 * Copyright (c) 2019-2026 The OSCAR Team
 * Copyright (C) 2011-2018 Mark Watkins 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PROGRESSDIALOG_H
#define PROGRESSDIALOG_H

#include <QCloseEvent>
#include <QDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

class ProgressDialog:public QDialog {
Q_OBJECT
public:
    explicit ProgressDialog(QWidget * parent);
    virtual ~ProgressDialog();

    void addAbortButton();

    /*! \brief Call before the programmatic close() at the end of a long operation
     *  so that closeEvent allows the dialog to close normally. Without this,
     *  closeEvent treats any close as a Cancel request and ignores it. */
    void allowClose() { m_allowClose = true; }

    void setPixmap(QPixmap &pixmap) { imglabel->setPixmap(pixmap); }
    QProgressBar * progress;
public slots:
    void setMessage(QString msg);
    void onAbortClicked();

    void setProgressMax(int max);
    void setProgressValue(int val);

signals:
    void abortClicked();
protected:
    void closeEvent(QCloseEvent* event) override;

    QLabel * statusMsg;
    QHBoxLayout *hlayout;
    QLabel * imglabel;
    QVBoxLayout * vlayout;
    QPushButton * abortButton;
    bool m_allowClose;

};

#endif // PROGRESSDIALOG_H
