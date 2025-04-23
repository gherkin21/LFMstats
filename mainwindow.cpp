#include "mainwindow.h"
#include "ui_mainwindow.h"
// Include UI headers for pages WITH NEW WIDGETS
#include "ui_generalstatspage.h"
#include "ui_databasetablepage.h"
#include "ui_artistspage.h"
#include "ui_trackspage.h"
#include "ui_chartspage.h"
#include "ui_aboutpage.h"

#include <QPushButton>
#include <QLineEdit> // Include for setup/connections
#include <QMessageBox>
#include <QInputDialog>
#include <QDebug>
#include <QHeaderView>
#include <QDateTime>
#include <QTimer>
#include <QComboBox> // Include for connecting combobox
#include <QCoreApplication> // For processEvents

// Include Qt Charts implementation headers
#include <QtCharts/QChart>
#include <QtCharts/QBarSeries>
#include <QtCharts/QLineSeries> // For timeline chart
#include <QtCharts/QBarSet>
#include <QtCharts/QValueAxis>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QDateTimeAxis> // For timeline chart
#include <QtCharts/QLegendMarker> // For hiding legend items


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_isLoading(false),
    m_fetchingComplete(false),
    m_expectedTotalPages(0),
    m_lastSuccessfullySavedPage(0)
{
    ui->setupUi(this);
    setWindowTitle("Last.fm Scrobble Analyzer");
    ui->statusbar->showMessage("Ready.");
    setupPages(); // <= This now needs to find the new widgets

    // --- Connect Signals/Slots ---
    connect(ui->menuListWidget, &QListWidget::currentItemChanged, this, &MainWindow::onMenuItemChanged);
    // Connect buttons on About page
    QPushButton *fetchBtn = aboutPage ? aboutPage->findChild<QPushButton*>("fetchButton") : nullptr;
    if (fetchBtn) { connect(fetchBtn, &QPushButton::clicked, this, &MainWindow::fetchNewScrobbles); }
    else { qWarning() << "Could not find fetchButton on About page!"; }
    QPushButton *settingsBtn = aboutPage ? aboutPage->findChild<QPushButton*>("settingsButton"): nullptr;
    if (settingsBtn) { connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::setupUser); }
    else { qWarning() << "Could not find settingsButton on About page!"; }

    // Connect NEW ComboBox on Stats page
    if (m_meanRangeComboBox) {
        connect(m_meanRangeComboBox, &QComboBox::currentIndexChanged, this, &MainWindow::updateMeanScrobbleCalculation);
    } else { qWarning() << "Could not find meanRangeComboBox during setup!"; }

    // Connect NEW Find Last Played button
    if (m_findLastPlayedButton) {
        connect(m_findLastPlayedButton, &QPushButton::clicked, this, &MainWindow::findLastPlayedTrack);
        // Optional: Connect returnPressed on line edits to trigger find
        if(m_artistInput) connect(m_artistInput, &QLineEdit::returnPressed, m_findLastPlayedButton, &QPushButton::click);
        if(m_trackInput) connect(m_trackInput, &QLineEdit::returnPressed, m_findLastPlayedButton, &QPushButton::click);
    } else { qWarning() << "Could not find findLastPlayedButton during setup!"; }

    // Connect Managers (no changes here usually)
    connect(&m_lastFmManager, &LastFmManager::pageReadyForSaving, this, &MainWindow::handleSavePageOfScrobbles);
    connect(&m_lastFmManager, &LastFmManager::totalPagesDetermined, this, &MainWindow::handleTotalPagesDetermined);
    connect(&m_lastFmManager, &LastFmManager::fetchFinished, this, &MainWindow::handleFetchFinished);
    connect(&m_lastFmManager, &LastFmManager::fetchError, this, &MainWindow::handleApiError); // Connect simplified signal
    connect(&m_databaseManager, &DatabaseManager::pageSaveCompleted, this, &MainWindow::handlePageSaveComplete);
    connect(&m_databaseManager, &DatabaseManager::pageSaveFailed, this, &MainWindow::handlePageSaveFailed);
    connect(&m_databaseManager, &DatabaseManager::loadComplete, this, &MainWindow::handleDbLoadComplete);
    connect(&m_databaseManager, &DatabaseManager::loadError, this, &MainWindow::handleDbLoadError);
    connect(&m_databaseManager, &DatabaseManager::statusMessage, this, &MainWindow::handleDbStatusUpdate);

    setupMenu();
    promptForSettings(); // Calls loadData -> updateDisplay -> update...View potentially
}

MainWindow::~MainWindow() {
    delete ui;
}

