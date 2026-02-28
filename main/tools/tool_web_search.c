#include "tool_web_search.h"
#include "femto_config.h"
#include "proxy/http_proxy.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs.h"
#include "cJSON.h"

static const char *TAG = "web_search";

static char s_search_key[128] = {0};

#if CONFIG_SPIRAM
#define SEARCH_BUF_SIZE     (16 * 1024)
#else
#define SEARCH_BUF_SIZE     (8 * 1024)
#endif
#define SEARCH_RESULT_COUNT 5

/* ── Response accumulator ─────────────────────────────────────── */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int    recording;  /* 0 = skip until marker, 1 = accumulate */
} search_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    search_buf_t *sb = (search_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        size_t needed = sb->len + evt->data_len;
        if (needed < sb->cap) {
            memcpy(sb->data + sb->len, evt->data, evt->data_len);
            sb->len += evt->data_len;
            sb->data[sb->len] = '\0';
        }
    }
    return ESP_OK;
}

/* DDG event handler: skips HTML header until results container appears.
 * DDG pages have ~8KB of boilerplate before any results, so skipping it
 * lets us fit 5 results in an 8KB buffer on non-PSRAM devices. */
static const char DDG_MARKER[] = "class=\"results\"";

static esp_err_t ddg_event_handler(esp_http_client_event_t *evt)
{
    search_buf_t *sb = (search_buf_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;

    const char *chunk = (const char *)evt->data;
    size_t chunk_len = evt->data_len;

    if (sb->recording) {
        /* Already past the header — accumulate normally */
        size_t space = sb->cap - 1 - sb->len;
        size_t copy = (chunk_len < space) ? chunk_len : space;
        if (copy > 0) {
            memcpy(sb->data + sb->len, chunk, copy);
            sb->len += copy;
            sb->data[sb->len] = '\0';
        }
        return ESP_OK;
    }

    /* Still scanning for the marker.  We append to the buffer temporarily
     * so that strstr can find a marker that spans two chunks. */
    size_t space = sb->cap - 1 - sb->len;
    size_t copy = (chunk_len < space) ? chunk_len : space;
    if (copy > 0) {
        memcpy(sb->data + sb->len, chunk, copy);
        sb->len += copy;
        sb->data[sb->len] = '\0';
    }

    char *found = strstr(sb->data, DDG_MARKER);
    if (found) {
        /* Keep everything from the marker onward */
        size_t offset = (size_t)(found - sb->data);
        size_t keep = sb->len - offset;
        memmove(sb->data, found, keep);
        sb->len = keep;
        sb->data[sb->len] = '\0';
        sb->recording = 1;
    } else {
        /* Keep only the tail (marker-length bytes) in case the marker
         * straddles this chunk boundary. */
        size_t tail = sizeof(DDG_MARKER) - 1;
        if (sb->len > tail) {
            memmove(sb->data, sb->data + sb->len - tail, tail);
            sb->len = tail;
            sb->data[sb->len] = '\0';
        }
    }
    return ESP_OK;
}

/* ── Init ─────────────────────────────────────────────────────── */

esp_err_t tool_web_search_init(void)
{
    /* Start with build-time default */
    if (FEMTO_SECRET_SEARCH_KEY[0] != '\0') {
        strncpy(s_search_key, FEMTO_SECRET_SEARCH_KEY, sizeof(s_search_key) - 1);
    }

    /* NVS overrides take highest priority (set via CLI) */
    nvs_handle_t nvs;
    if (nvs_open(FEMTO_NVS_SEARCH, NVS_READONLY, &nvs) == ESP_OK) {
        char tmp[128] = {0};
        size_t len = sizeof(tmp);
        if (nvs_get_str(nvs, FEMTO_NVS_KEY_API_KEY, tmp, &len) == ESP_OK && tmp[0]) {
            strncpy(s_search_key, tmp, sizeof(s_search_key) - 1);
        }
        nvs_close(nvs);
    }

    if (s_search_key[0]) {
        ESP_LOGI(TAG, "Web search initialized (key configured)");
    } else {
        ESP_LOGI(TAG, "No search API key configured. DuckDuckGo fallback active.");
    }
    return ESP_OK;
}

/* ── URL-encode a query string ────────────────────────────────── */

static size_t url_encode(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t pos = 0;

    for (; *src && pos < dst_size - 3; src++) {
        unsigned char c = (unsigned char)*src;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[pos++] = c;
        } else if (c == ' ') {
            dst[pos++] = '+';
        } else {
            dst[pos++] = '%';
            dst[pos++] = hex[c >> 4];
            dst[pos++] = hex[c & 0x0F];
        }
    }
    dst[pos] = '\0';
    return pos;
}

