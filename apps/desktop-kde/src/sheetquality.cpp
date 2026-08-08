#include "sheetquality.h"

#include <QCoreApplication>
#include <QImage>
#include <QRect>

namespace {

// Дрожание отличается от движения не величиной, а характером.
//
// Первая попытка ловила разброс размера фигуры между кадрами с порогом 2%.
// На настоящем питомце она сработала на шести состояниях из восьми, и почти
// все срабатывания оказались законными: прыжок уменьшает фигуру, чтобы
// влезть в ячейку, свернувшийся клубком питомец ниже сидящего вдвое.
// Предупреждение, которое врёт в пяти случаях из шести, научат игнорировать
// за день — и вместе с ним пропустят настоящее.
//
// Признак получше: при плавном движении фигура идёт туда и обратно, поэтому
// сумма покадровых изменений близка к удвоенному размаху. При дрожании она
// много больше размаха: фигура скачет каждый кадр.
//
// На встроенном питомце отношение выходит 1.0–2.6 у всех состояний,
// то есть дрожания по высоте нет вовсе. Порог поставлен с запасом.
constexpr double kJitterRatio = 3.5;

// Ниже этого размаха мерить нечего: пара пикселей разницы — это округление
// при отрисовке, а не свойство ассета.
constexpr int kIgnorableRange = 3;

// Ниже этого значения пиксель считается прозрачным. Полупрозрачная тень
// не должна влиять на размер фигуры.
constexpr int kAlphaThreshold = 24;

// Границы непрозрачной части ячейки.
QRect opaqueBounds(const QImage &sheet, int x0, int y0, int width, int height)
{
    int left = width;
    int right = -1;
    int top = height;
    int bottom = -1;

    for (int y = 0; y < height; ++y) {
        const int sy = y0 + y;
        if (sy < 0 || sy >= sheet.height())
            continue;

        for (int x = 0; x < width; ++x) {
            const int sx = x0 + x;
            if (sx < 0 || sx >= sheet.width())
                continue;

            if (qAlpha(sheet.pixel(sx, sy)) < kAlphaThreshold)
                continue;

            left = qMin(left, x);
            right = qMax(right, x);
            top = qMin(top, y);
            bottom = qMax(bottom, y);
        }
    }

    if (right < 0)
        return {};

    return QRect(left, top, right - left + 1, bottom - top + 1);
}

} // namespace

QStringList SheetQuality::inspect(const QImage &sheet,
                                  const QList<CoreBridge::Animation> &animations,
                                  const QStringList &names)
{
    QStringList warnings;

    if (sheet.isNull()) {
        warnings << QCoreApplication::translate("SheetQuality",
                                                "лист не удалось прочитать — качество не проверено");
        return warnings;
    }

    for (int index = 0; index < animations.size() && index < names.size(); ++index) {
        const CoreBridge::Animation &animation = animations.at(index);
        if (animation.frames < 2 || animation.cellWidth <= 0 || animation.cellHeight <= 0)
            continue;

        QList<int> heights;
        heights.reserve(animation.frames);

        for (int frame = 0; frame < animation.frames; ++frame) {
            const QRect bounds = opaqueBounds(sheet,
                                              (animation.startColumn + frame) * animation.cellWidth,
                                              animation.row * animation.cellHeight,
                                              animation.cellWidth, animation.cellHeight);
            if (!bounds.isEmpty())
                heights.append(bounds.height());
        }

        if (heights.size() < 2)
            continue;

        int lowest = heights.first();
        int highest = heights.first();
        int totalChange = 0;
        for (int i = 1; i < heights.size(); ++i) {
            lowest = qMin(lowest, heights.at(i));
            highest = qMax(highest, heights.at(i));
            totalChange += qAbs(heights.at(i) - heights.at(i - 1));
        }

        const int range = highest - lowest;
        if (range <= kIgnorableRange)
            continue;

        const double ratio = double(totalChange) / double(range);
        if (ratio > kJitterRatio) {
            warnings << QCoreApplication::translate(
                            "SheetQuality",
                            "«%1»: фигура скачет между кадрами (размах %2 px, "
                            "суммарные изменения %3 px) — питомец будет дрожать")
                            .arg(names.at(index))
                            .arg(range)
                            .arg(totalChange);
        }
    }

    return warnings;
}
