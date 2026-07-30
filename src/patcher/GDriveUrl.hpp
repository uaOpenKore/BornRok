#pragma once
// Pure helpers for Google-Drive download URLs (no network) so they unit-test
// offline. The patcher's HTTP layer uses these to turn a share/mirror link from
// downloads.list into a direct-download URL.
#include <string>

namespace uaro {

// Extract the Drive file id from any of the common URL shapes:
//   .../uc?export=download&id=<id>
//   .../file/d/<id>/view  |  .../d/<id>/...
//   .../open?id=<id>  |  drive.usercontent.google.com/download?id=<id>
// Returns "" if no id is found.
std::string gdriveFileId(const std::string& url);

// Direct-download endpoint that bypasses the virus-scan interstitial for large
// files (the confirm=t form). Matches the URL pattern proven on the 1.8 GB
// package. Empty id -> "".
std::string gdriveDirectUrl(const std::string& fileId);

}  // namespace uaro