// Updated setupPages to find new widgets
void MainWindow::setupPages() {
    while(ui->stackedWidget->count() > 0) { ui->stackedWidget->removeWidget(ui->stackedWidget->widget(0)); }

    // --- General Stats Page ---
    Ui::GeneralStatsPage ui_gs;
    generalStatsPage = new QWidget();
    ui_gs.setupUi(generalStatsPage);
    m_statsLabel = ui_gs.statsLabel; // May be unused now
    // Find widgets by object name (ensure these match names set in Designer)
    m_firstScrobbleLabelValue = generalStatsPage->findChild<QLabel*>("firstScrobbleLabelValue");
    m_lastScrobbleLabelValue = generalStatsPage->findChild<QLabel*>("lastScrobbleLabelValue");
    m_meanRangeComboBox = generalStatsPage->findChild<QComboBox*>("meanRangeComboBox");
    m_meanScrobblesResultLabel = generalStatsPage->findChild<QLabel*>("meanScrobblesResultLabel");
    m_longestStreakLabelValue = generalStatsPage->findChild<QLabel*>("longestStreakLabelValue");
    m_currentStreakLabelValue = generalStatsPage->findChild<QLabel*>("currentStreakLabelValue");
    m_artistInput = generalStatsPage->findChild<QLineEdit*>("artistInput");
    m_trackInput = generalStatsPage->findChild<QLineEdit*>("trackInput");
    m_findLastPlayedButton = generalStatsPage->findChild<QPushButton*>("findLastPlayedButton");
    m_lastPlayedResultLabel = generalStatsPage->findChild<QLabel*>("lastPlayedResultLabel");

    // Add items to combobox if not done in Designer
    if (m_meanRangeComboBox && m_meanRangeComboBox->count() == 0) {
        m_meanRangeComboBox->addItems({"Last 7 Days", "Last 30 Days", "Last 90 Days", "All Time"});
        m_meanRangeComboBox->setToolTip("Calculate average scrobbles per day over selected period");
    }
    // Set placeholders for input fields
    if(m_artistInput) m_artistInput->setPlaceholderText("Artist Name");
    if(m_trackInput) m_trackInput->setPlaceholderText("Track Name");


    // --- Other Pages Setup (DB, Artists, Tracks, Charts, About) ---
    Ui::DatabaseTablePage ui_dt; databaseTablePage = new QWidget(); ui_dt.setupUi(databaseTablePage); m_dbTableWidget = ui_dt.dbTableWidget;
    Ui::ArtistsPage ui_a; artistsPage = new QWidget(); ui_a.setupUi(artistsPage); m_artistListWidget = ui_a.artistListWidget;
    Ui::TracksPage ui_t; tracksPage = new QWidget(); ui_t.setupUi(tracksPage); m_trackListWidget = ui_t.trackListWidget;
    Ui::ChartsPage ui_c; chartsPage = new QWidget(); ui_c.setupUi(chartsPage);
    m_artistsChartView = chartsPage->findChild<QChartView*>("artistsChartView");
    m_tracksChartView = chartsPage->findChild<QChartView*>("tracksChartView");
    m_hourlyChartView = chartsPage->findChild<QChartView*>("hourlyChartView");
    m_weeklyChartView = chartsPage->findChild<QChartView*>("weeklyChartView");
    Ui::AboutPage ui_ab; aboutPage = new QWidget(); ui_ab.setupUi(aboutPage); m_currentUserLabel = ui_ab.currentUserLabel;

    // --- Add Pages to StackedWidget ---
    ui->stackedWidget->addWidget(generalStatsPage);     // Index 0
    ui->stackedWidget->addWidget(databaseTablePage);    // Index 1
    ui->stackedWidget->addWidget(artistsPage);          // Index 2
    ui->stackedWidget->addWidget(tracksPage);           // Index 3
    ui->stackedWidget->addWidget(chartsPage);           // Index 4
    ui->stackedWidget->addWidget(aboutPage);            // Index 5

    // --- Validate NEW pointers (optional but good practice) ---
    if (!m_firstScrobbleLabelValue) qWarning("firstScrobbleLabelValue not found! Check object name in UI file.");
    if (!m_lastScrobbleLabelValue) qWarning("lastScrobbleLabelValue not found!");
    if (!m_meanRangeComboBox) qWarning("meanRangeComboBox not found!");
    if (!m_meanScrobblesResultLabel) qWarning("meanScrobblesResultLabel not found!");
    if (!m_longestStreakLabelValue) qWarning("longestStreakLabelValue not found!");
    if (!m_currentStreakLabelValue) qWarning("currentStreakLabelValue not found!");
    if (!m_artistInput) qWarning("artistInput not found!");
    if (!m_trackInput) qWarning("trackInput not found!");
    if (!m_findLastPlayedButton) qWarning("findLastPlayedButton not found!");
    if (!m_lastPlayedResultLabel) qWarning("lastPlayedResultLabel not found!");
    if (!m_artistsChartView) qWarning("artistsChartView not found!");
    if (!m_tracksChartView) qWarning("tracksChartView not found!");
    if (!m_hourlyChartView) qWarning("hourlyChartView not found!");
    if (!m_weeklyChartView) qWarning("weeklyChartView not found!");
}

void MainWindow::setupMenu() {
    ui->menuListWidget->clear();
    ui->menuListWidget->addItem("Dashboard / Stats"); // Index 0
    ui->menuListWidget->addItem("Database View");     // Index 1
    ui->menuListWidget->addItem("Top Artists");       // Index 2
    ui->menuListWidget->addItem("Top Tracks");        // Index 3
    ui->menuListWidget->addItem("Charts");            // Index 4
    ui->menuListWidget->addItem("About / Settings");  // Index 5
    ui->menuListWidget->setCurrentRow(0);
}

