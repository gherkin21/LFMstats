#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QSettings>
#include <QString>

/**
 * @class SettingsManager
 * @brief Manages application settings persistence using QSettings.
 * @details Handles saving and loading of the Last.fm API key, username, and
 * state related to the scrobble fetching process (like whether the initial full
 * fetch is complete and resume information).
 * @inherits QObject
 */
class SettingsManager : public QObject {
  Q_OBJECT
public:
  /**
   * @brief Constructs a SettingsManager instance.
   * @details Initializes QSettings with appropriate scope and format.
   * @param parent The parent QObject, defaults to nullptr.
   */
  explicit SettingsManager(QObject *parent = nullptr);

  /**
   * @brief Saves the Last.fm API key to settings.
   * @param apiKey The API key string to save.
   */
  void saveApiKey(const QString &apiKey);
  /**
   * @brief Retrieves the saved Last.fm API key from settings.
   * @return The saved API key string, or an empty string if not set.
   */
  QString apiKey() const;
  /**
   * @brief Saves the Last.fm username to settings.
   * @param username The username string to save.
   */
  void saveUsername(const QString &username);
  /**
   * @brief Retrieves the saved Last.fm username from settings.
   * @return The saved username string, or an empty string if not set.
   */
  QString username() const;

  /**
   * @brief Sets the flag indicating whether the initial full fetch of scrobble
   * history is complete.
   * @param complete True if the initial fetch is considered complete, false
   * otherwise.
   */
  void setInitialFetchComplete(bool complete);
  /**
   * @brief Checks if the initial full fetch of scrobble history is marked as
   * complete.
   * @return True if the initial fetch is complete, false otherwise. Defaults to
   * false.
   */
  bool isInitialFetchComplete() const;
  /**
   * @brief Saves the page number of the last successfully saved page during an
   * initial fetch.
   * @details Used for resuming an interrupted fetch.
   * @param page The 1-based page number.
   */
  void saveLastSuccessfullySavedPage(int page);
  /**
   * @brief Loads the page number of the last successfully saved page from
   * settings.
   * @return The saved page number, or 0 if not set.
   */
  int loadLastSuccessfullySavedPage() const;
  /**
   * @brief Saves the total number of pages expected for the current user, as
   * reported by the API.
   * @details Used for resuming and tracking progress of an initial fetch.
   * @param totalPages The total number of pages.
   */
  void saveExpectedTotalPages(int totalPages);
  /**
   * @brief Loads the expected total number of pages from settings.
   * @return The saved total pages count, or 0 if not set.
   */
  int loadExpectedTotalPages() const;
  /**
   * @brief Clears settings related to resuming an initial fetch (last saved
   * page, expected total pages).
   * @details Typically called when a fetch completes successfully or the user
   * changes.
   */
  void clearResumeState();

private:
  QSettings
      m_settings; /**< @brief The QSettings instance used for persistence. */
  const QString KEY_API_KEY =
      "lastfm/apiKey"; /**< @brief Settings key for the API key. */
  const QString KEY_USERNAME =
      "lastfm/username"; /**< @brief Settings key for the username. */
  const QString KEY_INITIAL_FETCH_COMPLETE =
      "state/initialFetchComplete"; /**< @brief Settings key for the initial
                                       fetch completion flag. */
  const QString KEY_LAST_SAVED_PAGE =
      "state/lastSavedPage"; /**< @brief Settings key for the last saved page
                                number during initial fetch. */
  const QString KEY_EXPECTED_TOTAL_PAGES =
      "state/expectedTotalPages"; /**< @brief Settings key for the expected
                                     total pages during initial fetch. */
};

#endif // SETTINGSMANAGER_H
