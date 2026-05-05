/* Drift Plot Widget Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "driftplotwidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QtMath>
#include <QDateTime>

DriftPlotWidget::DriftPlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
}

double DriftPlotWidget::tNoon(QDate d)
{
    return QDateTime(d, QTime(12, 0, 0), Qt::UTC).toMSecsSinceEpoch();
}

void DriftPlotWidget::setData(const QList<Point>& points)
{
    m_points   = points;
    m_hasModel = false;
    update();
}

void DriftPlotWidget::setModel(double c0Ms, double slope)
{
    m_c0Ms     = c0Ms;
    m_slope    = slope;
    m_hasModel = true;
    update();
}

void DriftPlotWidget::clearModel()
{
    m_hasModel = false;
    update();
}

void DriftPlotWidget::clear()
{
    m_points.clear();
    m_hasModel = false;
    update();
}

void DriftPlotWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int ml = 58, mr = 12, mt = 10, mb = 40;
    QRect plot(ml, mt, width() - ml - mr, height() - mt - mb);

    p.fillRect(rect(), palette().window());

    if (m_points.isEmpty()) {
        p.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        p.drawText(rect(), Qt::AlignCenter, tr("No data loaded"));
        return;
    }

    // --- Compute bounds ---
    double minY = m_points[0].offsetMs, maxY = m_points[0].offsetMs;
    QDate  minD = m_points[0].date,     maxD = m_points[0].date;
    for (const auto& pt : m_points) {
        minY = qMin(minY, pt.offsetMs);
        maxY = qMax(maxY, pt.offsetMs);
        if (pt.date < minD) minD = pt.date;
        if (pt.date > maxD) maxD = pt.date;
    }
    if (m_hasModel) {
        double y0 = m_c0Ms + m_slope * tNoon(minD);
        double y1 = m_c0Ms + m_slope * tNoon(maxD);
        minY = qMin(minY, qMin(y0, y1));
        maxY = qMax(maxY, qMax(y0, y1));
    }
    double yRange = maxY - minY;
    if (yRange < 200.0) { double mid = (minY + maxY) / 2.0; minY = mid - 100; maxY = mid + 100; yRange = 200; }
    minY -= yRange * 0.12;
    maxY += yRange * 0.12;

    int days = minD.daysTo(maxD);
    if (days < 1) days = 1;

    auto toX = [&](QDate d) -> double {
        return plot.left() + (double)minD.daysTo(d) / days * plot.width();
    };
    auto toY = [&](double ms) -> double {
        return plot.bottom() - (ms - minY) / (maxY - minY) * plot.height();
    };

    // --- Background & border ---
    p.fillRect(plot, Qt::white);
    p.setPen(QPen(QColor(180, 180, 180), 1));
    p.drawRect(plot);

    QFont tickFont = font();
    tickFont.setPointSize(qMax(7, font().pointSize() - 2));
    p.setFont(tickFont);

    // --- Y grid + labels ---
    // Pick a nice tick interval in ms
    double ySpan = maxY - minY;
    double rawStep = ySpan / 5.0;
    double mag = qPow(10.0, qFloor(qLn(rawStep) / qLn(10.0)));
    double step = mag;
    if (rawStep / mag > 5.0)       step = mag * 10;
    else if (rawStep / mag > 2.0)  step = mag * 5;
    else if (rawStep / mag > 1.0)  step = mag * 2;
    double firstTick = qCeil(minY / step) * step;

    for (double ms = firstTick; ms <= maxY; ms += step) {
        int y = (int)toY(ms);
        if (y < plot.top() || y > plot.bottom()) continue;
        p.setPen(QPen(QColor(220, 220, 220), 1));
        p.drawLine(plot.left(), y, plot.right(), y);
        p.setPen(QColor(80, 80, 80));
        QString label = QString::number((int)qRound(ms));
        p.drawText(0, y - 10, ml - 5, 20, Qt::AlignRight | Qt::AlignVCenter, label);
    }

    // Zero line
    if (minY < 0 && maxY > 0) {
        p.setPen(QPen(QColor(160, 160, 160), 1, Qt::DashLine));
        p.drawLine(plot.left(), (int)toY(0), plot.right(), (int)toY(0));
    }

    // --- X grid + labels ---
    int nXTicks = qMin(days, 8);
    for (int i = 0; i <= nXTicks; ++i) {
        QDate d = minD.addDays((int)qRound((double)days * i / nXTicks));
        int x = (int)toX(d);
        if (x < plot.left() || x > plot.right()) continue;
        p.setPen(QPen(QColor(220, 220, 220), 1));
        p.drawLine(x, plot.top(), x, plot.bottom());
        p.setPen(QColor(80, 80, 80));
        QRect lr(x - 28, plot.bottom() + 4, 56, 18);
        p.drawText(lr, Qt::AlignCenter, d.toString("MM-dd"));
    }

    // --- Model line ---
    if (m_hasModel) {
        double x0 = toX(minD), y0 = toY(m_c0Ms + m_slope * tNoon(minD));
        double x1 = toX(maxD), y1 = toY(m_c0Ms + m_slope * tNoon(maxD));
        p.setPen(QPen(QColor(210, 60, 60), 2));
        p.drawLine(QPointF(x0, y0), QPointF(x1, y1));
    }

    // --- Scatter points ---
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(50, 110, 200));
    for (const auto& pt : m_points) {
        int x = (int)toX(pt.date);
        int y = (int)toY(pt.offsetMs);
        p.drawEllipse(QPoint(x, y), 4, 4);
    }

    // --- Axis title (Y) ---
    p.setPen(QColor(80, 80, 80));
    p.save();
    p.translate(11, plot.top() + plot.height() / 2);
    p.rotate(-90);
    QFont axFont = font();
    axFont.setPointSize(qMax(7, font().pointSize() - 1));
    p.setFont(axFont);
    p.drawText(-40, -7, 80, 14, Qt::AlignCenter, tr("Offset (ms)"));
    p.restore();
}
