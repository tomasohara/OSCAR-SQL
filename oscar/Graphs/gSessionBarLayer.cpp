/* gSessionBarLayer Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "gSessionBarLayer.h"
#include "gGraph.h"
#include "SleepLib/profiles.h"

gSessionBarLayer::gSessionBarLayer()
    : Layer(NoChannel)
{
}

void gSessionBarLayer::SetDay(Day *d)
{
    Layer::SetDay(d);
    m_sessions.clear();
    if (d) {
        m_sessions = d->getSessions(MT_CPAP);
    }
}

/*! \brief Draws a 7-pixel gray bar segment at the graph top for each CPAP session.
    Only active when the EventFlagSessionBar preference is on and there is more
    than one session in the day.  Coordinates are mapped to the current view
    bounds so the bar follows zoom and pan correctly.
    */
void gSessionBarLayer::paint(QPainter &painter, gGraph &w, const QRegion &region)
{
    if (!m_visible) return;
    if (!m_day) return;
    if (!p_profile->appearance->eventFlagSessionBar()) return;
    if (m_sessions.size() <= 1) return;

    const QRect bounds = region.boundingRect();
    const int left  = bounds.left() + 1;
    const int top   = bounds.top()  + 1;
    const int width = bounds.width();

    const double minx  = w.min_x;
    const double maxx  = w.max_x;
    const double xx    = maxx - minx;
    if (xx <= 0) return;
    const double xmult = width / xx;

    for (const auto &sess : m_sessions) {
        double x1 = (sess->first() - minx) * xmult + left;
        double x2 = (sess->last()  - minx) * xmult + left;
        if (x2 < left || x1 > left + width) continue;
        x1 = qMax(x1, static_cast<double>(left));
        x2 = qMin(x2, static_cast<double>(left + width));
        painter.fillRect(QRectF(x1, top, x2 - x1, barHeight), Qt::gray);
    }
}
