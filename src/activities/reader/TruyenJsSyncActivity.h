#pragma once
#include <Epub.h>

#include <functional>
#include <memory>

#include "TruyenJsSyncClient.h"
#include "activities/Activity.h"

/**
 * Activity for syncing reading progress with TruyenJs2 server.
 *
 * Uses the crosspoint:syncUrl and dc:identifier embedded in the EPUB metadata
 * to identify the server and story.
 *
 * Flow:
 * 1. Connect to WiFi (if not connected)
 * 2. Fetch both device and web progress from TruyenJs2
 * 3. Show comparison and options (Apply/Upload)
 * 4. Apply remote progress or upload local progress
 */
class TruyenJsSyncActivity final : public Activity {
 public:
  explicit TruyenJsSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                const std::shared_ptr<Epub>& epub, int currentSpineIndex, int currentPage,
                                int totalPagesInSpine)
      : Activity("TruyenJsSync", renderer, mappedInput),
        epub(epub),
        currentSpineIndex(currentSpineIndex),
        currentPage(currentPage),
        totalPagesInSpine(totalPagesInSpine) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == SYNCING; }

 private:
  enum State {
    WIFI_SELECTION,
    CONNECTING,
    SYNCING,
    SHOWING_RESULT,
    UPLOADING,
    UPLOAD_COMPLETE,
    NO_REMOTE_PROGRESS,
    SYNC_FAILED,
  };

  std::shared_ptr<Epub> epub;
  int currentSpineIndex;
  int currentPage;
  int totalPagesInSpine;

  State state = WIFI_SELECTION;
  std::string statusMessage;

  // Remote progress (from device sync or web)
  bool hasDeviceProgress = false;
  bool hasWebProgress = false;
  TruyenJsProgress deviceProgress{};
  TruyenJsProgress webProgress{};

  // Which remote source to show (prefer web if more recent)
  TruyenJsProgress* activeRemoteProgress = nullptr;

  // Selection: 0=Apply, 1=Upload
  int selectedOption = 0;

  // Convert spine position to TruyenJs chapter index
  // TruyenJs2 chapter positions are 1-based, and the EPUB's first spine item is the cover page
  // So spine index 1 = chapter 1, spine index 2 = chapter 2, etc.
  int spineToChapterIndex(int spineIndex) const;
  int chapterToSpineIndex(int chapterIndex) const;
  float localProgressInChapter() const;

  void onWifiSelectionComplete(bool success);
  void performSync();
  void performUpload();
};
