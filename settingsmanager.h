#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QSettings>
#include <QString>

class SettingsManager : public QObject {
    Q_OBJECT
public:
    explicit SettingsManager(QObject *parent = nullptr);

    // --- Basic Settings ---
    void saveApiKey(const QString &apiKey);
    QString apiKey() const;
    void saveUsername(const QString &username);
    QString username() const;

    // --- Initial Fetch State ---
    // Sets the flag indicating if a full historical fetch has ever completed successfully.
    void setInitialFetchComplete(bool complete);
    // Checks if the flag for a successful initial full fetch is set.
    bool isInitialFetchComplete() const;

    // --- Resume State (for incomplete initial fetch) ---
    // Saves the highest page number successfully saved during an initial fetch attempt.
    void saveLastSuccessfullySavedPage(int page);
    // Loads the highest page number successfully saved. Defaults to 0.
    int loadLastSuccessfullySavedPage() const;
    // Saves the total number of pages expected for the full history.
    void saveExpectedTotalPages(int totalPages);
    // Loads the expected total number of pages. Defaults to 0.
    int loadExpectedTotalPages() const;
    // Clears the resume state (page numbers) - used on success or user change.
    void clearResumeState();

private:
    QSettings m_settings; // Manages storing settings persistently
    // Define constants for keys to avoid typos and ease maintenance
    const QString KEY_API_KEY = "lastfm/apiKey";
    const QString KEY_USERNAME = "lastfm/username";
    const QString KEY_INITIAL_FETCH_COMPLETE = "state/initialFetchComplete";
    const QString KEY_LAST_SAVED_PAGE = "state/lastSavedPage";
    const QString KEY_EXPECTED_TOTAL_PAGES = "state/expectedTotalPages";
};

#endif // SETTINGSMANAGER_H
