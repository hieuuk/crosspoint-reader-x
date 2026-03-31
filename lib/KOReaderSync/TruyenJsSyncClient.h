#pragma once
#include <string>

/**
 * Progress data from TruyenJs2 device-sync API.
 */
struct TruyenJsProgress {
  int chapterIndex;          // Chapter position (1-based in TruyenJs2)
  float progressInChapter;   // 0.0 to 1.0 within the chapter
  std::string device;        // Device name
  std::string updatedAt;     // ISO timestamp
};

/**
 * HTTP client for TruyenJs2 device-sync API.
 *
 * API Endpoints:
 *   GET  /device-sync/:storyId       - Get device sync progress
 *   PUT  /device-sync/:storyId       - Update device sync progress
 *   GET  /device-sync/:storyId/web   - Get web reading progress in sync format
 */
class TruyenJsSyncClient {
 public:
  enum Error { OK = 0, NETWORK_ERROR, SERVER_ERROR, JSON_ERROR, NOT_FOUND };

  /**
   * Get device sync progress for a story.
   * @param syncUrl Base sync URL (e.g., "https://server.com/api/device-sync")
   * @param storyId The story identifier (dc:identifier from EPUB)
   * @param bookPosition Book position for series (empty string if standalone)
   * @param outProgress Output: the progress data
   * @return OK on success, NOT_FOUND if no progress exists
   */
  static Error getProgress(const std::string& syncUrl, const std::string& storyId, const std::string& bookPosition,
                           TruyenJsProgress& outProgress);

  /**
   * Update device sync progress for a story.
   * @param syncUrl Base sync URL
   * @param storyId The story identifier
   * @param progress The progress data to upload
   * @param bookPosition Book position for series (empty string if standalone)
   * @return OK on success
   */
  static Error updateProgress(const std::string& syncUrl, const std::string& storyId,
                              const TruyenJsProgress& progress, const std::string& bookPosition);

  /**
   * Get web reading progress for comparison.
   * @param syncUrl Base sync URL
   * @param storyId The story identifier
   * @param bookPosition Book position for series (empty string if standalone)
   * @param outProgress Output: the web progress data
   * @return OK on success
   */
  static Error getWebProgress(const std::string& syncUrl, const std::string& storyId, const std::string& bookPosition,
                              TruyenJsProgress& outProgress);

  static const char* errorString(Error error);
};
