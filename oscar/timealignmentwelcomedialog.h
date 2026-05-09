/* Time Alignment Welcome Dialog Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef TIMEALIGNMENTWELCOMEDIALOG_H
#define TIMEALIGNMENTWELCOMEDIALOG_H

#include <QDialog>

class QCheckBox;

/*!
 * \class TimeAlignmentWelcomeDialog
 * \brief Splash/welcome dialog shown before the Time Corrections dialog.
 *
 * Explains the correct order for applying device time corrections.
 * Includes an optional "Don't show this again" checkbox whose state is
 * persisted per-profile via UserSettings.
 */
class TimeAlignmentWelcomeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TimeAlignmentWelcomeDialog(QWidget *parent = nullptr);

private slots:
    void onDontShowAgainToggled(bool checked);

private:
    QCheckBox *m_dontShowAgain;
};

#endif // TIMEALIGNMENTWELCOMEDIALOG_H
