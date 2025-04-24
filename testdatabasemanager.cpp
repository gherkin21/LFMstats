#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include "databasemanager.h"
#include "scrobbledata.h"

QDateTime createUtcDateTime(int year, int month, int day, int hour, int min,
                            int sec) {
  return QDateTime(QDate(year, month, day), QTime(hour, min, sec), Qt::UTC);
}

class TestDatabaseManager : public QObject {
  Q_OBJECT

public:
  TestDatabaseManager();
  ~TestDatabaseManager() override;

private:
  QTemporaryDir tempDir;
  QString dbPath;
  DatabaseManager *dbManager;

  QString testUser = "testuser";
  QList<ScrobbleData> scrobblesPage1;
  QList<ScrobbleData> scrobblesPage2_overlap;
  QList<ScrobbleData> scrobblesPage3_different_week;

  bool compareScrobbles(const QList<ScrobbleData> &s1,
                        const QList<ScrobbleData> &s2);
  QList<ScrobbleData> readJsonFileDirectly(const QString &filePath);

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  void testGetWeekStart();
  void testGetWeekFilePath();

  void testSaveChunkSync_new();
  void testSaveChunkSync_merge();
  void testSaveChunkSync_duplicates();
  void testSaveChunkSync_emptyInput();
  void testSaveChunkSync_invalidUser();
  void testSaveChunkSync_multipleFiles();
  void testSaveChunkSync_corruptExistingFile();

  void testLoadScrobblesSync_empty();
  void testLoadScrobblesSync_range();
  void testLoadScrobblesSync_all();
  void testLoadScrobblesSync_corruptFile();

  void testFindLastTimestampSync_empty();
  void testFindLastTimestampSync_found();

  void testIsSaveInProgress();
};

bool TestDatabaseManager::compareScrobbles(const QList<ScrobbleData> &s1,
                                           const QList<ScrobbleData> &s2) {
  if (s1.size() != s2.size())
    return false;
  for (int i = 0; i < s1.size(); ++i) {

    if (s1[i].timestamp != s2[i].timestamp || s1[i].artist != s2[i].artist ||
        s1[i].track != s2[i].track || s1[i].album != s2[i].album) {
      return false;
    }
  }
  return true;
}

QList<ScrobbleData>
TestDatabaseManager::readJsonFileDirectly(const QString &filePath) {
  QList<ScrobbleData> dataList;
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "Failed to open file for direct read:" << filePath;
    return dataList;
  }
  QByteArray jsonData = file.readAll();
  file.close();
  QJsonDocument doc = QJsonDocument::fromJson(jsonData);
  if (!doc.isArray()) {
    qWarning() << "File is not a JSON array:" << filePath;
    return dataList;
  }
  QJsonArray array = doc.array();
  for (const QJsonValue &val : array) {
    QJsonObject obj = val.toObject();
    ScrobbleData s;
    s.timestamp =
        QDateTime::fromSecsSinceEpoch(obj["uts"].toInteger(), Qt::UTC);
    s.artist = obj["artist"].toString();
    s.track = obj["track"].toString();
    s.album = obj["album"].toString();
    if (s.timestamp.isValid() && !s.artist.isEmpty() && !s.track.isEmpty()) {
      dataList.append(s);
    }
  }
  std::sort(dataList.begin(), dataList.end(), [](const auto &a, const auto &b) {
    return a.timestamp < b.timestamp;
  });
  return dataList;
}

TestDatabaseManager::TestDatabaseManager() : dbManager(nullptr) {}
TestDatabaseManager::~TestDatabaseManager() {}

void TestDatabaseManager::initTestCase() {
  QVERIFY(tempDir.isValid());
  dbPath = tempDir.path();
  qInfo() << "Using temporary directory for DB tests:" << dbPath;
  dbManager = new DatabaseManager(dbPath);

  QDateTime week1_t1 = createUtcDateTime(2023, 10, 23, 10, 0, 0);
  QDateTime week1_t2 = createUtcDateTime(2023, 10, 24, 12, 0, 0);
  QDateTime week1_t3 = createUtcDateTime(2023, 10, 25, 14, 0, 0);

  scrobblesPage1 << ScrobbleData{"Artist A", "Track 1", "Album X", week1_t1};
  scrobblesPage1 << ScrobbleData{"Artist B", "Track 2", "Album Y", week1_t2};

  scrobblesPage2_overlap << ScrobbleData{"Artist B", "Track 2", "Album Y",
                                         week1_t2};
  scrobblesPage2_overlap << ScrobbleData{"Artist C", "Track 3", "Album Z",
                                         week1_t3};

  QDateTime week2_t1 = createUtcDateTime(2023, 10, 30, 8, 0, 0);
  QDateTime week2_t2 = createUtcDateTime(2023, 10, 31, 9, 0, 0);
  scrobblesPage3_different_week
      << ScrobbleData{"Artist D", "Track 4", "", week2_t1};
  scrobblesPage3_different_week
      << ScrobbleData{"Artist E", "Track 5", "", week2_t2};
}

