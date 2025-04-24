#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QSettings>
#include <QString>

class SettingsManager : public QObject {
  Q_OBJECT
public:
  explicit SettingsManager(QObject *parent = nullptr);

  void saveApiKey(const QString &apiKey);
  QString apiKey() const;
  void saveUsername(const QString &username);
  QString username() const;

  void setInitialFetchComplete(bool complete);
  bool isInitialFetchComplete() const;

  void saveLastSuccessfullySavedPage(int page);
  int loadLastSuccessfullySavedPage() const;
  void saveExpectedTotalPages(int totalPages);
  int loadExpectedTotalPages() const;
  void clearResumeState();

private:
  QSettings m_settings;
  const QString KEY_API_KEY = "lastfm/apiKey";
  const QString KEY_USERNAME = "lastfm/username";
  const QString KEY_INITIAL_FETCH_COMPLETE = "state/initialFetchComplete";
  const QString KEY_LAST_SAVED_PAGE = "state/lastSavedPage";
  const QString KEY_EXPECTED_TOTAL_PAGES = "state/expectedTotalPages";
};

#endif // SETTINGSMANAGER_H
