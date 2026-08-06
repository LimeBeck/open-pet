#include "inputregion.h"

#include <QVector>
#include <algorithm>
#include <cmath>

namespace {

bool blockIsOpaque(const QImage &image, int x0, int y0, int block, int threshold)
{
    const int xEnd = std::min(x0 + block, image.width());
    const int yEnd = std::min(y0 + block, image.height());
    for (int y = y0; y < yEnd; ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = x0; x < xEnd; ++x) {
            // Формат гарантирован вызывающей стороной: ARGB32, альфа — старший байт.
            const uchar alpha = line[x * 4 + 3];
            if (alpha >= threshold)
                return true;
        }
    }
    return false;
}

} // namespace

QRegion regionFromAlpha(const QImage &source, int block, int alphaThreshold, qreal scale)
{
    if (source.isNull())
        return QRegion();

    const QImage image = source.format() == QImage::Format_ARGB32
        ? source
        : source.convertToFormat(QImage::Format_ARGB32);

    block = std::max(1, block);
    const int width = image.width();
    const int height = image.height();

    // QRegion::operator+= на каждый прямоугольник заметно дороже, чем setRects
    // на готовом массиве, а прямоугольников тут сотни. Собираем список сами.
    QVector<QRect> rects;

    for (int y = 0; y < height; y += block) {
        const int rowHeight = std::min(block, height - y);
        int runStart = -1;

        for (int x = 0; x <= width; x += block) {
            const bool opaque = x < width && blockIsOpaque(image, x, y, block, alphaThreshold);

            if (opaque && runStart < 0) {
                runStart = x;
            } else if (!opaque && runStart >= 0) {
                rects.append(QRect(runStart, y, x - runStart, rowHeight));
                runStart = -1;
            }
        }
    }

    if (rects.isEmpty())
        return QRegion();

    if (scale > 0 && !qFuzzyCompare(scale, qreal(1))) {
        for (QRect &rect : rects) {
            // Округление наружу: лучше поймать лишний пиксель по краю силуэта,
            // чем оставить дыру, в которую проваливается клик по питомцу.
            const int left = int(std::floor(rect.left() / scale));
            const int top = int(std::floor(rect.top() / scale));
            const int right = int(std::ceil((rect.right() + 1) / scale));
            const int bottom = int(std::ceil((rect.bottom() + 1) / scale));
            rect = QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
        }
    }

    QRegion region;
    region.setRects(rects.constData(), int(rects.size()));
    return region;
}

const char *regionModeName(RegionMode mode)
{
    switch (mode) {
    case RegionMode::FullWindow:
        return "FullWindow (без региона)";
    case RegionMode::StaticRect:
        return "StaticRect (вариант A)";
    case RegionMode::AlphaOnce:
        return "AlphaOnce (вариант B)";
    case RegionMode::AlphaPerFrame:
        return "AlphaPerFrame (вариант C)";
    }
    return "?";
}
