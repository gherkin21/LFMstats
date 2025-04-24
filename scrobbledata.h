#ifndef SCROBBLEDATA_H
#define SCROBBLEDATA_H

#include <QDateTime>
#include <QString>

/**
 * @struct ScrobbleData
 * @brief Represents the essential information for a single Last.fm scrobble (a
 * played track).
 */
struct ScrobbleData {
  QString artist;      /**< @brief The name of the artist. */
  QString track;       /**< @brief The name of the track. */
  QString album;       /**< @brief The name of the album. */
  QDateTime timestamp; /**< @brief The UTC timestamp when the track finished
                          playing (or started, according to Last.fm). */
};

#endif // SCROBBLEDATA_H