void MainWindow::promptForSettings() {
    QString username = m_settingsManager.username(); QString apiKey = m_settingsManager.apiKey(); bool settingsNeeded = false;
    if (username.isEmpty()) { settingsNeeded = true; bool ok; username = QInputDialog::getText(this, "Setup", "Username:", QLineEdit::Normal, "", &ok); if (ok && !username.isEmpty()) m_settingsManager.saveUsername(username); else { QMessageBox::warning(this, "Setup", "Username required."); username = "";} }
    if (apiKey.isEmpty()) { settingsNeeded = true; bool ok; apiKey = QInputDialog::getText(this, "Setup", "API Key:", QLineEdit::Normal, "", &ok); if (ok && !apiKey.isEmpty()) m_settingsManager.saveApiKey(apiKey); else { QMessageBox::warning(this, "Setup", "API Key required."); apiKey = "";} }
    username = m_settingsManager.username(); apiKey = m_settingsManager.apiKey(); // Re-read
    if (!username.isEmpty() && !apiKey.isEmpty()) {
        ui->profileNameLabel->setText(username); if(m_currentUserLabel) m_currentUserLabel->setText(username);
        qInfo() << "[Main Window] Calling LastFmManager::setup with API Key:" << (apiKey.isEmpty() ? "EMPTY" : "SET") << "Username:" << username;
        m_lastFmManager.setup(apiKey, username); loadDataForCurrentView();
    } else { ui->profileNameLabel->setText("<Required>"); if(m_currentUserLabel) m_currentUserLabel->setText("<Not Set>"); m_loadedScrobbles.clear(); updateDisplay(); }
}

void MainWindow::setupUser() {
    bool ok_user, ok_key; QString current_user = m_settingsManager.username(); QString current_key = m_settingsManager.apiKey();
    QString username = QInputDialog::getText(this, "Settings", "Username:", QLineEdit::Normal, current_user, &ok_user);
    QString apiKey = QInputDialog::getText(this, "Settings", "API Key:", QLineEdit::Normal, current_key, &ok_key);
    bool changed = false; bool userChanged = false;
    if (ok_user) { if (!username.isEmpty()) { if (username != current_user) { m_settingsManager.saveUsername(username); ui->profileNameLabel->setText(username); if(m_currentUserLabel) m_currentUserLabel->setText(username); changed = true; userChanged = true; } } else { QMessageBox::warning(this, "Input Error", "Username empty."); } }
    if (ok_key) { if (!apiKey.isEmpty()) { if (apiKey != current_key) { m_settingsManager.saveApiKey(apiKey); changed = true; } } else { QMessageBox::warning(this, "Input Error", "API Key empty."); } }
    if (changed) {
        QString currentApiKey = m_settingsManager.apiKey(); QString currentUsername = m_settingsManager.username();
        qInfo() << "[Main Window] Calling LastFmManager::setup from setupUser. API Key:" << (currentApiKey.isEmpty() ? "EMPTY" : "SET") << "Username:" << currentUsername;
        m_lastFmManager.setup(currentApiKey, currentUsername);
        QMessageBox::information(this, "Settings Updated", "Settings updated. Fetch if needed.\nData cleared.");
        m_loadedScrobbles.clear();
        if (userChanged) { m_settingsManager.setInitialFetchComplete(false); m_settingsManager.clearResumeState(); m_lastSuccessfullySavedPage = 0; m_expectedTotalPages = 0; qInfo() << "User changed, reset state."; }
        updateDisplay();
    }
}

void MainWindow::fetchNewScrobbles() {
    if (m_isLoading) { QMessageBox::warning(this, "Busy", "Operation in progress."); return; }
    QString username = m_settingsManager.username(); QString apiKey = m_settingsManager.apiKey();
    if (username.isEmpty() || apiKey.isEmpty()) { QMessageBox::warning(this, "Setup", "Set Username/API Key first."); promptForSettings(); return; }

    m_isLoading = true; m_fetchingComplete = false;
    bool isUpdate = m_settingsManager.isInitialFetchComplete();
    qInfo() << "================ FETCH TRIGGERED ================";
    if (isUpdate) {
        qint64 startTimestamp = m_databaseManager.getLastSyncTimestamp(username);
        ui->statusbar->showMessage("Fetching updates..."); qInfo() << "Mode: Incremental Update since" << startTimestamp;
        m_expectedTotalPages = 0; m_lastSuccessfullySavedPage = 0;
        m_lastFmManager.fetchScrobblesSince(startTimestamp);
    } else {
        m_lastSuccessfullySavedPage = m_settingsManager.loadLastSuccessfullySavedPage();
        m_expectedTotalPages = m_settingsManager.loadExpectedTotalPages();
        int startPage = m_lastSuccessfullySavedPage + 1;
        ui->statusbar->showMessage(QString("Fetching history from page %1...").arg(startPage));
        qInfo() << "Mode: Full Fetch/Resume from page" << startPage << "(Known Total:" << m_expectedTotalPages << ")";
        m_lastFmManager.startInitialOrResumeFetch(startPage, m_expectedTotalPages);
    }
    qInfo() << "==================================================";
}

