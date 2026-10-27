/*
 * Copyright (C) 2015-2022 Département de l'Instruction Publique (DIP-SEM)
 *
 * Copyright (C) 2013 Open Education Foundation
 *
 * Copyright (C) 2010-2013 Groupement d'Intérêt Public pour
 * l'Education Numérique en Afrique (GIP ENA)
 *
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef UBSHAPERECOGNIZER_H
#define UBSHAPERECOGNIZER_H

#include <QtCore>
#include <QtGui>

struct UBShapeRecognizerParams {
    qreal minSize = 40;          // min bounding box diagonal, in scene units
    qreal lineMaxDeviation = 0.04;   // max perpendicular deviation / length
    qreal closeRatio = 0.2;      // start-end distance / path length to consider closed
    qreal circleMaxError = 0.08; // RMS radial error / radius
    qreal polygonEpsilon = 0.04; // RDP epsilon / perimeter
    qreal rectAngleSnap = 10;    // degrees
};

struct UBRecognizedShape {
    enum Type { None, Line, Circle, Ellipse, Triangle, Rectangle } type = None;
    QList<QPointF> points;   // already densified points (>= 48 for circle/ellipse), closed shapes repeat the first point at the end
};

class UBShapeRecognizer {
public:
    static UBRecognizedShape recognize(const QList<QPointF>& points, const UBShapeRecognizerParams& params = UBShapeRecognizerParams());
};

#endif // UBSHAPERECOGNIZER_H
