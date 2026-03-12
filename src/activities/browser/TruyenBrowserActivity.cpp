#include "TruyenBrowserActivity.h"

#include <ArduinoJson.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/StringUtils.h"
#include "util/UrlUtils.h"

namespace {
constexpr int PAGE_ITEMS = 23;
constexpr int API_PAGE_SIZE = 20;
}  // namespace

void TruyenBrowserActivity::loadLibraries() {
  libraries.clear();

  auto addLibrary = [this](const char* url, const char* name, int index) {
    if (strlen(url) > 0) {
      TruyenLibrary lib;
      lib.url = url;
      if (strlen(name) > 0) {
        lib.name = name;
      } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "Library %d", index);
        lib.name = buf;
      }
      libraries.push_back(std::move(lib));
    }
  };

  addLibrary(SETTINGS.truyenServerUrl, SETTINGS.truyenServerName, 1);
  addLibrary(SETTINGS.truyenServerUrl2, SETTINGS.truyenServerName2, 2);
  addLibrary(SETTINGS.truyenServerUrl3, SETTINGS.truyenServerName3, 3);
}

void TruyenBrowserActivity::selectLibrary(int index) {
  selectedUrl = libraries[index].url;
  selectedName = libraries[index].name;
  libraryIndex = index;
  state = BrowserState::CHECK_WIFI;
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();
  checkAndConnectWifi();
}

void TruyenBrowserActivity::onEnter() {
  Activity::onEnter();

  loadLibraries();
  entries.clear();
  selectorIndex = 0;
  libraryIndex = 0;
  currentPage = 1;
  totalCount = 0;
  errorMessage.clear();

  if (libraries.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_NO_SERVER_URL);
    requestUpdate();
    return;
  }

  if (libraries.size() == 1) {
    // Only one library configured, skip picker
    selectLibrary(0);
  } else {
    state = BrowserState::LIBRARY_SELECTION;
    requestUpdate();
  }
}

void TruyenBrowserActivity::onExit() {
  Activity::onExit();

  WiFi.mode(WIFI_OFF);
  entries.clear();
  libraries.clear();
}

void TruyenBrowserActivity::loop() {
  if (state == BrowserState::WIFI_SELECTION) {
    return;
  }

  if (state == BrowserState::LIBRARY_SELECTION) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      selectLibrary(libraryIndex);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }

    buttonNavigator.onNextRelease([this] {
      libraryIndex = ButtonNavigator::nextIndex(libraryIndex, libraries.size());
      requestUpdate();
    });

    buttonNavigator.onPreviousRelease([this] {
      libraryIndex = ButtonNavigator::previousIndex(libraryIndex, libraries.size());
      requestUpdate();
    });
    return;
  }

  if (state == BrowserState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        state = BrowserState::LOADING;
        statusMessage = tr(STR_TRUYEN_FETCHING);
        requestUpdate();
        fetchStories(currentPage);
      } else {
        launchWifiSelection();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (libraries.size() > 1) {
        // Go back to library picker
        state = BrowserState::LIBRARY_SELECTION;
        entries.clear();
        selectorIndex = 0;
        currentPage = 1;
        totalCount = 0;
        requestUpdate();
      } else {
        onGoHome();
      }
    }
    return;
  }

  if (state == BrowserState::CHECK_WIFI) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (libraries.size() > 1) {
        state = BrowserState::LIBRARY_SELECTION;
        requestUpdate();
      } else {
        onGoHome();
      }
    }
    return;
  }

  if (state == BrowserState::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (libraries.size() > 1) {
        state = BrowserState::LIBRARY_SELECTION;
        requestUpdate();
      } else {
        onGoHome();
      }
    }
    return;
  }

  if (state == BrowserState::DOWNLOADING) {
    return;
  }

  if (state == BrowserState::BROWSING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!entries.empty()) {
        downloadStory(entries[selectorIndex]);
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (currentPage > 1) {
        // Go to previous API page
        currentPage--;
        state = BrowserState::LOADING;
        statusMessage = tr(STR_TRUYEN_FETCHING);
        requestUpdate();
        fetchStories(currentPage);
      } else if (libraries.size() > 1) {
        // Go back to library picker
        state = BrowserState::LIBRARY_SELECTION;
        entries.clear();
        selectorIndex = 0;
        currentPage = 1;
        totalCount = 0;
        requestUpdate();
      } else {
        onGoHome();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      // Next API page
      const int totalPages = (totalCount + API_PAGE_SIZE - 1) / API_PAGE_SIZE;
      if (currentPage < totalPages) {
        currentPage++;
        state = BrowserState::LOADING;
        statusMessage = tr(STR_TRUYEN_FETCHING);
        requestUpdate();
        fetchStories(currentPage);
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      // Previous API page
      if (currentPage > 1) {
        currentPage--;
        state = BrowserState::LOADING;
        statusMessage = tr(STR_TRUYEN_FETCHING);
        requestUpdate();
        fetchStories(currentPage);
      }
    }

    if (!entries.empty()) {
      buttonNavigator.onNextRelease([this] {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, entries.size());
        requestUpdate();
      });

      buttonNavigator.onPreviousRelease([this] {
        selectorIndex = ButtonNavigator::previousIndex(selectorIndex, entries.size());
        requestUpdate();
      });

      buttonNavigator.onNextContinuous([this] {
        selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, entries.size(), PAGE_ITEMS);
        requestUpdate();
      });

      buttonNavigator.onPreviousContinuous([this] {
        selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, entries.size(), PAGE_ITEMS);
        requestUpdate();
      });
    }
  }
}

void TruyenBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (state == BrowserState::LIBRARY_SELECTION) {
    renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_TRUYEN_SELECT_LIBRARY), true, EpdFontFamily::BOLD);

    for (size_t i = 0; i < libraries.size(); i++) {
      if (i == static_cast<size_t>(libraryIndex)) {
        renderer.fillRect(0, 60 + i * 40 - 2, pageWidth - 1, 40);
      }
      auto item = renderer.truncatedText(UI_10_FONT_ID, libraries[i].name.c_str(), pageWidth - 40);
      renderer.drawText(UI_10_FONT_ID, 20, 60 + i * 40, item.c_str(),
                        i != static_cast<size_t>(libraryIndex));
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  renderer.drawCenteredText(UI_12_FONT_ID, 15, selectedName.c_str(), true, EpdFontFamily::BOLD);

  if (state == BrowserState::CHECK_WIFI || state == BrowserState::LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_DOWNLOADING));
    const auto maxWidth = pageWidth - 40;
    auto title = renderer.truncatedText(UI_10_FONT_ID, statusMessage.c_str(), maxWidth);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, title.c_str());
    if (downloadTotal > 0) {
      const int barWidth = pageWidth - 100;
      constexpr int barHeight = 20;
      constexpr int barX = 50;
      const int barY = pageHeight / 2 + 20;
      GUI.drawProgressBar(renderer, Rect{barX, barY, barWidth, barHeight}, downloadProgress, downloadTotal);
    }
    renderer.displayBuffer();
    return;
  }

  // Browsing state - show page info in header
  {
    const int totalPages = (totalCount + API_PAGE_SIZE - 1) / API_PAGE_SIZE;
    char pageBuf[32];
    snprintf(pageBuf, sizeof(pageBuf), "%d / %d", currentPage, totalPages);
    renderer.drawText(UI_10_FONT_ID, pageWidth - 80, 15, pageBuf, true);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DOWNLOAD), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (entries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_TRUYEN_NO_STORIES));
    renderer.displayBuffer();
    return;
  }

  const auto pageStartIndex = selectorIndex / PAGE_ITEMS * PAGE_ITEMS;
  renderer.fillRect(0, 60 + (selectorIndex % PAGE_ITEMS) * 30 - 2, pageWidth - 1, 30);

  for (size_t i = pageStartIndex; i < entries.size() && i < static_cast<size_t>(pageStartIndex + PAGE_ITEMS); i++) {
    const auto& entry = entries[i];

    std::string displayText = entry.title;
    if (!entry.author.empty()) {
      displayText += " - " + entry.author;
    }

    auto item = renderer.truncatedText(UI_10_FONT_ID, displayText.c_str(), renderer.getScreenWidth() - 40);
    renderer.drawText(UI_10_FONT_ID, 20, 60 + (i % PAGE_ITEMS) * 30, item.c_str(),
                      i != static_cast<size_t>(selectorIndex));
  }

  renderer.displayBuffer();
}