void MainWindow::onMenuItemChanged(QListWidgetItem *current, QListWidgetItem *previous) {
    Q_UNUSED(previous); if (!current) return;
    ui->selectedTabLabel->setText("Selected: " + current->text());
    int index = ui->menuListWidget->row(current);
    if (index >= 0 && index < ui->stackedWidget->count()) { ui->stackedWidget->setCurrentIndex(index); loadDataForCurrentView(); }
    else { qWarning() << "Invalid index selected:" << index; }
}

void MainWindow::loadDataForCurrentView() {
    if (!m_loadedScrobbles.isEmpty() && !m_isLoading) { updateDisplay(); return; }
    QString username = m_settingsManager.username();
    if (!username.isEmpty() && !m_isLoading && m_loadedScrobbles.isEmpty()) {
        m_isLoading = true; ui->statusbar->showMessage("Loading data from database...");
        m_databaseManager.loadAllScrobblesAsync(username);
    } else if (m_isLoading) { qDebug() << "Load requested while already loading."; }
    else { updateDisplay(); } // Show empty state if needed
}

void MainWindow::handleSavePageOfScrobbles(const QList<ScrobbleData> &pageScrobbles, int pageNumber) {
    qDebug() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << "- [Main] Rcvd pageReady: Page" << pageNumber << ", Size:" << pageScrobbles.count();
    if (!pageScrobbles.isEmpty()) {
        qDebug() << "[Main] Calling DB saveAsync page" << pageNumber;
        m_databaseManager.saveScrobblesAsync(pageNumber, m_settingsManager.username(), pageScrobbles);
    } else if (!m_settingsManager.isInitialFetchComplete()) {
        qWarning() << "[Main] Empty Page" << pageNumber << " during initial fetch. Simulating completion.";
        handlePageSaveComplete(pageNumber); // Simulate to advance state
    } else { qDebug() << "[Main] Empty Page" << pageNumber << " during update, skipping save call."; }
}

void MainWindow::handleTotalPagesDetermined(int totalPages) {
    qInfo() << "[Main] Total pages determined:" << totalPages;
    if (m_expectedTotalPages <= 0 || totalPages != m_expectedTotalPages) {
        m_expectedTotalPages = totalPages;
        if (!m_settingsManager.isInitialFetchComplete()) { m_settingsManager.saveExpectedTotalPages(m_expectedTotalPages); }
    }
}

void MainWindow::handleFetchFinished() {
    qInfo() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << "- [Main] API Fetch part finished.";
    m_fetchingComplete = true; checkOverallCompletion();
}

void MainWindow::handleApiError(const QString &errorString) {
    qWarning() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << "- [Main] API Error:" << errorString;
    m_fetchingComplete = true; m_isLoading = false;
    m_settingsManager.setInitialFetchComplete(false); qWarning() << "API Error: Marked initial fetch as incomplete.";
    ui->statusbar->showMessage("API Error.", 5000); QMessageBox::critical(this, "API Error", errorString);
}

void MainWindow::handlePageSaveComplete(int pageNumber) {
    qDebug() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << "- [Main] DB Save Complete: Page" << pageNumber;
    m_lastSuccessfullySavedPage = qMax(m_lastSuccessfullySavedPage, pageNumber);
    if (!m_settingsManager.isInitialFetchComplete()) { m_settingsManager.saveLastSuccessfullySavedPage(m_lastSuccessfullySavedPage); }
    checkOverallCompletion();
}

void MainWindow::handlePageSaveFailed(int pageNumber, const QString &error) {
    qWarning() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << "- [Main] DB Save FAILED: Page" << pageNumber << "Err:" << error;
    m_fetchingComplete = true; m_isLoading = false; // Stop process on DB fail
    m_settingsManager.setInitialFetchComplete(false); qWarning() << "DB Save Error: Marked initial fetch as incomplete.";
    ui->statusbar->showMessage("Database save error!", 5000); QMessageBox::critical(this, "DB Save Error", error);
}

void MainWindow::checkOverallCompletion() {
    if (m_fetchingComplete && !m_databaseManager.isSaveInProgress() && m_isLoading) {
        qInfo() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << "- [Main] Fetch/Save operations fully complete.";
        bool hadError = ui->statusbar->currentMessage().contains("Error", Qt::CaseInsensitive);
        bool wasInitial = !m_settingsManager.isInitialFetchComplete();
        if (!hadError) {
            if (wasInitial) {
                if (m_expectedTotalPages > 0 && m_lastSuccessfullySavedPage >= m_expectedTotalPages) {
                    qInfo() << "Initial fetch fully completed.";
                    m_settingsManager.setInitialFetchComplete(true); m_settingsManager.clearResumeState();
                    ui->statusbar->showMessage("Full history download complete.", 5000);
                } else {
                    qWarning() << "Fetch finished but incomplete! Saved:" << m_lastSuccessfullySavedPage << "Expected:" << m_expectedTotalPages;
                    m_settingsManager.setInitialFetchComplete(false); ui->statusbar->showMessage("History download incomplete.", 5000);
                }
            } else { ui->statusbar->showMessage("Update complete.", 5000); }
        } else { ui->statusbar->showMessage("Update finished with errors.", 5000); }
        m_isLoading = false; // Operation finished (success or error)
        qInfo() << "Reloading data after completion."; m_loadedScrobbles.clear(); loadDataForCurrentView();
    } else if (m_fetchingComplete && m_databaseManager.isSaveInProgress() && m_isLoading) {
        qDebug() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << "- Completion check: Fetch done, waiting for DB saves...";
        ui->statusbar->showMessage("Finishing saving...");
    } else if (!m_fetchingComplete && m_isLoading) {
        qDebug() << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << "- Completion check: Still fetching...";
    } else { /* Log other states if needed */ }
}