void TestDatabaseManager::cleanupTestCase() {
  delete dbManager;
  dbManager = nullptr;

  qInfo() << "Cleaned up temporary directory:" << dbPath;
}

void TestDatabaseManager::init() {

  QDir userDir(dbPath + "/" + testUser);
  if (userDir.exists()) {
    userDir.removeRecursively();
  }
}

void TestDatabaseManager::cleanup() {}

void TestDatabaseManager::testGetWeekStart() {

  QDateTime dt1 = createUtcDateTime(2023, 10, 23, 10, 0, 0);
  QDateTime expected1 = createUtcDateTime(2023, 10, 23, 0, 0, 0);
  QCOMPARE(DatabaseManager::getWeekStart(dt1), expected1);

  QDateTime dt2 = createUtcDateTime(2023, 10, 29, 23, 0, 0);
  QDateTime expected2 = createUtcDateTime(2023, 10, 23, 0, 0, 0);
  QCOMPARE(DatabaseManager::getWeekStart(dt2), expected2);

  QDateTime dt3 = createUtcDateTime(2023, 10, 30, 0, 0, 1);
  QDateTime expected3 = createUtcDateTime(2023, 10, 30, 0, 0, 0);
  QCOMPARE(DatabaseManager::getWeekStart(dt3), expected3);

  QDateTime dt4 = createUtcDateTime(2024, 1, 1, 5, 0, 0);
  QDateTime expected4 = createUtcDateTime(2024, 1, 1, 0, 0, 0);
  QCOMPARE(DatabaseManager::getWeekStart(dt4), expected4);

  QDateTime dt5 = createUtcDateTime(2023, 12, 31, 18, 0, 0);
  QDateTime expected5 = createUtcDateTime(2023, 12, 25, 0, 0, 0);
  QCOMPARE(DatabaseManager::getWeekStart(dt5), expected5);
}

void TestDatabaseManager::testGetWeekFilePath() {
  QString userPath = dbPath + "/" + testUser;
  QDateTime dt1 = createUtcDateTime(2023, 10, 23, 10, 0, 0);
  QString expectedPath1 = QString("%1/%2.json").arg(userPath).arg(1698019200);
  QCOMPARE(DatabaseManager::getWeekFilePath(userPath, dt1), expectedPath1);

  QDateTime dt2 = createUtcDateTime(2023, 10, 29, 23, 0, 0);
  QCOMPARE(DatabaseManager::getWeekFilePath(userPath, dt2), expectedPath1);

  QDateTime dt3 = createUtcDateTime(2023, 10, 30, 0, 0, 1);
  QString expectedPath3 = QString("%1/%2.json").arg(userPath).arg(1698624000);
  QCOMPARE(DatabaseManager::getWeekFilePath(userPath, dt3), expectedPath3);
}

void TestDatabaseManager::testSaveChunkSync_new() {
  QString errorMsg;
  bool success = DatabaseManager::saveChunkSync(dbPath, testUser,
                                                scrobblesPage1, errorMsg);
  QVERIFY(success);
  QVERIFY(errorMsg.isEmpty());

  QDateTime weekStart =
      DatabaseManager::getWeekStart(scrobblesPage1[0].timestamp);
  QString filePath = QString("%1/%2/%3.json")
                         .arg(dbPath)
                         .arg(testUser)
                         .arg(weekStart.toSecsSinceEpoch());
  QVERIFY(QFile::exists(filePath));

  QList<ScrobbleData> loadedData = readJsonFileDirectly(filePath);
  QCOMPARE(loadedData.size(), scrobblesPage1.size());
  QVERIFY(compareScrobbles(loadedData, scrobblesPage1));
}

