/* Drift Plot Widget Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef DRIFTPLOTWIDGET_H
#define DRIFTPLOTWIDGET_H

#include <QWidget>
#include <QDate>
#include <QList>

class DriftPlotWidget : public QWidget
{
    Q_OBJECT
public:
    struct Point { QDate date; double offsetMs; };

    explicit DriftPlotWidget(QWidget *parent = nullptr);

    void setData(const QList<Point>& points);
    void setModel(double c0Ms, double slope);   // predicted = c0Ms + slope * t_noon_ms
    void clearModel();
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QList<Point> m_points;
    bool   m_hasModel = false;
    double m_c0Ms     = 0.0;
    double m_slope    = 0.0;   // = c1 - 1.0

    static double tNoon(QDate d);
};

#endif // DRIFTPLOTWIDGET_H