/* ── URL-decode a percent-encoded string (in-place) ──────────── */

static void url_decode(char *str)
{
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            unsigned int ch;
            char hex[3] = { src[1], src[2], '\0' };
            if (sscanf(hex, "%x", &ch) == 1) {
                *dst++ = (char)ch;
                src += 3;
                continue;
            }
        }
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* ── Strip HTML tags and decode common entities ──────────────── */

static size_t strip_html(const char *src, char *dst, size_t dst_size)
{
    size_t pos = 0;
    int in_tag = 0;

    for (; *src && pos < dst_size - 1; src++) {
        if (*src == '<') { in_tag = 1; continue; }
        if (*src == '>') { in_tag = 0; continue; }
        if (in_tag) continue;

        /* Decode common HTML entities */
        if (*src == '&') {
            if (strncmp(src, "&amp;", 5) == 0)       { dst[pos++] = '&'; src += 4; continue; }
            if (strncmp(src, "&lt;", 4) == 0)        { dst[pos++] = '<'; src += 3; continue; }
            if (strncmp(src, "&gt;", 4) == 0)        { dst[pos++] = '>'; src += 3; continue; }
            if (strncmp(src, "&quot;", 6) == 0)      { dst[pos++] = '"'; src += 5; continue; }
            if (strncmp(src, "&#39;", 5) == 0)       { dst[pos++] = '\''; src += 4; continue; }
            if (strncmp(src, "&nbsp;", 6) == 0)      { dst[pos++] = ' '; src += 5; continue; }
            if (strncmp(src, "&#x27;", 6) == 0)      { dst[pos++] = '\''; src += 5; continue; }
        }
        dst[pos++] = *src;
    }
    dst[pos] = '\0';

    /* Collapse runs of whitespace */
    char *r = dst, *w = dst;
    int prev_sp = 0;
    while (*r) {
        if (*r == ' ' || *r == '\t' || *r == '\n' || *r == '\r') {
            if (!prev_sp) { *w++ = ' '; prev_sp = 1; }
        } else {
            *w++ = *r; prev_sp = 0;
        }
        r++;
    }
    *w = '\0';
    return (size_t)(w - dst);
}

/* ── Format results as readable text ──────────────────────────── */

static void format_results(cJSON *root, char *output, size_t output_size)
{
    cJSON *web = cJSON_GetObjectItem(root, "web");
    if (!web) {
        snprintf(output, output_size, "No web results found.");
        return;
    }

    cJSON *results = cJSON_GetObjectItem(web, "results");
    if (!results || !cJSON_IsArray(results) || cJSON_GetArraySize(results) == 0) {
        snprintf(output, output_size, "No web results found.");
        return;
    }

    size_t off = 0;
    int idx = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, results) {
        if (idx >= SEARCH_RESULT_COUNT) break;

        cJSON *title = cJSON_GetObjectItem(item, "title");
        cJSON *url = cJSON_GetObjectItem(item, "url");
        cJSON *desc = cJSON_GetObjectItem(item, "description");

        off += snprintf(output + off, output_size - off,
            "%d. %s\n   %s\n   %s\n\n",
            idx + 1,
            (title && cJSON_IsString(title)) ? title->valuestring : "(no title)",
            (url && cJSON_IsString(url)) ? url->valuestring : "",
            (desc && cJSON_IsString(desc)) ? desc->valuestring : "");

        if (off >= output_size - 1) break;
        idx++;
    }
}

/* ── Format DuckDuckGo HTML results ───────────────────────────── */

