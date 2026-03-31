#include "TruyenJsSyncActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
void wifiOff() {
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}
}  // namespace

int TruyenJsSyncActivity::spineToChapterIndex(const int spineIndex) const {
  // The EPUB exported by TruyenJs2 has:
  // spine[0] = cover.xhtml
  // spine[1] = chapter_0001.xhtml (chapter position 1)
  // spine[2] = chapter_0002.xhtml (chapter position 2)
  // So chapter position = spineIndex (since cover is spine 0, chapter 1 is spine 1)
  return spineIndex;
}

int TruyenJsSyncActivity::chapterToSpineIndex(const int chapterIndex) const {
  // Inverse of above
  return chapterIndex;
}

float TruyenJsSyncActivity::localProgressInChapter() const {
  if (totalPagesInSpine <= 0) return 0.0f;
  return static_cast<float>(currentPage) / static_cast<float>(totalPagesInSpine);
}

void TruyenJsSyncActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  LOG_DBG("TJSync", "WiFi connected, starting sync");
  {
    RenderLock lock(*this);
    state = SYNCING;
    statusMessage = tr(STR_FETCH_PROGRESS);
  }
  requestUpdate(true);

  performSync();
}

void TruyenJsSyncActivity::performSync() {
  const std::string& syncUrl = epub->getSyncUrl();
  const std::string& storyId = epub->getIdentifier();
  const std::string& bookPos = epub->getBookPosition();

  LOG_DBG("TJSync", "Syncing story=%s, syncUrl=%s, book=%s", storyId.c_str(), syncUrl.c_str(), bookPos.c_str());

  // Fetch device sync progress
  auto result = TruyenJsSyncClient::getProgress(syncUrl, storyId, bookPos, deviceProgress);
  if (result == TruyenJsSyncClient::OK) {
    hasDeviceProgress = true;
    LOG_DBG("TJSync", "Device progress: ch=%d, prog=%.2f%%", deviceProgress.chapterIndex,
            deviceProgress.progressInChapter * 100);
  }

  // Fetch web reading progress
  result = TruyenJsSyncClient::getWebProgress(syncUrl, storyId, bookPos, webProgress);
  if (result == TruyenJsSyncClient::OK) {
    hasWebProgress = true;
    LOG_DBG("TJSync", "Web progress: ch=%d, prog=%.2f%%", webProgress.chapterIndex,
            webProgress.progressInChapter * 100);
  }

  if (!hasDeviceProgress && !hasWebProgress) {
    {
      RenderLock lock(*this);
      state = NO_REMOTE_PROGRESS;
    }
    requestUpdate(true);
    return;
  }

  // Determine which remote progress to show (prefer the most recent)
  if (hasWebProgress && hasDeviceProgress) {
    // Compare: higher chapter or same chapter with higher progress wins
    if (webProgress.chapterIndex > deviceProgress.chapterIndex ||
        (webProgress.chapterIndex == deviceProgress.chapterIndex &&
         webProgress.progressInChapter > deviceProgress.progressInChapter)) {
      activeRemoteProgress = &webProgress;
    } else {
      activeRemoteProgress = &deviceProgress;
    }
  } else if (hasWebProgress) {
    activeRemoteProgress = &webProgress;
  } else {
    activeRemoteProgress = &deviceProgress;
  }

  // Determine default selection based on which is further
  const int localChapter = spineToChapterIndex(currentSpineIndex);
  const float localProg = localProgressInChapter();

  if (localChapter > activeRemoteProgress->chapterIndex ||
      (localChapter == activeRemoteProgress->chapterIndex && localProg > activeRemoteProgress->progressInChapter)) {
    selectedOption = 1;  // Upload (local is ahead)
  } else {
    selectedOption = 0;  // Apply (remote is ahead)
  }

  {
    RenderLock lock(*this);
    state = SHOWING_RESULT;
  }
  requestUpdate(true);
}

void TruyenJsSyncActivity::performUpload() {
  {
    RenderLock lock(*this);
    state = UPLOADING;
    statusMessage = tr(STR_UPLOAD_PROGRESS);
  }
  requestUpdateAndWait();

  const std::string& syncUrl = epub->getSyncUrl();
  const std::string& storyId = epub->getIdentifier();
  const std::string& bookPos = epub->getBookPosition();

  TruyenJsProgress progress;
  progress.chapterIndex = spineToChapterIndex(currentSpineIndex);
  progress.progressInChapter = localProgressInChapter();

  const auto result = TruyenJsSyncClient::updateProgress(syncUrl, storyId, progress, bookPos);

  if (result != TruyenJsSyncClient::OK) {
    wifiOff();
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = TruyenJsSyncClient::errorString(result);
    }
    requestUpdate();
    return;
  }

  wifiOff();
  {
    RenderLock lock(*this);
    state = UPLOAD_COMPLETE;
  }
  requestUpdate(true);
}