void MainWindow::handleDbLoadComplete(const QList<ScrobbleData> &scrobbles) {
    if (m_isLoading) { m_isLoading = false; }
    ui->statusbar->showMessage(QString("Loaded %1 scrobbles.").arg(scrobbles.count()), 5000);
    m_loadedScrobbles = scrobbles; updateDisplay();
    if (!m_settingsManager.isInitialFetchComplete() && !scrobbles.isEmpty()) {
        qWarning() << "Loaded data, but initial full fetch may be incomplete.";
        QTimer::singleShot(5100, this, [this](){ if (this->isVisible() && !m_settingsManager.isInitialFetchComplete()) { QString msg = ui->statusbar->currentMessage(); if (msg.startsWith("Loaded")) ui->statusbar->showMessage(msg + " (History incomplete)", 0); } });
    } else if (!m_settingsManager.isInitialFetchComplete() && scrobbles.isEmpty()) { qInfo() << "No data loaded. Initial fetch needed."; ui->statusbar->showMessage("No data. Please Fetch.", 5000); }
}

void MainWindow::handleDbLoadError(const QString &error) {
    if (m_isLoading) { m_isLoading = false; }
    ui->statusbar->showMessage("Database load error!", 5000); QMessageBox::critical(this, "DB Load Error", error);
    m_loadedScrobbles.clear(); updateDisplay();
}

void MainWindow::handleDbStatusUpdate(const QString &message) {
    if (message.contains("Error", Qt::CaseInsensitive)) { ui->statusbar->showMessage(message); }
    else if (m_isLoading || message.contains("Idle", Qt::CaseInsensitive) || message.contains("complete", Qt::CaseInsensitive) ) { ui->statusbar->showMessage(message); }
    else { ui->statusbar->showMessage(message, 2500); }
}

// Slot to update the mean scrobbles calculation based on combobox
void MainWindow::updateMeanScrobbleCalculation() {
    if (m_loadedScrobbles.isEmpty() || !m_meanRangeComboBox || !m_meanScrobblesResultLabel) {
        if(m_meanScrobblesResultLabel) m_meanScrobblesResultLabel->setText("N/A");
        return;
    }

    QString selectedRange = m_meanRangeComboBox->currentText();
    QDateTime fromDateUTC; // We need UTC for the calculation function
    QDateTime toDateUTC;   // We need UTC for the calculation function

    // Get latest scrobble time (UTC)
    QDateTime lastScrobbleUTC = m_analyticsEngine.getLastScrobbleDate(m_loadedScrobbles);
    if (lastScrobbleUTC.isNull()) { // Handle case where last date isn't found
        lastScrobbleUTC = QDateTime::currentDateTimeUtc();
    }
    // Set 'to' boundary (exclusive) for calculation - end of the day of the last scrobble in local time?
    // Or just use last scrobble time + 1 sec? Let's use last scrobble time + 1 sec for simplicity.
    toDateUTC = lastScrobbleUTC.addSecs(1); // Range is [fromDateUTC, toDateUTC)

    // Determine 'from' date based on selection, converting local time ranges to UTC
    if (selectedRange == "Last 7 Days") {
        fromDateUTC = toDateUTC.addDays(-7);
    } else if (selectedRange == "Last 30 Days") {
        fromDateUTC = toDateUTC.addDays(-30);
    } else if (selectedRange == "Last 90 Days") {
        fromDateUTC = toDateUTC.addDays(-90);
    } else if (selectedRange == "All Time") {
        fromDateUTC = m_analyticsEngine.getFirstScrobbleDate(m_loadedScrobbles);
        if (fromDateUTC.isNull()) { // Handle case of no valid first date
            m_meanScrobblesResultLabel->setText("Error: No Date Range");
            return;
        }
    } else {
        m_meanScrobblesResultLabel->setText("Invalid range");
        return;
    }

    qDebug() << "Calculating mean for UTC range:" << fromDateUTC.toString(Qt::ISODate) << "to" << toDateUTC.toString(Qt::ISODate);
    // Call analytics engine with UTC date range
    double mean = m_analyticsEngine.getMeanScrobblesPerDay(m_loadedScrobbles, fromDateUTC, toDateUTC);
    m_meanScrobblesResultLabel->setText(QString::number(mean, 'f', 2)); // Display result
}