static void format_ddg_results(const char *html, char *output, size_t output_size)
{
    size_t off = 0;
    int idx = 0;
    const char *pos = html;

    /* Scratch buffers for title, URL, snippet */
    char title_buf[256];
    char url_buf[512];
    char snippet_buf[512];

    while (idx < SEARCH_RESULT_COUNT && pos && *pos) {
        /* Find next result anchor: class="result__a" */
        pos = strstr(pos, "class=\"result__a\"");
        if (!pos) break;

        /* ── Extract URL from href ── */
        /* Backtrack to find href= in this <a> tag */
        const char *a_start = pos;
        /* Search backwards for '<a' (limited scan) */
        int found_href = 0;
        for (int i = 0; i < 500 && a_start > html; i++, a_start--) {
            if (a_start[0] == '<' && (a_start[1] == 'a' || a_start[1] == 'A')) break;
        }
        const char *href = strstr(a_start, "href=\"");
        if (href && href < pos + 20) {
            href += 6;
            const char *href_end = strchr(href, '"');
            if (href_end) {
                size_t hlen = (size_t)(href_end - href);
                if (hlen >= sizeof(url_buf)) hlen = sizeof(url_buf) - 1;
                memcpy(url_buf, href, hlen);
                url_buf[hlen] = '\0';

                /* DDG wraps URLs as //duckduckgo.com/l/?uddg=<encoded>&... */
                char *uddg = strstr(url_buf, "uddg=");
                if (uddg) {
                    uddg += 5; /* skip "uddg=" */
                    char *amp = strchr(uddg, '&');
                    if (amp) *amp = '\0';
                    url_decode(uddg);
                    memmove(url_buf, uddg, strlen(uddg) + 1);
                }
                found_href = 1;
            }
        }
        if (!found_href) { url_buf[0] = '\0'; }

        /* ── Extract title text from <a ...>TITLE</a> ── */
        const char *tag_end = strchr(pos, '>');
        if (!tag_end) break;
        tag_end++;
        const char *a_close = strstr(tag_end, "</a>");
        if (a_close) {
            size_t raw_len = (size_t)(a_close - tag_end);
            /* Use snippet_buf as temp for raw html title */
            if (raw_len >= sizeof(snippet_buf)) raw_len = sizeof(snippet_buf) - 1;
            memcpy(snippet_buf, tag_end, raw_len);
            snippet_buf[raw_len] = '\0';
            strip_html(snippet_buf, title_buf, sizeof(title_buf));
            pos = a_close + 4;
        } else {
            title_buf[0] = '\0';
        }

        /* ── Extract snippet from class="result__snippet" ── */
        snippet_buf[0] = '\0';
        const char *snip = strstr(pos, "class=\"result__snippet\"");
        /* Only use if it appears before the next result */
        const char *next_result = strstr(pos, "class=\"result__a\"");
        if (snip && (!next_result || snip < next_result)) {
            const char *snip_gt = strchr(snip, '>');
            if (snip_gt) {
                snip_gt++;
                /* Find closing tag (</a> or </span> or </td>) */
                const char *snip_end = strstr(snip_gt, "</a>");
                if (!snip_end || (next_result && snip_end > next_result))
                    snip_end = strstr(snip_gt, "</span>");
                if (!snip_end || (next_result && snip_end > next_result))
                    snip_end = strstr(snip_gt, "</td>");
                if (snip_end) {
                    size_t raw_len = (size_t)(snip_end - snip_gt);
                    char raw_buf[512];
                    if (raw_len >= sizeof(raw_buf)) raw_len = sizeof(raw_buf) - 1;
                    memcpy(raw_buf, snip_gt, raw_len);
                    raw_buf[raw_len] = '\0';
                    strip_html(raw_buf, snippet_buf, sizeof(snippet_buf));
                    pos = snip_end;
                }
            }
        }

        /* Trim leading/trailing whitespace from title and snippet */
        char *t = title_buf;
        while (*t == ' ') t++;
        char *s = snippet_buf;
        while (*s == ' ') s++;

        off += snprintf(output + off, output_size - off,
            "%d. %s\n   %s\n   %s\n\n",
            idx + 1,
            t[0] ? t : "(no title)",
            url_buf,
            s);

        if (off >= output_size - 1) break;
        idx++;
    }

    if (idx == 0) {
        snprintf(output, output_size, "No web results found.");
    }
}

/* ── Direct HTTPS request ─────────────────────────────────────── */

