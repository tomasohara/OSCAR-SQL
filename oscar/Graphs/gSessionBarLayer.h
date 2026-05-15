/* gSessionBarLayer Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef GSESSIONBARLAYER_H
#define GSESSIONBARLAYER_H

#include "Graphs/layer.h"

/*! \class gSessionBarLayer
    \brief Draws a thin gray session-boundary bar at the top of a graph when
    multiple CPAP sessions exist in the day and the EventFlagSessionBar preference
    is enabled.  Mirrors the bar drawn by gFlagsGroup on the Event Flags graph.
    The bar respects the current view zoom (uses w.min_x / w.max_x).
    */
class gSessionBarLayer : public Layer
{
public:
    gSessionBarLayer();
    virtual ~gSessionBarLayer() {}

    //! \brief Populates the CPAP session list for the given day.
    virtual void SetDay(Day *d);

    //! \brief Draws the gray session bars along the top of the graph region.
    virtual void paint(QPainter &painter, gGraph &w, const QRegion &region);

    virtual EventDataType Miny() { return 0; }
    virtual EventDataType Maxy() { return 0; }

    virtual Layer * Clone() {
        gSessionBarLayer *l = new gSessionBarLayer();
        Layer::CloneInto(l);
        return l;
    }

protected:
    QList<Session *> m_sessions;
    static const int barHeight = 7;
};

#endif // GSESSIONBARLAYER_H
