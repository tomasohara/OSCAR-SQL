/* BorrowingTimeEdit — QTimeEdit with cross-field borrow/carry on step
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef BORROWINGTIMEEDIT_H
#define BORROWINGTIMEEDIT_H

#include <QTimeEdit>
#include <QtMath>

/** QTimeEdit subclass that borrows/carries across H/M/S fields when stepping.
 *  Qt's built-in wrapping only wraps each section independently; this class
 *  converts to total seconds, applies the step, then converts back.
 *  When a step would go below zero, steppedPastZero(overshootSecs) is emitted
 *  so the parent can flip the sign widget and set the residual magnitude. */
class BorrowingTimeEdit : public QTimeEdit
{
    Q_OBJECT
public:
    explicit BorrowingTimeEdit(QWidget* parent = nullptr) : QTimeEdit(parent) {}

    /** When reversed, up/down steps are flipped so that "down" increases the
     *  magnitude. Set true whenever the sign button shows "−". */
    void setReversed(bool r) { m_reversed = r; }

    StepEnabled stepEnabled() const override { return StepUpEnabled | StepDownEnabled; }

    void stepBy(int steps) override
    {
        if (m_reversed) steps = -steps;

        // Use currentSectionIndex() rather than currentSection() to avoid the
        // Hour12Section vs Hour24Section ambiguity in Qt6: HourSection is
        // defined as Hour12Section (0x0010) but "HH" format reports Hour24Section
        // (0x0020), causing the hours field to fall through to default (1 sec).
        int multSecs;
        switch (currentSectionIndex()) {
            case 0: multSecs = 3600; break;  // HH
            case 1: multSecs = 60;   break;  // mm
            default: multSecs = 1;   break;  // ss
        }
        QTime t = time();
        int totalSecs = t.hour() * 3600 + t.minute() * 60 + t.second();
        int newSecs = totalSecs + steps * multSecs;

        if (newSecs < 0) {
            setTime(QTime(0, 0, 0));
            emit steppedPastZero(-newSecs);
            return;
        }

        newSecs = qMin(newSecs, 86399);
        setTime(QTime(newSecs / 3600, (newSecs % 3600) / 60, newSecs % 60));
    }

signals:
    /** Emitted when the user steps below zero; overshootSecs is the residual
     *  magnitude past zero. The parent should flip the sign and set this value. */
    void steppedPastZero(int overshootSecs);

private:
    bool m_reversed = false;
};

#endif // BORROWINGTIMEEDIT_H