static esp_err_t search_direct(const char *url, search_buf_t *sb)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = sb,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "X-Subscription-Token", s_search_key);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) return err;
    if (status != 200) {
        ESP_LOGE(TAG, "Search API returned %d", status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ── DuckDuckGo direct HTTPS request ──────────────────────────── */

static esp_err_t ddg_search_direct(const char *encoded_query, search_buf_t *sb)
{
    sb->recording = 0; /* skip header until results marker */

    esp_http_client_config_t config = {
        .url = "https://html.duckduckgo.com/html/",
        .method = HTTP_METHOD_POST,
        .event_handler = ddg_event_handler,
        .user_data = sb,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Content-Type",
                               "application/x-www-form-urlencoded");
    esp_http_client_set_header(client, "User-Agent",
                               "Mozilla/5.0 (compatible; FemtoClaw/1.0)");

    char post_data[300];
    snprintf(post_data, sizeof(post_data), "q=%s", encoded_query);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) return err;
    if (status != 200) {
        ESP_LOGE(TAG, "DuckDuckGo returned %d", status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ── Proxy HTTPS request ──────────────────────────────────────── */

static esp_err_t search_via_proxy(const char *path, search_buf_t *sb)
{
    proxy_conn_t *conn = proxy_conn_open("api.search.brave.com", 443, 15000);
    if (!conn) return ESP_ERR_HTTP_CONNECT;

    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "GET %s HTTP/1.1\r\n"
        "Host: api.search.brave.com\r\n"
        "Accept: application/json\r\n"
        "X-Subscription-Token: %s\r\n"
        "Connection: close\r\n\r\n",
        path, s_search_key);

    if (proxy_conn_write(conn, header, hlen) < 0) {
        proxy_conn_close(conn);
        return ESP_ERR_HTTP_WRITE_DATA;
    }

    /* Read full response */
    char tmp[4096];
    size_t total = 0;
    while (1) {
        int n = proxy_conn_read(conn, tmp, sizeof(tmp), 15000);
        if (n <= 0) break;
        size_t copy = (total + n < sb->cap - 1) ? (size_t)n : sb->cap - 1 - total;
        if (copy > 0) {
            memcpy(sb->data + total, tmp, copy);
            total += copy;
        }
    }
    sb->data[total] = '\0';
    sb->len = total;
    proxy_conn_close(conn);

    /* Check status */
    int status = 0;
    if (total > 5 && strncmp(sb->data, "HTTP/", 5) == 0) {
        const char *sp = strchr(sb->data, ' ');
        if (sp) status = atoi(sp + 1);
    }

    /* Strip headers */
    char *body = strstr(sb->data, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t blen = total - (body - sb->data);
        memmove(sb->data, body, blen);
        sb->len = blen;
        sb->data[sb->len] = '\0';
    }

    if (status != 200) {
        ESP_LOGE(TAG, "Search API returned %d via proxy", status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ── Execute ──────────────────────────────────────────────────── */

esp_err_t tool_web_search_execute(const char *input_json, char *output, size_t output_size)
{
    /* Parse input to get query */
    cJSON *input = cJSON_Parse(input_json);
    if (!input) {
        snprintf(output, output_size, "Error: Invalid input JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *query = cJSON_GetObjectItem(input, "query");
    if (!query || !cJSON_IsString(query) || query->valuestring[0] == '\0') {
        cJSON_Delete(input);
        snprintf(output, output_size, "Error: Missing 'query' field");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Searching: %s", query->valuestring);

    char encoded_query[256];
    url_encode(query->valuestring, encoded_query, sizeof(encoded_query));
    cJSON_Delete(input);

    /* Allocate response buffer (PSRAM when available, internal RAM otherwise) */
    search_buf_t sb = {0};
    sb.data = femto_alloc(SEARCH_BUF_SIZE);
    if (!sb.data) {
        snprintf(output, output_size, "Error: Out of memory");
        return ESP_ERR_NO_MEM;
    }
    sb.cap = SEARCH_BUF_SIZE;

    esp_err_t err;

    if (s_search_key[0] == '\0') {
        /* ── DuckDuckGo fallback (no API key needed) ── */
        ESP_LOGI(TAG, "Using DuckDuckGo fallback");
        err = ddg_search_direct(encoded_query, &sb);

        if (err != ESP_OK) {
            free(sb.data);
            snprintf(output, output_size, "Error: DuckDuckGo search request failed");
            return err;
        }

        format_ddg_results(sb.data, output, output_size);
        free(sb.data);
    } else {
        /* ── Brave Search (API key configured) ── */
        char path[384];
        snprintf(path, sizeof(path),
                 "/res/v1/web/search?q=%s&count=%d", encoded_query, SEARCH_RESULT_COUNT);

        if (http_proxy_is_enabled()) {
            err = search_via_proxy(path, &sb);
        } else {
            char url[512];
            snprintf(url, sizeof(url), "https://api.search.brave.com%s", path);
            err = search_direct(url, &sb);
        }

        if (err != ESP_OK) {
            free(sb.data);
            snprintf(output, output_size, "Error: Search request failed");
            return err;
        }

        /* Parse and format Brave JSON results */
        cJSON *root = cJSON_Parse(sb.data);
        free(sb.data);

        if (!root) {
            snprintf(output, output_size, "Error: Failed to parse search results");
            return ESP_FAIL;
        }

        format_results(root, output, output_size);
        cJSON_Delete(root);
    }

    ESP_LOGI(TAG, "Search complete, %d bytes result", (int)strlen(output));
    return ESP_OK;
}

esp_err_t tool_web_search_set_key(const char *api_key)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(FEMTO_NVS_SEARCH, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, FEMTO_NVS_KEY_API_KEY, api_key));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    strncpy(s_search_key, api_key, sizeof(s_search_key) - 1);
    ESP_LOGI(TAG, "Search API key saved");
    return ESP_OK;
}