// Slot called when the "Find" button for last played is clicked
void MainWindow::findLastPlayedTrack() {
    if (!m_artistInput || !m_trackInput || !m_lastPlayedResultLabel) { return; }
    QString artist = m_artistInput->text().trimmed(); QString track = m_trackInput->text().trimmed();
    if (artist.isEmpty() || track.isEmpty()) { m_lastPlayedResultLabel->setText("<i style='color: red;'>Enter Artist & Track</i>"); return; }
    if (m_loadedScrobbles.isEmpty()) { m_lastPlayedResultLabel->setText("<i style='color: orange;'>No data loaded</i>"); return; }

    m_lastPlayedResultLabel->setText("<i>Searching...</i>");
    QCoreApplication::processEvents(); // Force UI update

    // Analytics engine returns UTC time
    QDateTime lastPlayedUTC = m_analyticsEngine.findLastPlayed(m_loadedScrobbles, artist, track);

    if (lastPlayedUTC.isValid()) {
        // --- Display result in LOCAL time ---
        m_lastPlayedResultLabel->setText(lastPlayedUTC.toLocalTime().toString("dd MMM yyyy 'at' hh:mm"));
    } else {
        m_lastPlayedResultLabel->setText("<i>Not found in history</i>");
    }
}

// --- UI Update Functions ---

void MainWindow::updateDisplay() {
    if (m_isLoading && m_loadedScrobbles.isEmpty()) { qDebug() << "UpdateDisplay skipped"; return; }
    int index = ui->stackedWidget->currentIndex();
    switch(index) { /* Call appropriate update function */
    case 0: updateGeneralStatsView(); break; case 1: updateDatabaseTableView(); break; case 2: updateArtistsView(); break;
    case 3: updateTracksView(); break; case 4: updateChartsView(); break; case 5: updateAboutView(); break;
    default: qWarning() << "UpdateDisplay invalid index:" << index; break;
    }
}

// MODIFIED Stats View Update
void MainWindow::updateGeneralStatsView() {
    // Check required label pointers
    bool pointersOk = m_firstScrobbleLabelValue && m_lastScrobbleLabelValue &&
                      m_longestStreakLabelValue && m_currentStreakLabelValue &&
                      m_meanScrobblesResultLabel && m_lastPlayedResultLabel;

    if (!pointersOk) { /* ... handle missing labels ... */ return; }

    QString firstDateStr = "N/A", lastDateStr = "N/A", longestStreakStr = "N/A", currentStreakStr = "N/A";

    if (!m_loadedScrobbles.isEmpty()) {
        // --- Calculate Stats (Engine now handles local time conversion internally for streaks) ---
        QDateTime firstDateUTC = m_analyticsEngine.getFirstScrobbleDate(m_loadedScrobbles);
        QDateTime lastDateUTC = m_analyticsEngine.getLastScrobbleDate(m_loadedScrobbles);
        ListeningStreak streak = m_analyticsEngine.calculateListeningStreaks(m_loadedScrobbles); // Uses local dates now

        // --- Format Results (Convert UTC dates to Local for display) ---
        if (firstDateUTC.isValid()) firstDateStr = firstDateUTC.toLocalTime().toString("dd MMM yyyy");
        if (lastDateUTC.isValid()) lastDateStr = lastDateUTC.toLocalTime().toString("dd MMM yyyy");

        // Streak dates (longestStreakEndDate, currentStreakStartDate) are already local from engine
        longestStreakStr = QString("%1 day(s)").arg(streak.longestStreakDays);
        if (streak.longestStreakDays > 0 && streak.longestStreakEndDate.isValid()) {
            longestStreakStr += QString(" (ending %1)").arg(streak.longestStreakEndDate.toString("dd MMM yy"));
        }
        currentStreakStr = QString("%1 day(s)").arg(streak.currentStreakDays);
        if (streak.currentStreakDays > 0 && streak.currentStreakStartDate.isValid()) {
            currentStreakStr += QString(" (since %1)").arg(streak.currentStreakStartDate.toString("dd MMM yy"));
        }

        // --- Update Labels ---
        m_firstScrobbleLabelValue->setText(firstDateStr);
        m_lastScrobbleLabelValue->setText(lastDateStr);
        m_longestStreakLabelValue->setText(longestStreakStr);
        m_currentStreakLabelValue->setText(currentStreakStr);

        updateMeanScrobbleCalculation(); // Trigger mean update
        m_lastPlayedResultLabel->setText(""); // Clear previous search result

    } else { /* ... Set Labels to N/A if no data ... */ }
}