void TruyenJsSyncActivity::onEnter() {
  Activity::onEnter();

  if (!epub->hasSyncUrl()) {
    state = SYNC_FAILED;
    statusMessage = "No sync URL in EPUB";
    requestUpdate();
    return;
  }

  // Check if already connected
  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }

  // Launch WiFi selection
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void TruyenJsSyncActivity::onExit() {
  Activity::onExit();
  wifiOff();
}

void TruyenJsSyncActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "TruyenJs Sync", true, EpdFontFamily::BOLD);

  if (state == SYNCING || state == UPLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, 300, statusMessage.c_str(), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (state == SHOWING_RESULT && activeRemoteProgress) {
    renderer.drawCenteredText(UI_10_FONT_ID, 120, tr(STR_PROGRESS_FOUND), true, EpdFontFamily::BOLD);

    // Show chapter numbers directly (TruyenJs2 EPUBs may not have TOC entries for all chapters)
    char remoteChapterBuf[32];
    snprintf(remoteChapterBuf, sizeof(remoteChapterBuf), "Chapter %d", activeRemoteProgress->chapterIndex);
    const std::string remoteChapter = remoteChapterBuf;
    char localChapterBuf[32];
    snprintf(localChapterBuf, sizeof(localChapterBuf), "Chapter %d", spineToChapterIndex(currentSpineIndex));
    const std::string localChapter = localChapterBuf;

    // Remote progress
    renderer.drawText(UI_10_FONT_ID, 20, 160, tr(STR_REMOTE_LABEL), true);
    char remoteChapterStr[128];
    snprintf(remoteChapterStr, sizeof(remoteChapterStr), "  %s", remoteChapter.c_str());
    renderer.drawText(UI_10_FONT_ID, 20, 185, remoteChapterStr);
    char remoteProgStr[64];
    snprintf(remoteProgStr, sizeof(remoteProgStr), "  %.0f%% in chapter", activeRemoteProgress->progressInChapter * 100);
    renderer.drawText(UI_10_FONT_ID, 20, 210, remoteProgStr);

    if (!activeRemoteProgress->device.empty()) {
      char deviceStr[64];
      snprintf(deviceStr, sizeof(deviceStr), tr(STR_DEVICE_FROM_FORMAT), activeRemoteProgress->device.c_str());
      renderer.drawText(UI_10_FONT_ID, 20, 235, deviceStr);
    }

    // Local progress
    renderer.drawText(UI_10_FONT_ID, 20, 270, tr(STR_LOCAL_LABEL), true);
    char localChapterStr[128];
    snprintf(localChapterStr, sizeof(localChapterStr), "  %s", localChapter.c_str());
    renderer.drawText(UI_10_FONT_ID, 20, 295, localChapterStr);
    char localProgStr[64];
    snprintf(localProgStr, sizeof(localProgStr), tr(STR_PAGE_TOTAL_OVERALL_FORMAT), currentPage + 1, totalPagesInSpine,
             localProgressInChapter() * 100);
    renderer.drawText(UI_10_FONT_ID, 20, 320, localProgStr);

    const int optionY = 350;
    const int optionHeight = 30;

    if (selectedOption == 0) {
      renderer.fillRect(0, optionY - 2, pageWidth - 1, optionHeight);
    }
    renderer.drawText(UI_10_FONT_ID, 20, optionY, tr(STR_APPLY_REMOTE), selectedOption != 0);

    if (selectedOption == 1) {
      renderer.fillRect(0, optionY + optionHeight - 2, pageWidth - 1, optionHeight);
    }
    renderer.drawText(UI_10_FONT_ID, 20, optionY + optionHeight, tr(STR_UPLOAD_LOCAL), selectedOption != 1);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, 280, tr(STR_NO_REMOTE_MSG), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, 320, tr(STR_UPLOAD_PROMPT));

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_UPLOAD), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == UPLOAD_COMPLETE) {
    renderer.drawCenteredText(UI_10_FONT_ID, 300, tr(STR_UPLOAD_SUCCESS), true, EpdFontFamily::BOLD);

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SYNC_FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, 280, tr(STR_SYNC_FAILED_MSG), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, 320, statusMessage.c_str());

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void TruyenJsSyncActivity::loop() {
  if (state == SYNC_FAILED || state == UPLOAD_COMPLETE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    }
    return;
  }

  if (state == SHOWING_RESULT) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      selectedOption = (selectedOption + 1) % 2;
      requestUpdate();
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedOption == 0 && activeRemoteProgress) {
        // Apply remote progress - convert chapter index back to spine index
        const int targetSpine = chapterToSpineIndex(activeRemoteProgress->chapterIndex);
        // Pass progressInChapter so the reader can position within the chapter after rendering
        SyncResult syncResult;
        syncResult.spineIndex = targetSpine;
        syncResult.page = 0;
        syncResult.progressInSpine = activeRemoteProgress->progressInChapter;
        setResult(syncResult);
        finish();
      } else if (selectedOption == 1) {
        performUpload();
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    }
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      performUpload();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    }
    return;
  }
}
