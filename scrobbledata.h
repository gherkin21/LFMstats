#ifndef SCROBBLEDATA_H
#define SCROBBLEDATA_H

#include <QString>
#include <QDateTime>

struct ScrobbleData {
    QString artist;
    QString track;
    QString album;
    QDateTime timestamp;
    // Note: duration is often missing or 0 in getrecenttracks,
    // getting it accurately requires extra API calls (track.getInfo).
    // We'll omit it for simplicity for now.
};

#endif // SCROBBLEDATA_H