void TestDatabaseManager::testSaveChunkSync_merge() {
  QString errorMsg;

  QVERIFY(DatabaseManager::saveChunkSync(dbPath, testUser, scrobblesPage1,
                                         errorMsg));

  QVERIFY(DatabaseManager::saveChunkSync(dbPath, testUser,
                                         scrobblesPage2_overlap, errorMsg));
  QVERIFY(errorMsg.isEmpty());

  QDateTime weekStart =
      DatabaseManager::getWeekStart(scrobblesPage1[0].timestamp);
  QString filePath = QString("%1/%2/%3.json")
                         .arg(dbPath)
                         .arg(testUser)
                         .arg(weekStart.toSecsSinceEpoch());
  QVERIFY(QFile::exists(filePath));

  QList<ScrobbleData> loadedData = readJsonFileDirectly(filePath);
  QList<ScrobbleData> expectedData = scrobblesPage1;
  expectedData.append(scrobblesPage2_overlap.last());
  std::sort(
      expectedData.begin(), expectedData.end(),
      [](const auto &a, const auto &b) { return a.timestamp < b.timestamp; });

  QCOMPARE(loadedData.size(), 3);
  QVERIFY(compareScrobbles(loadedData, expectedData));
}

void TestDatabaseManager::testSaveChunkSync_duplicates() {
  QString errorMsg;

  QVERIFY(DatabaseManager::saveChunkSync(dbPath, testUser, scrobblesPage1,
                                         errorMsg));

  QVERIFY(DatabaseManager::saveChunkSync(dbPath, testUser, scrobblesPage1,
                                         errorMsg));
  QVERIFY(errorMsg.isEmpty());

  QDateTime weekStart =
      DatabaseManager::getWeekStart(scrobblesPage1[0].timestamp);
  QString filePath = QString("%1/%2/%3.json")
                         .arg(dbPath)
                         .arg(testUser)
                         .arg(weekStart.toSecsSinceEpoch());
  QList<ScrobbleData> loadedData = readJsonFileDirectly(filePath);
  QCOMPARE(loadedData.size(), scrobblesPage1.size());
  QVERIFY(compareScrobbles(loadedData, scrobblesPage1));
}

void TestDatabaseManager::testSaveChunkSync_emptyInput() {
  QString errorMsg;
  QList<ScrobbleData> emptyList;
  bool success =
      DatabaseManager::saveChunkSync(dbPath, testUser, emptyList, errorMsg);
  QVERIFY(success);
  QVERIFY(errorMsg.isEmpty());

  QDir userDir(dbPath + "/" + testUser);
  QVERIFY(!userDir.exists());
}

void TestDatabaseManager::testSaveChunkSync_invalidUser() {
  QString errorMsg;
  QString invalidUser = "";
  bool success = DatabaseManager::saveChunkSync(dbPath, invalidUser,
                                                scrobblesPage1, errorMsg);
  QVERIFY(!success);
  QVERIFY(!errorMsg.isEmpty());
  QVERIFY(errorMsg.contains("empty"));
}

void TestDatabaseManager::testSaveChunkSync_multipleFiles() {
  QString errorMsg;

  QVERIFY(DatabaseManager::saveChunkSync(dbPath, testUser, scrobblesPage1,
                                         errorMsg));

  QVERIFY(DatabaseManager::saveChunkSync(
      dbPath, testUser, scrobblesPage3_different_week, errorMsg));
  QVERIFY(errorMsg.isEmpty());

  QDateTime weekStart1 =
      DatabaseManager::getWeekStart(scrobblesPage1[0].timestamp);
  QString filePath1 = QString("%1/%2/%3.json")
                          .arg(dbPath)
                          .arg(testUser)
                          .arg(weekStart1.toSecsSinceEpoch());
  QVERIFY(QFile::exists(filePath1));
  QDateTime weekStart2 =
      DatabaseManager::getWeekStart(scrobblesPage3_different_week[0].timestamp);
  QString filePath2 = QString("%1/%2/%3.json")
                          .arg(dbPath)
                          .arg(testUser)
                          .arg(weekStart2.toSecsSinceEpoch());
  QVERIFY(QFile::exists(filePath2));

  QList<ScrobbleData> loadedData2 = readJsonFileDirectly(filePath2);
  QVERIFY(compareScrobbles(loadedData2, scrobblesPage3_different_week));
}

