#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

/**
 * A story entry from the TruyenJs2 server.
 */
struct TruyenEntry {
  std::string id;
  std::string title;
  std::string author;
  int chapterCount = 0;
};

/**
 * A configured library (server URL + display name).
 */
struct TruyenLibrary {
  std::string url;
  std::string name;
};

/**
 * Activity for browsing and downloading stories from a TruyenJs2 server.
 * Supports multiple configured libraries. If more than one library is configured,
 * shows a library picker before connecting.
 */
class TruyenBrowserActivity final : public Activity {
 public:
  enum class BrowserState {
    LIBRARY_SELECTION,
    CHECK_WIFI,
    WIFI_SELECTION,
    LOADING,
    BROWSING,
    DOWNLOADING,
    ERROR
  };

  explicit TruyenBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TruyenBrowser", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  BrowserState state = BrowserState::LIBRARY_SELECTION;
  std::vector<TruyenLibrary> libraries;
  int libraryIndex = 0;
  std::string selectedUrl;
  std::string selectedName;
  std::vector<TruyenEntry> entries;
  int selectorIndex = 0;
  int currentPage = 1;
  int totalCount = 0;
  std::string errorMessage;
  std::string statusMessage;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;

  void loadLibraries();
  void selectLibrary(int index);
  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchStories(int page);
  bool parseStoriesJson(const std::string& json);
  void downloadStory(const TruyenEntry& story);
  bool preventAutoSleep() override { return true; }
};
