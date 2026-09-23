#pragma once
#include <QPixmap>
#include <QString>

// Square thumbnail decoded near the size the list actually draws.
// The full cover file stays on disk.
QPixmap squareCoverThumb(const QString &path, int side);
