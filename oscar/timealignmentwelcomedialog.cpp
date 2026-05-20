/* Time Alignment Welcome Dialog Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "timealignmentwelcomedialog.h"
#include "SleepLib/profiles.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHBoxLayout>

TimeAlignmentWelcomeDialog::TimeAlignmentWelcomeDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Time Alignment Instructions"));
    setMinimumWidth(620);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 12);

    // --- Instructions text ---
    auto *text = new QTextBrowser(this);
    text->setOpenExternalLinks(false);
    text->setReadOnly(true);
    text->setFrameStyle(QFrame::NoFrame);
    text->setHtml(tr(
        "<h3>Time Alignment Instructions</h3>"
        "<p>All devices should be aligned to the CPAP timeline as the reference. "
        "Apply corrections in this order:</p>"
        "<ol>"
        "<li><b>Correct the CPAP device first</b> &mdash; fix any timezone misconfigurations, "
        "DST offsets, or travel adjustments.</li>"
        "<li><b>Adjust other devices</b> &mdash; apply the same DST or travel corrections to "
        "any additional devices.</li>"
        "<li><b>Fine-tune alignment</b> &mdash; apply offsets to bring all devices into sync "
        "with the CPAP data.</li>"
        "</ol>"
        "<p>For precise alignment, use short-duration events as reference points. The most "
        "reliable markers are those associated with physical movement, such as sudden irregular "
        "changes in flow rate, sleep stage transitions, and movement spikes. An example of "
        "these events is shown below.</p>"
    ));
    text->setMinimumHeight(180);
    mainLayout->addWidget(text);

    // --- Example image ---
    QPixmap pixmap(":/icons/time_alignment_example.png");
    if (!pixmap.isNull()) {
        auto *imageLabel = new QLabel(this);
        imageLabel->setAlignment(Qt::AlignCenter);
        // Scale to fit dialog width while preserving aspect ratio
        imageLabel->setPixmap(pixmap.scaledToWidth(580, Qt::SmoothTransformation));
        mainLayout->addWidget(imageLabel);
    } else {
        auto *placeholder = new QLabel(
            tr("[Place time_alignment_example.png in oscar/icons/ to display the example image here]"),
            this);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setMinimumHeight(80);
        placeholder->setStyleSheet("border: 1px dashed #888; color: #888; padding: 8px;");
        mainLayout->addWidget(placeholder);
    }

    // --- Don't show again checkbox ---
    m_dontShowAgain = new QCheckBox(tr("Don't show this again"), this);
    if (p_profile) {
        m_dontShowAgain->setChecked(p_profile->general->skipTimeAlignWelcome());
    }
    connect(m_dontShowAgain, &QCheckBox::toggled, this, &TimeAlignmentWelcomeDialog::onDontShowAgainToggled);

    // --- Continue button ---
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Continue"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->addWidget(m_dontShowAgain);
    bottomRow->addStretch();
    bottomRow->addWidget(buttons);

    mainLayout->addLayout(bottomRow);
}

void TimeAlignmentWelcomeDialog::onDontShowAgainToggled(bool checked)
{
    if (p_profile) {
        p_profile->general->setSkipTimeAlignWelcome(checked);
    }
}
