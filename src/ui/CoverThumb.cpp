#include "CoverThumb.h"

#include <QImageReader>

#include <algorithm>
#include <cmath>

QPixmap squareCoverThumb(const QString &path, int side)
{
    if (path.isEmpty() || side < 1) return {};

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize src = reader.size();
    if (src.isValid() && src.width() > 0 && src.height() > 0) {
        const double scale = double(side) / double(std::min(src.width(), src.height()));
        const int width = std::max(side, int(std::lround(src.width() * scale)));
        const int height = std::max(side, int(std::lround(src.height() * scale)));
        reader.setScaledSize(QSize(width, height));
    }

    QImage image = reader.read();
    if (image.isNull()) return {};
    const int crop = std::min(image.width(), image.height());
    if (crop < 1) return {};
    const int x = (image.width() - crop) / 2;
    const int y = (image.height() - crop) / 2;
    image = image.copy(x, y, crop, crop);
    if (image.width() != side)
        image = image.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return QPixmap::fromImage(image);
}
