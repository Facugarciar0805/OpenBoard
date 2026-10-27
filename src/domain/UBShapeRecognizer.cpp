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

#include "UBShapeRecognizer.h"

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{

constexpr qreal kPi = 3.14159265358979323846;

qreal pointToLineDistance(const QPointF& p, const QPointF& a, const QPointF& b)
{
    qreal dx = b.x() - a.x();
    qreal dy = b.y() - a.y();
    qreal len = std::hypot(dx, dy);
    if (len <= 1e-9)
    {
        return std::hypot(p.x() - a.x(), p.y() - a.y());
    }
    return std::abs(dy * p.x() - dx * p.y() + b.x() * a.y() - b.y() * a.x()) / len;
}

qreal pointToSegmentDistance(const QPointF& p, const QPointF& a, const QPointF& b)
{
    qreal dx = b.x() - a.x();
    qreal dy = b.y() - a.y();
    qreal lenSq = dx * dx + dy * dy;
    if (lenSq <= 1e-9)
    {
        return std::hypot(p.x() - a.x(), p.y() - a.y());
    }

    qreal t = ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / lenSq;
    t = qBound(0.0, t, 1.0);
    QPointF proj(a.x() + t * dx, a.y() + t * dy);
    return std::hypot(p.x() - proj.x(), p.y() - proj.y());
}

void rdpRecursive(const QList<QPointF>& pts, int start, int end, qreal epsilon, QList<int>& keptIndices)
{
    if (end <= start + 1)
    {
        return;
    }

    qreal maxDist = 0.0;
    int maxIndex = -1;

    for (int i = start + 1; i < end; ++i)
    {
        qreal d = pointToLineDistance(pts[i], pts[start], pts[end]);
        if (d > maxDist)
        {
            maxDist = d;
            maxIndex = i;
        }
    }

    if (maxDist > epsilon && maxIndex != -1)
    {
        rdpRecursive(pts, start, maxIndex, epsilon, keptIndices);
        keptIndices.append(maxIndex);
        rdpRecursive(pts, maxIndex, end, epsilon, keptIndices);
    }
}

QList<QPointF> simplifyRDP(const QList<QPointF>& pts, qreal epsilon)
{
    if (pts.size() <= 2)
    {
        return pts;
    }

    QList<int> keptIndices;
    keptIndices.append(0);
    rdpRecursive(pts, 0, pts.size() - 1, epsilon, keptIndices);
    keptIndices.append(pts.size() - 1);

    QList<QPointF> result;
    result.reserve(keptIndices.size());
    for (int idx : keptIndices)
    {
        result.append(pts[idx]);
    }

    return result;
}

} // namespace

