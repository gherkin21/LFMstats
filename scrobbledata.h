#ifndef SCROBBLEDATA_H
#define SCROBBLEDATA_H

#include <QDateTime>
#include <QString>

struct ScrobbleData {
  QString artist;
  QString track;
  QString album;
  QDateTime timestamp;
};

#endif // SCROBBLEDATA_H