void MainWindow::updateDatabaseTableView() {
    if (!m_dbTableWidget) return; m_dbTableWidget->setSortingEnabled(false); m_dbTableWidget->clearContents(); m_dbTableWidget->setRowCount(0);
    if (m_loadedScrobbles.isEmpty()) { m_dbTableWidget->setSortingEnabled(true); return; }
    SortedCounts sortedArtists = m_analyticsEngine.getTopArtists(m_loadedScrobbles, -1);
    m_dbTableWidget->setRowCount(sortedArtists.count());
    for (int row = 0; row < sortedArtists.count(); ++row) {
        const auto &pair = sortedArtists[row]; QTableWidgetItem *iRank = new QTableWidgetItem(); iRank->setData(Qt::DisplayRole, row + 1);
        QTableWidgetItem *iArtist = new QTableWidgetItem(pair.first); QTableWidgetItem *iCount = new QTableWidgetItem(); iCount->setData(Qt::DisplayRole, pair.second);
        m_dbTableWidget->setItem(row, 0, iRank); m_dbTableWidget->setItem(row, 1, iArtist); m_dbTableWidget->setItem(row, 2, iCount);
    }
    m_dbTableWidget->resizeColumnsToContents(); m_dbTableWidget->horizontalHeader()->setStretchLastSection(true); m_dbTableWidget->setSortingEnabled(true);
}

void MainWindow::updateArtistsView() {
    if (!m_artistListWidget) return; m_artistListWidget->clear(); if (m_loadedScrobbles.isEmpty()) { m_artistListWidget->addItem("(No data)"); return; }
    SortedCounts top = m_analyticsEngine.getTopArtists(m_loadedScrobbles, 100);
    if (top.isEmpty()) { m_artistListWidget->addItem("(No data)"); } else { for(const auto& p : top) m_artistListWidget->addItem(QString("%1 (%2)").arg(p.first).arg(p.second)); }
}

void MainWindow::updateTracksView() {
    if (!m_trackListWidget) return; m_trackListWidget->clear(); if (m_loadedScrobbles.isEmpty()) { m_trackListWidget->addItem("(No data)"); return; }
    SortedCounts top = m_analyticsEngine.getTopTracks(m_loadedScrobbles, 100);
    if (top.isEmpty()) { m_trackListWidget->addItem("(No data)"); } else { for(const auto& p : top) m_trackListWidget->addItem(QString("%1 (%2)").arg(p.first).arg(p.second)); }
}

void MainWindow::updateChartsView() {
    qDebug() << "Updating all charts...";
    updateArtistChart();
    updateTrackChart();
    updateHourlyChart();
    updateWeeklyChart();
}

void MainWindow::updateArtistChart() {
    if (!m_artistsChartView || !m_artistsChartView->chart()) { return; }
    QChart *chart = m_artistsChartView->chart(); chart->removeAllSeries();
    QList<QAbstractAxis*> axes; foreach(QAbstractAxis *a, chart->axes()) axes.append(a); foreach(QAbstractAxis *a, axes) chart->removeAxis(a); qDeleteAll(axes); axes.clear();
    chart->setTitle("Top 10 Artists");
    if (m_loadedScrobbles.isEmpty()) { chart->setTitle("Top 10 Artists (No Data)"); return; }
    SortedCounts topData = m_analyticsEngine.getTopArtists(m_loadedScrobbles, 10);
    if(topData.isEmpty()) { chart->setTitle("Top 10 Artists (No Data)"); return; }
    QBarSeries *series = new QBarSeries(chart); QBarSet *set = new QBarSet("Plays"); QStringList cats; int maxV = 0;
    for (int i = topData.size() - 1; i >= 0; --i) { const auto &p = topData[i]; *set << p.second; cats << p.first; if (p.second > maxV) maxV = p.second; }
    series->append(set); chart->addSeries(series); chart->setAnimationOptions(QChart::SeriesAnimations);
    QBarCategoryAxis *axX = new QBarCategoryAxis(chart); axX->append(cats); chart->addAxis(axX, Qt::AlignBottom); series->attachAxis(axX);
    QValueAxis *axY = new QValueAxis(chart); axY->setRange(0, maxV > 0 ? maxV * 1.1 : 10); axY->setLabelFormat("%d"); axY->setTitleText("Play Count"); chart->addAxis(axY, Qt::AlignLeft); series->attachAxis(axY);
    chart->legend()->setVisible(false); m_artistsChartView->setRenderHint(QPainter::Antialiasing);
}

void MainWindow::updateTrackChart() {
    if (!m_tracksChartView || !m_tracksChartView->chart()) { return; }
    QChart *chart = m_tracksChartView->chart(); chart->removeAllSeries();
    QList<QAbstractAxis*> axes; foreach(QAbstractAxis *a, chart->axes()) axes.append(a); foreach(QAbstractAxis *a, axes) chart->removeAxis(a); qDeleteAll(axes); axes.clear();
    chart->setTitle("Top 10 Tracks");
    if (m_loadedScrobbles.isEmpty()) { chart->setTitle("Top 10 Tracks (No Data)"); return; }
    SortedCounts topData = m_analyticsEngine.getTopTracks(m_loadedScrobbles, 10);
    if(topData.isEmpty()) { chart->setTitle("Top 10 Tracks (No Data)"); return; }
    QBarSeries *series = new QBarSeries(chart); QBarSet *set = new QBarSet("Plays"); QStringList cats; int maxV = 0;
    for (int i = topData.size() - 1; i >= 0; --i) {
        const auto &p = topData[i]; *set << p.second; QString label = p.first; if (label.length() > 35) label = label.left(32) + "..."; cats << label; if (p.second > maxV) maxV = p.second;
    }
    series->append(set); chart->addSeries(series); chart->setAnimationOptions(QChart::SeriesAnimations);
    QBarCategoryAxis *axX = new QBarCategoryAxis(chart); axX->append(cats); chart->addAxis(axX, Qt::AlignBottom); series->attachAxis(axX);
    QValueAxis *axY = new QValueAxis(chart); axY->setRange(0, maxV > 0 ? maxV * 1.1 : 10); axY->setLabelFormat("%d"); axY->setTitleText("Play Count"); chart->addAxis(axY, Qt::AlignLeft); series->attachAxis(axY);
    chart->legend()->setVisible(false); m_tracksChartView->setRenderHint(QPainter::Antialiasing);
}

