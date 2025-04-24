# Last.fm Scrobble Analyzer

A desktop application built with C++ and the Qt framework to fetch, store, analyze, and visualize your Last.fm scrobble history.

## Description

This application connects to the Last.fm API to download your music listening history (scrobbles). It stores the data efficiently in a local database (weekly JSON files) and provides various tools to analyze and visualize your listening habits, including:

*   Overall statistics (first/last scrobble, streaks, average plays/day)
*   Top artist and track rankings
*   Charts showing listening patterns over time (hourly, weekly)
*   Ability to search for the last time you played a specific track.

It supports fetching your complete history, performing incremental updates, and resuming interrupted downloads.

## Features

*   **Full History Fetch:** Download your entire Last.fm scrobble history.
*   **Incremental Updates:** Fetch only new scrobbles since the last sync.
*   **Download Resumption:** Resumes fetching from the last successfully saved page if the initial download is interrupted.
*   **Local Database:** Stores scrobbles locally in JSON files organized by week per user.
*   **Dashboard Stats:** View total scrobbles, date range, average scrobbles per day, and listening streaks.
*   **Last Played Finder:** Search for the last time you listened to a specific artist/track combination.
*   **Top Lists:** See your most played artists and tracks.
*   **Charts:**
    *   Top 10 Artists (Bar Chart)
    *   Top 10 Tracks (Bar Chart)
    *   Scrobbles per Hour of Day (Bar Chart)
    *   Scrobbles per Day of Week (Bar Chart)
*   **Background Operations:** Fetching and saving happen in background threads to keep the UI responsive.
*   **User Configuration:** Easily set and change your Last.fm username and API key.

## Prerequisites

*   **Qt Framework:** Version 6.x (or 5.15+ with potential minor adjustments). Requires the following modules:
    *   `Core`
    *   `Gui`
    *   `Widgets`
    *   `Network`
    *   `Charts`
*   **CMake:** Version 3.16 or higher.
*   **C++ Compiler:** A modern C++ compiler compatible with your Qt version (GCC, Clang, MSVC).
*   **Last.fm Account:** You need a Last.fm account.
*   **Last.fm API Key:** You must obtain a free API key from Last.fm: [Get an API Account](https://www.last.fm/api/account/create)

## Building

This project uses CMake.

**1. Clone the Repository:**

```bash
git clone https://github.com/gherkin21/LFMstats.git
cd your-repo