UBRecognizedShape UBShapeRecognizer::recognize(const QList<QPointF>& points, const UBShapeRecognizerParams& params)
{
    UBRecognizedShape shape;
    shape.type = UBRecognizedShape::None;

    if (points.size() < 5)
    {
        return shape;
    }

    qreal minX = points[0].x();
    qreal maxX = points[0].x();
    qreal minY = points[0].y();
    qreal maxY = points[0].y();
    qreal pathLength = 0.0;

    for (int i = 0; i < points.size(); ++i)
    {
        qreal x = points[i].x();
        qreal y = points[i].y();
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;

        if (i > 0)
        {
            pathLength += std::hypot(points[i].x() - points[i - 1].x(), points[i].y() - points[i - 1].y());
        }
    }

    QRectF bbox(minX, minY, maxX - minX, maxY - minY);
    qreal diag = std::hypot(bbox.width(), bbox.height());

    if (diag < params.minSize || pathLength <= 0.0)
    {
        return shape;
    }

    qreal chordDist = std::hypot(points.last().x() - points.first().x(), points.last().y() - points.first().y());
    bool isClosed = (chordDist / pathLength) <= params.closeRatio;

    if (!isClosed)
    {
        // Test Line candidate
        qreal maxDev = 0.0;
        for (const QPointF& p : points)
        {
            qreal dev = pointToLineDistance(p, points.first(), points.last());
            if (dev > maxDev)
            {
                maxDev = dev;
            }
        }

        if (chordDist > 0.0 && (maxDev / chordDist) <= params.lineMaxDeviation)
        {
            shape.type = UBRecognizedShape::Line;
            shape.points = { points.first(), points.last() };
            return shape;
        }

        return shape;
    }

    // Closed shape: test Circle, Ellipse, Triangle, Rectangle
    struct Candidate
    {
        UBRecognizedShape::Type type = UBRecognizedShape::None;
        QList<QPointF> points;
        qreal error = 1e9;
    };

    Candidate curvedCandidate;
    Candidate polygonCandidate;

    // 1. Circle fit via Kasa least-squares
    qreal sumX = 0.0;
    qreal sumY = 0.0;
    for (const QPointF& p : points)
    {
        sumX += p.x();
        sumY += p.y();
    }
    qreal meanX = sumX / points.size();
    qreal meanY = sumY / points.size();

    qreal Suu = 0.0;
    qreal Suv = 0.0;
    qreal Svv = 0.0;
    qreal Suz = 0.0;
    qreal Svz = 0.0;

    for (const QPointF& p : points)
    {
        qreal u = p.x() - meanX;
        qreal v = p.y() - meanY;
        qreal z = u * u + v * v;
        Suu += u * u;
        Suv += u * v;
        Svv += v * v;
        Suz += u * z;
        Svz += v * z;
    }

    qreal det = 2.0 * (Suu * Svv - Suv * Suv);
    bool circleOk = false;

    if (std::abs(det) > 1e-7)
    {
        qreal uc = (Svv * Suz - Suv * Svz) / det;
        qreal vc = (Suu * Svz - Suv * Suz) / det;
        qreal cx = meanX + uc;
        qreal cy = meanY + vc;

        qreal sumZ = 0.0;
        for (const QPointF& p : points)
        {
            qreal u = p.x() - meanX;
            qreal v = p.y() - meanY;
            sumZ += (u * u + v * v);
        }

        qreal rSq = uc * uc + vc * vc + (sumZ / points.size());
        if (rSq > 1e-4)
        {
            qreal R = std::sqrt(rSq);
            qreal sumSqErr = 0.0;
            for (const QPointF& p : points)
            {
                qreal d = std::hypot(p.x() - cx, p.y() - cy);
                qreal diff = d - R;
                sumSqErr += diff * diff;
            }
            qreal rmsErr = std::sqrt(sumSqErr / points.size());
            qreal circleErr = rmsErr / R;

            if (circleErr <= params.circleMaxError)
            {
                circleOk = true;
                curvedCandidate.type = UBRecognizedShape::Circle;
                qreal perim = 2.0 * kPi * R;
                curvedCandidate.error = (perim > 0.0) ? (rmsErr / perim) : circleErr;

                const int circleSamples = 64;
                for (int k = 0; k <= circleSamples; ++k)
                {
                    qreal angle = 2.0 * kPi * k / static_cast<qreal>(circleSamples);
                    curvedCandidate.points.append(QPointF(cx + R * std::cos(angle), cy + R * std::sin(angle)));
                }
            }
        }
    }

    // 2. Axis-aligned Ellipse fit (if circle did not match)
    if (!circleOk && bbox.width() > 1e-4 && bbox.height() > 1e-4)
    {
        qreal cx = bbox.center().x();
        qreal cy = bbox.center().y();
        qreal a = bbox.width() / 2.0;
        qreal b = bbox.height() / 2.0;

        qreal sumNormRadialErr = 0.0;
        qreal sumSqDiff = 0.0;
        for (const QPointF& p : points)
        {
            qreal dx = p.x() - cx;
            qreal dy = p.y() - cy;
            qreal theta = std::atan2(dy, dx);

            qreal denom = std::hypot(b * std::cos(theta), a * std::sin(theta));
            qreal rExpected = (denom > 1e-9) ? (a * b / denom) : 0.0;
            qreal rActual = std::hypot(dx, dy);
            qreal diff = std::abs(rActual - rExpected);
            sumSqDiff += diff * diff;

            if (rExpected > 1e-9)
            {
                sumNormRadialErr += diff / rExpected;
            }
            else
            {
                sumNormRadialErr += 1.0;
            }
        }

        qreal meanNormRadialErr = sumNormRadialErr / points.size();
        if (meanNormRadialErr <= params.circleMaxError)
        {
            curvedCandidate.type = UBRecognizedShape::Ellipse;
            qreal h = ((a - b) * (a - b)) / ((a + b) * (a + b));
            qreal perim = kPi * (a + b) * (1.0 + (3.0 * h) / (10.0 + std::sqrt(4.0 - 3.0 * h)));
            qreal rmsErr = std::sqrt(sumSqDiff / points.size());
            curvedCandidate.error = (perim > 0.0) ? (rmsErr / perim) : meanNormRadialErr;

            const int ellipseSamples = 64;
            for (int k = 0; k <= ellipseSamples; ++k)
            {
                qreal angle = 2.0 * kPi * k / static_cast<qreal>(ellipseSamples);
                curvedCandidate.points.append(QPointF(cx + a * std::cos(angle), cy + b * std::sin(angle)));
            }
        }
    }

    // 3. Polygon fitting via RDP
    QList<QPointF> closedPoints = points;
    if (std::hypot(closedPoints.first().x() - closedPoints.last().x(), closedPoints.first().y() - closedPoints.last().y()) > 1e-6)
    {
        closedPoints.append(closedPoints.first());
    }

    // Rotate points so that the start/end point is at an extreme corner
    int cornerIdx1 = 0;
    qreal maxDist1 = 0.0;
    for (int i = 0; i < closedPoints.size(); ++i)
    {
        qreal d = std::hypot(closedPoints[i].x() - closedPoints[0].x(), closedPoints[i].y() - closedPoints[0].y());
        if (d > maxDist1)
        {
            maxDist1 = d;
            cornerIdx1 = i;
        }
    }

    int cornerIdx2 = cornerIdx1;
    qreal maxDist2 = 0.0;
    for (int i = 0; i < closedPoints.size(); ++i)
    {
        qreal d = std::hypot(closedPoints[i].x() - closedPoints[cornerIdx1].x(), closedPoints[i].y() - closedPoints[cornerIdx1].y());
        if (d > maxDist2)
        {
            maxDist2 = d;
            cornerIdx2 = i;
        }
    }

    QList<QPointF> rotatedPoints;
    rotatedPoints.reserve(closedPoints.size());
    int nPoints = closedPoints.size() - 1; // excluding repeat of first
    for (int i = 0; i < nPoints; ++i)
    {
        rotatedPoints.append(closedPoints[(cornerIdx2 + i) % nPoints]);
    }
    rotatedPoints.append(rotatedPoints.first());

    qreal epsilon = params.polygonEpsilon * pathLength;
    QList<QPointF> rdpPoints = simplifyRDP(rotatedPoints, epsilon);

    // Filter adjacent points that are too close
    QList<QPointF> filteredVertices;
    for (int i = 0; i < rdpPoints.size() - 1; ++i)
    {
        if (filteredVertices.isEmpty() ||
            std::hypot(filteredVertices.last().x() - rdpPoints[i].x(), filteredVertices.last().y() - rdpPoints[i].y()) > epsilon * 0.75)
        {
            filteredVertices.append(rdpPoints[i]);
        }
    }
    if (!filteredVertices.isEmpty() &&
        std::hypot(filteredVertices.first().x() - filteredVertices.last().x(), filteredVertices.first().y() - filteredVertices.last().y()) <= epsilon * 0.75)
    {
        filteredVertices.removeLast();
    }

    int numCorners = filteredVertices.size();

    if (numCorners == 3)
    {
        // Triangle
        const QPointF& v0 = filteredVertices[0];
        const QPointF& v1 = filteredVertices[1];
        const QPointF& v2 = filteredVertices[2];

        qreal area = 0.5 * std::abs((v1.x() - v0.x()) * (v2.y() - v0.y()) - (v2.x() - v0.x()) * (v1.y() - v0.y()));
        qreal bboxArea = bbox.width() * bbox.height();

        if (bboxArea > 0.0 && (area / bboxArea) >= 0.05)
        {
            qreal perim = std::hypot(v1.x() - v0.x(), v1.y() - v0.y()) +
                          std::hypot(v2.x() - v1.x(), v2.y() - v1.y()) +
                          std::hypot(v0.x() - v2.x(), v0.y() - v2.y());

            if (perim > 0.0)
            {
                qreal sumSqDist = 0.0;
                for (const QPointF& p : points)
                {
                    qreal d0 = pointToSegmentDistance(p, v0, v1);
                    qreal d1 = pointToSegmentDistance(p, v1, v2);
                    qreal d2 = pointToSegmentDistance(p, v2, v0);
                    qreal minD = qMin(d0, qMin(d1, d2));
                    sumSqDist += minD * minD;
                }
                qreal rmsDist = std::sqrt(sumSqDist / points.size());
                qreal normError = rmsDist / perim;

                if (normError <= 0.08)
                {
                    polygonCandidate.type = UBRecognizedShape::Triangle;
                    polygonCandidate.error = normError;
                    polygonCandidate.points = { v0, v1, v2, v0 };
                }
            }
        }
    }
    else if (numCorners == 4)
    {
        // Rectangle
        const QPointF& v0 = filteredVertices[0];
        const QPointF& v1 = filteredVertices[1];
        const QPointF& v2 = filteredVertices[2];
        const QPointF& v3 = filteredVertices[3];

        bool anglesOk = true;
        qreal sumAngleDev = 0.0;

        for (int j = 0; j < 4; ++j)
        {
            const QPointF& pPrev = filteredVertices[(j + 3) % 4];
            const QPointF& pCurr = filteredVertices[j];
            const QPointF& pNext = filteredVertices[(j + 1) % 4];

            qreal uX = pPrev.x() - pCurr.x();
            qreal uY = pPrev.y() - pCurr.y();
            qreal vX = pNext.x() - pCurr.x();
            qreal vY = pNext.y() - pCurr.y();

            qreal lenU = std::hypot(uX, uY);
            qreal lenV = std::hypot(vX, vY);

            if (lenU < 1e-6 || lenV < 1e-6)
            {
                anglesOk = false;
                break;
            }

            qreal cosTheta = qBound(-1.0, (uX * vX + uY * vY) / (lenU * lenV), 1.0);
            qreal angleDeg = std::acos(cosTheta) * 180.0 / kPi;

            qreal dev = std::abs(angleDeg - 90.0);
            if (dev > 25.0)
            {
                anglesOk = false;
                break;
            }
            sumAngleDev += dev;
        }

        if (anglesOk && (sumAngleDev / 4.0) <= 18.0)
        {
            QList<QPointF> rectPts;
            qreal edgeAngle = QLineF(v0, v1).angle();
            qreal mod90 = std::fmod(edgeAngle, 90.0);
            if (mod90 < 0.0)
            {
                mod90 += 90.0;
            }
            qreal angleFromAxis = qMin(mod90, 90.0 - mod90);

            if (angleFromAxis <= params.rectAngleSnap)
            {
                rectPts = { bbox.topLeft(), bbox.topRight(), bbox.bottomRight(), bbox.bottomLeft(), bbox.topLeft() };
            }
            else
            {
                rectPts = { v0, v1, v2, v3, v0 };
            }

            qreal perim = 0.0;
            for (int k = 0; k < rectPts.size() - 1; ++k)
            {
                perim += std::hypot(rectPts[k + 1].x() - rectPts[k].x(), rectPts[k + 1].y() - rectPts[k].y());
            }

            if (perim > 0.0)
            {
                qreal sumSqDist = 0.0;
                for (const QPointF& p : points)
                {
                    qreal minD = 1e9;
                    for (int k = 0; k < rectPts.size() - 1; ++k)
                    {
                        qreal d = pointToSegmentDistance(p, rectPts[k], rectPts[k + 1]);
                        if (d < minD)
                        {
                            minD = d;
                        }
                    }
                    sumSqDist += minD * minD;
                }

                qreal rmsDist = std::sqrt(sumSqDist / points.size());
                qreal normError = rmsDist / perim;

                if (normError <= 0.08)
                {
                    polygonCandidate.type = UBRecognizedShape::Rectangle;
                    polygonCandidate.error = normError;
                    polygonCandidate.points = rectPts;
                }
            }
        }
    }

    // Pick best candidate with lowest normalized error
    if (curvedCandidate.type != UBRecognizedShape::None && polygonCandidate.type != UBRecognizedShape::None)
    {
        if (curvedCandidate.error <= polygonCandidate.error)
        {
            shape.type = curvedCandidate.type;
            shape.points = curvedCandidate.points;
        }
        else
        {
            shape.type = polygonCandidate.type;
            shape.points = polygonCandidate.points;
        }
    }
    else if (curvedCandidate.type != UBRecognizedShape::None)
    {
        shape.type = curvedCandidate.type;
        shape.points = curvedCandidate.points;
    }
    else if (polygonCandidate.type != UBRecognizedShape::None)
    {
        shape.type = polygonCandidate.type;
        shape.points = polygonCandidate.points;
    }

    return shape;
}