void TestDatabaseManager::testSaveChunkSync_corruptExistingFile() {
  QString errorMsg;

  QVERIFY(DatabaseManager::saveChunkSync(dbPath, testUser, scrobblesPage1,
                                         errorMsg));

  QDateTime weekStart =
      DatabaseManager::getWeekStart(scrobblesPage1[0].timestamp);
  QString filePath = QString("%1/%2/%3.json")
                         .arg(dbPath)
                         .arg(testUser)
                         .arg(weekStart.toSecsSinceEpoch());
  QFile file(filePath);
  QVERIFY(
      file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
  file.write("This is not JSON");
  file.close();

  QVERIFY(DatabaseManager::saveChunkSync(dbPath, testUser,
                                         scrobblesPage2_overlap, errorMsg));
  QVERIFY(errorMsg.isEmpty());

  QList<ScrobbleData> loadedData = readJsonFileDirectly(filePath);
  QList<ScrobbleData> expectedData;
  QMap<qint64, bool> existingTimestamps;
  for (const ScrobbleData &newScrobble : scrobblesPage2_overlap) {
    if (newScrobble.timestamp.isValid() &&
        newScrobble.timestamp.toSecsSinceEpoch() > 0) {
      if (!existingTimestamps.contains(
              newScrobble.timestamp.toSecsSinceEpoch())) {
        expectedData.append(newScrobble);
        existingTimestamps[newScrobble.timestamp.toSecsSinceEpoch()] = true;
      }
    }
  }
  std::sort(
      expectedData.begin(), expectedData.end(),
      [](const auto &a, const auto &b) { return a.timestamp < b.timestamp; });

  QCOMPARE(loadedData.size(), expectedData.size());
  QVERIFY(compareScrobbles(loadedData, expectedData));
}

void TestDatabaseManager::testLoadScrobblesSync_empty() {
  QString errorMsg;

  QList<ScrobbleData> loaded = DatabaseManager::loadScrobblesSync(
      dbPath, testUser, QDateTime::currentDateTimeUtc().addDays(-1),
      QDateTime::currentDateTimeUtc().addDays(1), errorMsg);
  QVERIFY(loaded.isEmpty());
  QVERIFY(errorMsg.isEmpty());

  QDir().mkpath(dbPath + "/" + testUser);
  loaded = DatabaseManager::loadScrobblesSync(
      dbPath, testUser, QDateTime::currentDateTimeUtc().addDays(-1),
      QDateTime::currentDateTimeUtc().addDays(1), errorMsg);
  QVERIFY(loaded.isEmpty());
  QVERIFY(errorMsg.isEmpty());
}

void TestDatabaseManager::testLoadScrobblesSync_range() {
  QString errorMsg;

  QVERIFY(DatabaseManager::saveChunkSync(dbPath, testUser, scrobblesPage1,
                                         errorMsg));
  QVERIFY(DatabaseManager::saveChunkSync(
      dbPath, testUser, scrobblesPage3_different_week, errorMsg));

  QDateTime week1Start =
      DatabaseManager::getWeekStart(scrobblesPage1[0].timestamp);
  QDateTime week1End = week1Start.addDays(7);
  QList<ScrobbleData> loaded = DatabaseManager::loadScrobblesSync(
      dbPath, testUser, week1Start, week1End, errorMsg);
  QVERIFY(errorMsg.isEmpty());
  QCOMPARE(loaded.size(), scrobblesPage1.size());
  QVERIFY(compareScrobbles(loaded, scrobblesPage1));

  QDateTime week2Start =
      DatabaseManager::getWeekStart(scrobblesPage3_different_week[0].timestamp);
  QDateTime week2End = week2Start.addDays(7);
  loaded = DatabaseManager::loadScrobblesSync(dbPath, testUser, week2Start,
                                              week2End, errorMsg);
  QVERIFY(errorMsg.isEmpty());
  QCOMPARE(loaded.size(), scrobblesPage3_different_week.size());
  QVERIFY(compareScrobbles(loaded, scrobblesPage3_different_week));

  QDateTime midWeek1 = scrobblesPage1[0].timestamp.addDays(1);
  loaded = DatabaseManager::loadScrobblesSync(dbPath, testUser, midWeek1,
                                              week1End, errorMsg);
  QVERIFY(errorMsg.isEmpty());
  QList<ScrobbleData> expectedPartial;
  expectedPartial << scrobblesPage1[1];
  QCOMPARE(loaded.size(), 1);
  QVERIFY(compareScrobbles(loaded, expectedPartial));

  QDateTime crossWeekStart = scrobblesPage1.last().timestamp.addSecs(-1);
  QDateTime crossWeekEnd =
      scrobblesPage3_different_week[0].timestamp.addSecs(1);
  loaded = DatabaseManager::loadScrobblesSync(dbPath, testUser, crossWeekStart,
                                              crossWeekEnd, errorMsg);
  QVERIFY(errorMsg.isEmpty());
  QList<ScrobbleData> expectedCross;
  expectedCross << scrobblesPage1.last();
  expectedCross << scrobblesPage3_different_week.first();
  std::sort(
      expectedCross.begin(), expectedCross.end(),
      [](const auto &a, const auto &b) { return a.timestamp < b.timestamp; });
  QCOMPARE(loaded.size(), 2);
  QVERIFY(compareScrobbles(loaded, expectedCross));
}

void TestDatabaseManager::testLoadScrobblesSync_all() {
  QString errorMsg;

  QVERIFY(DatabaseManager::saveChunkSync(dbPath, testUser, scrobblesPage1,
                                         errorMsg));
  QVERIFY(DatabaseManager::saveChunkSync(dbPath, testUser,
                                         scrobblesPage2_overlap, errorMsg));
  QVERIFY(DatabaseManager::saveChunkSync(
      dbPath, testUser, scrobblesPage3_different_week, errorMsg));

  QList<ScrobbleData> loaded =
      DatabaseManager::loadAllScrobblesSync(dbPath, testUser, errorMsg);
  QVERIFY(errorMsg.isEmpty());

  QList<ScrobbleData> expectedAll = scrobblesPage1;
  expectedAll << scrobblesPage2_overlap.last();
  expectedAll << scrobblesPage3_different_week;
  std::sort(
      expectedAll.begin(), expectedAll.end(),
      [](const auto &a, const auto &b) { return a.timestamp < b.timestamp; });

  QCOMPARE(loaded.size(), 5);
  QVERIFY(compareScrobbles(loaded, expectedAll));
}

void TestDatabaseManager::testLoadScrobblesSync_corruptFile() {
  QString errorMsg;

  QVERIFY(DatabaseManager::saveChunkSync(dbPath, testUser, scrobblesPage1,
                                         errorMsg));
  QVERIFY(DatabaseManager::saveChunkSync(
      dbPath, testUser, scrobblesPage3_different_week, errorMsg));

  QDateTime weekStart1 =
      DatabaseManager::getWeekStart(scrobblesPage1[0].timestamp);
  QString filePath1 = QString("%1/%2/%3.json")
                          .arg(dbPath)
                          .arg(testUser)
                          .arg(weekStart1.toSecsSinceEpoch());
  QFile file(filePath1);
  QVERIFY(
      file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
  file.write("This is not JSON");
  file.close();

  QList<ScrobbleData> loaded =
      DatabaseManager::loadAllScrobblesSync(dbPath, testUser, errorMsg);

  QVERIFY(!errorMsg.isEmpty());
  QVERIFY(errorMsg.contains(QFileInfo(filePath1).fileName()));
  QVERIFY(errorMsg.contains("Corrupt file"));

  QCOMPARE(loaded.size(), scrobblesPage3_different_week.size());
  QVERIFY(compareScrobbles(loaded, scrobblesPage3_different_week));
}

void TestDatabaseManager::testFindLastTimestampSync_empty() {

  QCOMPARE(DatabaseManager::findLastTimestampSync(dbPath, testUser), (qint64)0);

  QDir().mkpath(dbPath + "/" + testUser);
  QCOMPARE(DatabaseManager::findLastTimestampSync(dbPath, testUser), (qint64)0);
}

void TestDatabaseManager::testFindLastTimestampSync_found() {
  QString errorMsg;
  QVERIFY(DatabaseManager::saveChunkSync(dbPath, testUser, scrobblesPage1,
                                         errorMsg));
  QVERIFY(DatabaseManager::saveChunkSync(
      dbPath, testUser, scrobblesPage3_different_week, errorMsg));

  qint64 expectedTs =
      scrobblesPage3_different_week.last().timestamp.toSecsSinceEpoch();
  QCOMPARE(DatabaseManager::findLastTimestampSync(dbPath, testUser),
           expectedTs);

  QVERIFY(DatabaseManager::saveChunkSync(dbPath, testUser,
                                         scrobblesPage2_overlap, errorMsg));

  QCOMPARE(DatabaseManager::findLastTimestampSync(dbPath, testUser),
           expectedTs);
}

void TestDatabaseManager::testIsSaveInProgress() {

  QVERIFY(!dbManager->isSaveInProgress());

  dbManager->saveScrobblesAsync(1, testUser, scrobblesPage1);

  QTest::qWait(100);
  QVERIFY(dbManager->isSaveInProgress());

  int loops = 0;
  while (dbManager->isSaveInProgress() && loops < 100) {
    QTest::qWait(10);
    loops++;
  }
  qInfo() << "isSaveInProgress became false after" << loops * 10 << "ms";
  QVERIFY(!dbManager->isSaveInProgress());

  QVERIFY(!dbManager->isSaveInProgress());
}

QTEST_MAIN(TestDatabaseManager)

#include "testdatabasemanager.moc"
