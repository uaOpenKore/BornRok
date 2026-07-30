#pragma once
// Minimal HTTP(S) client over libcurl for the patcher: fetch the manifest into
// memory and stream files to disk with progress. Includes the Google-Drive
// download flow (confirm-token interstitial for large files).
#include <functional>
#include <string>

#include "core/Types.hpp"

namespace uaro::net {

// Progress callback: (bytesDownloaded, totalBytes-or-0-if-unknown). Return false
// to abort the transfer.
using HttpProgress = std::function<bool(u64 downloaded, u64 total)>;

struct HttpResult {
    bool ok = false;
    long status = 0;       // HTTP status code (0 if the request never completed)
    std::string error;     // libcurl/error message when !ok
};

// Call once at startup (curl_global_init). Safe to call multiple times.
void httpGlobalInit();

// GET `url` into `out` (follows redirects). For small payloads like downloads.list.
HttpResult fetchToString(const std::string& url, std::string& out, int timeoutSec = 30);

// GET `url`, streaming the body to `destPath`. `progress` may be empty.
// resume=false: create/truncate destPath (fresh download).
// resume=true : if destPath already exists with >0 bytes, continue it with an HTTP Range
//   request (CURLOPT_RESUME_FROM_LARGE) and append; the partial is kept on network failure
//   so a later call finishes it. The caller must SHA-verify the result (a server that ignores
//   Range, or a stale partial, yields a bad file -> discard + re-download fresh).
HttpResult downloadToFile(const std::string& url, const std::string& destPath,
                          const HttpProgress& progress = {}, bool resume = false);

// Download a file shared on Google Drive (any share/mirror URL). Resolves the file
// id, hits the confirm=t direct endpoint, and if Google still returns the HTML
// virus-scan interstitial, parses its form and retries with the confirm token.
// resume: forwarded to the direct download (the interstitial re-fetch always starts fresh).
HttpResult downloadGDrive(const std::string& mirrorUrl, const std::string& destPath,
                          const HttpProgress& progress = {}, bool resume = false);

}  // namespace uaro::net