void TruyenBrowserActivity::fetchStories(int page) {
  if (selectedUrl.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_NO_SERVER_URL);
    requestUpdate();
    return;
  }

  // Build API URL: <serverUrl>/api/stories/lite?page=N&pageSize=20&sortBy=LastRead&isDesc=true
  std::string baseUrl = selectedUrl;
  // Always use HTTPS - most servers redirect HTTP to HTTPS which fails on ESP32
  if (baseUrl.find("://") == std::string::npos) {
    baseUrl = "https://" + baseUrl;
  } else if (baseUrl.rfind("http://", 0) == 0) {
    baseUrl = "https://" + baseUrl.substr(7);
  }
  // Ensure no trailing slash
  if (!baseUrl.empty() && baseUrl.back() == '/') {
    baseUrl.pop_back();
  }
  std::string url = baseUrl;
  char queryBuf[128];
  snprintf(queryBuf, sizeof(queryBuf), "/api/stories/lite?page=%d&pageSize=%d&sortBy=LastRead&isDesc=true", page,
           API_PAGE_SIZE);
  url += queryBuf;

  LOG_DBG("TRUYEN", "Fetching: %s", url.c_str());

  std::string responseBody;
  if (!HttpDownloader::fetchUrl(url, responseBody)) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_TRUYEN_FETCH_FAILED);
    requestUpdate();
    return;
  }

  if (!parseStoriesJson(responseBody)) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_TRUYEN_PARSE_FAILED);
    requestUpdate();
    return;
  }

  LOG_DBG("TRUYEN", "Found %d stories (total: %d)", entries.size(), totalCount);
  selectorIndex = 0;

  if (entries.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_TRUYEN_NO_STORIES);
    requestUpdate();
    return;
  }

  state = BrowserState::BROWSING;
  requestUpdate();
}

bool TruyenBrowserActivity::parseStoriesJson(const std::string& json) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);

  if (error) {
    LOG_ERR("TRUYEN", "JSON parse error: %s", error.c_str());
    return false;
  }

  totalCount = doc["totalCount"] | 0;

  JsonArray items = doc["items"];
  if (items.isNull()) {
    LOG_ERR("TRUYEN", "No 'items' array in response");
    return false;
  }

  entries.clear();
  entries.reserve(items.size());

  for (JsonObject item : items) {
    TruyenEntry entry;
    entry.id = item["id"] | "";
    entry.title = item["Title"] | "";
    entry.author = item["Author"] | "";
    entry.chapterCount = item["ChapterCount"] | 0;

    if (!entry.id.empty() && !entry.title.empty()) {
      entries.push_back(std::move(entry));
    }
  }

  return true;
}

void TruyenBrowserActivity::downloadStory(const TruyenEntry& story) {
  state = BrowserState::DOWNLOADING;
  statusMessage = story.title;
  downloadProgress = 0;
  downloadTotal = 0;
  requestUpdate(true);

  // Build EPUB export URL: <serverUrl>/api/stories/<id>/export/epub
  std::string baseUrl = selectedUrl;
  if (baseUrl.find("://") == std::string::npos) {
    baseUrl = "https://" + baseUrl;
  } else if (baseUrl.rfind("http://", 0) == 0) {
    baseUrl = "https://" + baseUrl.substr(7);
  }
  if (!baseUrl.empty() && baseUrl.back() == '/') {
    baseUrl.pop_back();
  }
  std::string url = baseUrl;
  url += "/api/stories/" + story.id + "/export/epub";

  // Create sanitized filename
  std::string baseName = story.title;
  if (!story.author.empty()) {
    baseName += " - " + story.author;
  }
  std::string filename = "/" + StringUtils::sanitizeFilename(baseName) + ".epub";

  LOG_DBG("TRUYEN", "Downloading: %s -> %s", url.c_str(), filename.c_str());

  const auto result =
      HttpDownloader::downloadToFile(url, filename, [this](const size_t downloaded, const size_t total) {
        downloadProgress = downloaded;
        downloadTotal = total;
        requestUpdate(true);
      });

  if (result == HttpDownloader::OK) {
    LOG_DBG("TRUYEN", "Download complete: %s", filename.c_str());

    // Clear any existing cache for this file
    Epub epub(filename, "/.crosspoint");
    epub.clearCache();
    LOG_DBG("TRUYEN", "Cleared cache for: %s", filename.c_str());

    state = BrowserState::BROWSING;
    requestUpdate();
  } else {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_TRUYEN_EXPORT_FAILED);
    requestUpdate();
  }
}

void TruyenBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_TRUYEN_FETCHING);
    requestUpdate();
    fetchStories(currentPage);
    return;
  }

  launchWifiSelection();
}

void TruyenBrowserActivity::launchWifiSelection() {
  state = BrowserState::WIFI_SELECTION;
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void TruyenBrowserActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    LOG_DBG("TRUYEN", "WiFi connected, fetching stories");
    state = BrowserState::LOADING;
    statusMessage = tr(STR_TRUYEN_FETCHING);
    requestUpdate(true);
    fetchStories(currentPage);
  } else {
    LOG_DBG("TRUYEN", "WiFi selection cancelled/failed");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    state = BrowserState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
