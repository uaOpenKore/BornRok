#include "net/Http.hpp"

#include <curl/curl.h>

#include <cstdio>
#include <filesystem>
#include <mutex>

#include "core/Log.hpp"
#include "patcher/GDriveUrl.hpp"

namespace uaro::net {
namespace {

std::once_flag g_initOnce;

usize writeToString(char* ptr, usize size, usize nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

usize writeToFile(char* ptr, usize size, usize nmemb, void* userdata) {
    auto* f = static_cast<std::FILE*>(userdata);
    return std::fwrite(ptr, size, nmemb, f) * size;
}

struct ProgCtx {
    const HttpProgress* cb;
};

int progressTramp(void* p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<ProgCtx*>(p);
    if (ctx && ctx->cb && *ctx->cb)
        if (!(*ctx->cb)(static_cast<u64>(dlnow), static_cast<u64>(dltotal)))
            return 1;  // non-zero aborts the transfer
    return 0;
}

// Apply the options shared by every request.
void commonSetup(CURL* h) {
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "BornRok-patcher/1.0");
    curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");  // allow gzip/deflate
    // Enable the in-memory cookie engine so the Google-Drive confirm cookie carries
    // across the two requests on the same handle.
    curl_easy_setopt(h, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0L);
}

HttpResult finishStatus(CURL* h, CURLcode rc) {
    HttpResult r;
    long code = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
    r.status = code;
    if (rc != CURLE_OK) {
        r.error = curl_easy_strerror(rc);
        return r;
    }
    r.ok = (code >= 200 && code < 300);
    if (!r.ok) r.error = "HTTP " + std::to_string(code);
    return r;
}

// Extract value of a hidden <input name="KEY" value="VALUE"> from a GDrive form page.
std::string formField(const std::string& html, const std::string& key) {
    const std::string needle = "name=\"" + key + "\"";
    usize n = html.find(needle);
    if (n == std::string::npos) return "";
    const usize v = html.find("value=\"", n);
    if (v == std::string::npos) return "";
    const usize start = v + 7;
    const usize end = html.find('"', start);
    if (end == std::string::npos) return "";
    return html.substr(start, end - start);
}

}  // namespace

void httpGlobalInit() {
    std::call_once(g_initOnce, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

HttpResult fetchToString(const std::string& url, std::string& out, int timeoutSec) {
    httpGlobalInit();
    CURL* h = curl_easy_init();
    if (!h) return {false, 0, "curl_easy_init failed"};
    commonSetup(h);
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_TIMEOUT, static_cast<long>(timeoutSec));
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &out);
    const CURLcode rc = curl_easy_perform(h);
    HttpResult r = finishStatus(h, rc);
    curl_easy_cleanup(h);
    return r;
}

HttpResult downloadToFile(const std::string& url, const std::string& destPath,
                          const HttpProgress& progress, bool resume) {
    httpGlobalInit();
    // Resume: if the partial exists, reopen for append and tell curl to ask for the tail via
    // a Range request from the current size. Otherwise start fresh (truncate). Use filesystem
    // file_size (64-bit) -- ftell is `long` (32-bit on Windows) and would overflow on a multi-GB
    // GRF (#116), yielding a bogus resume offset.
    long long resumeFrom = 0;
    if (resume) {
        std::error_code ec;
        const auto sz = std::filesystem::file_size(destPath, ec);
        if (!ec) resumeFrom = static_cast<long long>(sz);
    }
    std::FILE* f = std::fopen(destPath.c_str(), resumeFrom > 0 ? "ab" : "wb");
    if (!f) return {false, 0, "cannot open " + destPath};
    CURL* h = curl_easy_init();
    if (!h) { std::fclose(f); return {false, 0, "curl_easy_init failed"}; }
    commonSetup(h);
    ProgCtx ctx{&progress};
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, progressTramp);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    if (resumeFrom > 0)
        curl_easy_setopt(h, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(resumeFrom));
    const CURLcode rc = curl_easy_perform(h);
    HttpResult r = finishStatus(h, rc);
    curl_easy_cleanup(h);
    std::fclose(f);
    // Keep the partial when resuming (a later call finishes it); only wipe on a fresh failure.
    if (!r.ok && !resume) std::remove(destPath.c_str());
    return r;
}

HttpResult downloadGDrive(const std::string& mirrorUrl, const std::string& destPath,
                          const HttpProgress& progress, bool resume) {
    const std::string id = gdriveFileId(mirrorUrl);
    if (id.empty()) {
        // Not a recognisable Drive URL -> try it as a plain direct link.
        return downloadToFile(mirrorUrl, destPath, progress, resume);
    }
    // First attempt: the confirm=t direct endpoint (works for most files). Resume if asked.
    HttpResult r = downloadToFile(gdriveDirectUrl(id), destPath, progress, resume);
    if (!r.ok) return r;

    // If Google served the HTML virus-scan interstitial instead of the file, the
    // saved "file" is a small HTML page. Detect, parse the form, and retry.
    std::FILE* f = std::fopen(destPath.c_str(), "rb");
    if (!f) return r;
    char head[512] = {0};
    const usize n = std::fread(head, 1, sizeof(head) - 1, f);
    std::fclose(f);
    const std::string sniff(head, n);
    const bool looksHtml = sniff.find("<!DOCTYPE html") != std::string::npos ||
                           sniff.find("<html") != std::string::npos;
    if (!looksHtml) return r;  // it was the real file

    // Re-fetch the interstitial page into memory to read the confirm token.
    std::string page;
    fetchToString(gdriveDirectUrl(id), page);
    const std::string confirm = formField(page, "confirm");
    const std::string uuid = formField(page, "uuid");
    std::string url = "https://drive.usercontent.google.com/download?id=" + id + "&export=download";
    if (!confirm.empty()) url += "&confirm=" + confirm;
    if (!uuid.empty()) url += "&uuid=" + uuid;
    log::info("patcher: GDrive interstitial for {}, retrying with confirm token", id);
    return downloadToFile(url, destPath, progress);
}

}  // namespace uaro::net