void MainWindow::updateHourlyChart() {
    if (!m_hourlyChartView || !m_hourlyChartView->chart()) { return; }
    QChart *chart = m_hourlyChartView->chart(); chart->removeAllSeries();
    QList<QAbstractAxis*> axes; foreach(QAbstractAxis *a, chart->axes()) axes.append(a); foreach(QAbstractAxis *a, axes) chart->removeAxis(a); qDeleteAll(axes); axes.clear();
    // --- MODIFIED TITLE ---
    chart->setTitle("Scrobbles per Hour of Day (Local Time)");

    if (m_loadedScrobbles.isEmpty()) { chart->setTitle("Scrobbles per Hour (No Data)"); return; }
    // Engine now returns counts based on local hour
    QVector<int> hourlyData = m_analyticsEngine.getScrobblesPerHourOfDay(m_loadedScrobbles);
    if(hourlyData.size() != 24) { chart->setTitle("Scrobbles per Hour (Error)"); return; }

    QBarSeries *series = new QBarSeries(chart); QBarSet *set = new QBarSet("Scrobbles"); QStringList cats; int maxV = 0;
    for(int hour = 0; hour < 24; ++hour) { *set << hourlyData[hour]; cats << QStringLiteral("%1").arg(hour, 2, 10, QLatin1Char('0')); if(hourlyData[hour] > maxV) maxV = hourlyData[hour]; }
    series->append(set); chart->addSeries(series); chart->setAnimationOptions(QChart::SeriesAnimations);
    QBarCategoryAxis *axX = new QBarCategoryAxis(chart); axX->append(cats); axX->setTitleText("Hour of Day (Local)"); chart->addAxis(axX, Qt::AlignBottom); series->attachAxis(axX); // Updated Axis Title
    QValueAxis *axY = new QValueAxis(chart); axY->setRange(0, maxV > 0 ? maxV * 1.1 : 10); axY->setLabelFormat("%d"); axY->setTitleText("Total Scrobbles"); chart->addAxis(axY, Qt::AlignLeft); series->attachAxis(axY);
    chart->legend()->setVisible(false); m_hourlyChartView->setRenderHint(QPainter::Antialiasing);
}

// MODIFIED: Weekly chart title reflects local time
void MainWindow::updateWeeklyChart() {
    if (!m_weeklyChartView || !m_weeklyChartView->chart()) { return; }
    QChart *chart = m_weeklyChartView->chart(); chart->removeAllSeries();
    QList<QAbstractAxis*> axes; foreach(QAbstractAxis *a, chart->axes()) axes.append(a); foreach(QAbstractAxis *a, axes) chart->removeAxis(a); qDeleteAll(axes); axes.clear();
    // --- MODIFIED TITLE ---
    chart->setTitle("Scrobbles per Day of Week (Local Time)");

    if (m_loadedScrobbles.isEmpty()) { chart->setTitle("Scrobbles per Day (No Data)"); return; }
    // Engine now returns counts based on local day
    QVector<int> weeklyData = m_analyticsEngine.getScrobblesPerDayOfWeek(m_loadedScrobbles);
    if(weeklyData.size() != 7) { chart->setTitle("Scrobbles per Day (Error)"); return; }

    QBarSeries *series = new QBarSeries(chart); QBarSet *set = new QBarSet("Scrobbles"); QStringList cats = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"}; int maxV = 0;
    for(int day = 0; day < 7; ++day) { *set << weeklyData[day]; if(weeklyData[day] > maxV) maxV = weeklyData[day]; }
    series->append(set); chart->addSeries(series); chart->setAnimationOptions(QChart::SeriesAnimations);
    QBarCategoryAxis *axX = new QBarCategoryAxis(chart); axX->append(cats); axX->setTitleText("Day of Week"); chart->addAxis(axX, Qt::AlignBottom); series->attachAxis(axX);
    QValueAxis *axY = new QValueAxis(chart); axY->setRange(0, maxV > 0 ? maxV * 1.1 : 10); axY->setLabelFormat("%d"); axY->setTitleText("Total Scrobbles"); chart->addAxis(axY, Qt::AlignLeft); series->attachAxis(axY);
    chart->legend()->setVisible(false); m_weeklyChartView->setRenderHint(QPainter::Antialiasing);
}

void MainWindow::updateAboutView() {
    if (m_currentUserLabel) { QString u = m_settingsManager.username(); m_currentUserLabel->setText(u.isEmpty() ? "<Not Set>" : u); }
    else { qWarning() << "currentUserLabel null!"; }
}
