#include "TruyenJsSyncClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Logging.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace {
constexpr char DEVICE_NAME[] = "CrossPoint";

bool isHttpsUrl(const std::string& url) { return url.rfind("https://", 0) == 0; }

void beginHttp(HTTPClient& http, std::unique_ptr<WiFiClientSecure>& secureClient, WiFiClient& plainClient,
               const std::string& url) {
  if (isHttpsUrl(url)) {
    secureClient.reset(new WiFiClientSecure);
    secureClient->setInsecure();
    http.begin(*secureClient, url.c_str());
  } else {
    http.begin(plainClient, url.c_str());
  }
}

std::string buildUrl(const std::string& syncUrl, const std::string& storyId, const std::string& suffix,
                     const std::string& bookPosition) {
  std::string url = syncUrl + "/" + storyId + suffix;
  if (!bookPosition.empty()) {
    url += (suffix.find('?') != std::string::npos ? "&" : "?");
    url += "book=" + bookPosition;
  }
  return url;
}

bool parseProgressJson(const String& responseBody, TruyenJsProgress& outProgress) {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, responseBody);
  if (error) {
    LOG_ERR("TJSync", "JSON parse failed: %s", error.c_str());
    return false;
  }

  // null response means no progress found
  if (doc.isNull()) {
    return false;
  }

  outProgress.chapterIndex = doc["chapterIndex"].as<int>();
  outProgress.progressInChapter = doc["progressInChapter"].as<float>();
  outProgress.device = doc["device"].as<std::string>();
  outProgress.updatedAt = doc["updatedAt"].as<std::string>();
  return true;
}
}  // namespace

TruyenJsSyncClient::Error TruyenJsSyncClient::getProgress(const std::string& syncUrl, const std::string& storyId,
                                                           const std::string& bookPosition,
                                                           TruyenJsProgress& outProgress) {
  const std::string url = buildUrl(syncUrl, storyId, "", bookPosition);
  LOG_DBG("TJSync", "Getting progress: %s", url.c_str());

  HTTPClient http;
  std::unique_ptr<WiFiClientSecure> secureClient;
  WiFiClient plainClient;
  beginHttp(http, secureClient, plainClient, url);

  const int httpCode = http.GET();

  if (httpCode == 200) {
    String responseBody = http.getString();
    http.end();

    if (!parseProgressJson(responseBody, outProgress)) {
      return NOT_FOUND;
    }

    LOG_DBG("TJSync", "Got device progress: chapter=%d, progress=%.2f%%, device=%s", outProgress.chapterIndex,
            outProgress.progressInChapter * 100, outProgress.device.c_str());
    return OK;
  }

  http.end();
  LOG_DBG("TJSync", "Get progress response: %d", httpCode);

  if (httpCode == 404) return NOT_FOUND;
  if (httpCode < 0) return NETWORK_ERROR;
  return SERVER_ERROR;
}

TruyenJsSyncClient::Error TruyenJsSyncClient::updateProgress(const std::string& syncUrl, const std::string& storyId,
                                                              const TruyenJsProgress& progress,
                                                              const std::string& bookPosition) {
  const std::string url = buildUrl(syncUrl, storyId, "", "");
  LOG_DBG("TJSync", "Updating progress: %s", url.c_str());

  HTTPClient http;
  std::unique_ptr<WiFiClientSecure> secureClient;
  WiFiClient plainClient;
  beginHttp(http, secureClient, plainClient, url);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["chapterIndex"] = progress.chapterIndex;
  doc["progressInChapter"] = progress.progressInChapter;
  doc["device"] = DEVICE_NAME;
  if (!bookPosition.empty()) {
    doc["bookPosition"] = atoi(bookPosition.c_str());
  }

  std::string body;
  serializeJson(doc, body);

  LOG_DBG("TJSync", "Request body: %s", body.c_str());

  const int httpCode = http.PUT(body.c_str());
  http.end();

  LOG_DBG("TJSync", "Update progress response: %d", httpCode);

  if (httpCode == 200) return OK;
  if (httpCode < 0) return NETWORK_ERROR;
  return SERVER_ERROR;
}

TruyenJsSyncClient::Error TruyenJsSyncClient::getWebProgress(const std::string& syncUrl, const std::string& storyId,
                                                              const std::string& bookPosition,
                                                              TruyenJsProgress& outProgress) {
  const std::string url = buildUrl(syncUrl, storyId, "/web", bookPosition);
  LOG_DBG("TJSync", "Getting web progress: %s", url.c_str());

  HTTPClient http;
  std::unique_ptr<WiFiClientSecure> secureClient;
  WiFiClient plainClient;
  beginHttp(http, secureClient, plainClient, url);

  const int httpCode = http.GET();

  if (httpCode == 200) {
    String responseBody = http.getString();
    http.end();

    if (!parseProgressJson(responseBody, outProgress)) {
      return NOT_FOUND;
    }

    LOG_DBG("TJSync", "Got web progress: chapter=%d, progress=%.2f%%", outProgress.chapterIndex,
            outProgress.progressInChapter * 100);
    return OK;
  }

  http.end();
  LOG_DBG("TJSync", "Get web progress response: %d", httpCode);

  if (httpCode == 404) return NOT_FOUND;
  if (httpCode < 0) return NETWORK_ERROR;
  return SERVER_ERROR;
}

const char* TruyenJsSyncClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NETWORK_ERROR:
      return "Network error";
    case SERVER_ERROR:
      return "Server error";
    case JSON_ERROR:
      return "JSON parse error";
    case NOT_FOUND:
      return "No progress found";
    default:
      return "Unknown error";
  }
}
