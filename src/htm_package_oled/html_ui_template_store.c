#include "html_ui_template_store.h"

#include <errno.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(html_ui_store, LOG_LEVEL_INF);

static bool g_html_template_seed_attempted[5];

static int html_ui_template_seed_slot_for_path(const char *template_path)
{
    if (template_path == NULL) {
        return -1;
    }

    if (strcmp(template_path, "/internal/html_templates/scan_home.html") == 0) {
        return 0;
    }
    if (strcmp(template_path, "/internal/html_templates/dummy_language.html") == 0) {
        return 1;
    }
    if (strcmp(template_path, "/internal/html_templates/dummy_mode.html") == 0) {
        return 2;
    }
    if (strcmp(template_path, "/internal/html_templates/dummy_scan.html") == 0) {
        return 3;
    }
    if (strcmp(template_path, "/internal/html_templates/dummy_courier.html") == 0) {
        return 4;
    }

    return -1;
}

/* Log internal LittleFS capacity to diagnose seed failures (for example ENOSPC). */
static void html_ui_log_internal_lfs_usage(const char *reason, const char *template_path)
{
    struct fs_statvfs sbuf;
    unsigned long long block_size;
    unsigned long long total_bytes;
    unsigned long long free_bytes;
    unsigned long long used_bytes;
    int rc;

    rc = fs_statvfs("/internal", &sbuf);
    if (rc < 0) {
        LOG_WRN(
                     "HTML_UI: failed to read LFS usage reason=%s path=%s rc=%d",
                     reason ? reason : "-",
                     template_path ? template_path : "-",
                     rc);
        return;
    }

    block_size = (unsigned long long)(sbuf.f_frsize != 0U ? sbuf.f_frsize : sbuf.f_bsize);
    total_bytes = block_size * (unsigned long long)sbuf.f_blocks;
    free_bytes = block_size * (unsigned long long)sbuf.f_bfree;
    used_bytes = (total_bytes >= free_bytes) ? (total_bytes - free_bytes) : 0U;

    LOG_WRN(
                 "HTML_UI: LFS usage reason=%s path=%s total=%llu used=%llu free=%llu block=%llu blocks=%lu bfree=%lu",
                 reason ? reason : "-",
                 template_path ? template_path : "-",
                 total_bytes,
                 used_bytes,
                 free_bytes,
                 block_size,
                 (unsigned long)sbuf.f_blocks,
                 (unsigned long)sbuf.f_bfree);
}

/* Ensure a single requested template file exists and has content. */
static void html_ui_ensure_template_file_seeded(const char *template_path, const char *seed_html)
{
    struct fs_dirent entry;
    struct fs_dirent template_entry;
    struct fs_file_t file;
    size_t html_len;
    ssize_t written;
    int rc;
    int slot;

    if (template_path == NULL || *template_path == 0 || seed_html == NULL || *seed_html == 0) {
        return;
    }

    slot = html_ui_template_seed_slot_for_path(template_path);
    if (slot >= 0 && g_html_template_seed_attempted[slot]) {
        return;
    }

    memset(&entry, 0, sizeof(entry));
    rc = fs_stat("/internal/html_templates", &entry);
    if (rc == 0) {
        if (entry.type != FS_DIR_ENTRY_DIR) {
            LOG_WRN(
                         "HTML_UI: template path exists but is not a directory path=%s",
                         "/internal/html_templates");
            return;
        }
    } else {
        rc = fs_mkdir("/internal/html_templates");
        if (rc < 0) {
            LOG_WRN(
                         "HTML_UI: failed to create template directory path=%s rc=%d",
                         "/internal/html_templates",
                         rc);
            html_ui_log_internal_lfs_usage("mkdir_failed", template_path);
            return;
        }
    }

    memset(&template_entry, 0, sizeof(template_entry));
    rc = fs_stat(template_path, &template_entry);
    if (rc == 0) {
        if (template_entry.type != FS_DIR_ENTRY_FILE) {
            LOG_WRN(
                         "HTML_UI: template path exists but is not a file path=%s",
                         template_path);
            return;
        }

        if (template_entry.size > 0) {
            LOG_DBG(
                         "HTML_UI: template present path=%s size=%zd",
                         template_path,
                         template_entry.size);
            if (slot >= 0) {
                g_html_template_seed_attempted[slot] = true;
            }
            return;
        }

        LOG_WRN(
                     "HTML_UI: template is empty, reseeding path=%s",
                     template_path);
    }

    html_len = strlen(seed_html);
    fs_file_t_init(&file);
    rc = fs_open(&file, template_path, FS_O_CREATE | FS_O_WRITE);
    if (rc < 0) {
        LOG_WRN(
                     "HTML_UI: failed to create seed template path=%s rc=%d",
                     template_path,
                     rc);
        html_ui_log_internal_lfs_usage("open_failed", template_path);
        if (slot >= 0) {
            g_html_template_seed_attempted[slot] = true;
        }
        return;
    }

    written = fs_write(&file, seed_html, html_len);
    (void)fs_sync(&file);
    rc = fs_close(&file);
    if (written < 0) {
        if ((int)written == -ENOSPC) {
            LOG_ERR(
                         "HTML_UI: no LFS space to seed template path=%s rc=%d",
                         template_path,
                         (int)written);
        } else {
            LOG_WRN(
                         "HTML_UI: failed to write seed template path=%s rc=%d",
                         template_path,
                         (int)written);
        }
        html_ui_log_internal_lfs_usage("write_failed", template_path);
        if (slot >= 0) {
            g_html_template_seed_attempted[slot] = true;
        }
        return;
    }
    if ((size_t)written != html_len) {
        LOG_WRN(
                     "HTML_UI: partial seed template write path=%s written=%d expected=%u",
                     template_path,
                     (int)written,
                     (unsigned)html_len);
        html_ui_log_internal_lfs_usage("write_partial", template_path);
        if (slot >= 0) {
            g_html_template_seed_attempted[slot] = true;
        }
        return;
    }
    if (rc < 0) {
        LOG_WRN(
                     "HTML_UI: failed to close seed template path=%s rc=%d",
                     template_path,
                     rc);
        html_ui_log_internal_lfs_usage("close_failed", template_path);
        if (slot >= 0) {
            g_html_template_seed_attempted[slot] = true;
        }
        return;
    }

    LOG_DBG( "HTML_UI: seeded template path=%s", template_path);
    if (slot >= 0) {
        g_html_template_seed_attempted[slot] = true;
    }

    memset(&template_entry, 0, sizeof(template_entry));
    rc = fs_stat(template_path, &template_entry);
    if (rc == 0 && template_entry.type == FS_DIR_ENTRY_FILE) {
        LOG_DBG(
                     "HTML_UI: template post-seed path=%s size=%zd",
                     template_path,
                     template_entry.size);
    }
}

int html_ui_load_template_from_lfs(const char *template_path, char *buffer, size_t buffer_size)
{
    struct fs_dirent entry;
    struct fs_file_t file;
    ssize_t read_len;
    int rc;

    if (template_path == NULL || buffer == NULL || buffer_size < 2U) {
        return -EINVAL;
    }

    buffer[0] = 0;
    memset(&entry, 0, sizeof(entry));

    rc = fs_stat(template_path, &entry);
    if (rc < 0) {
        LOG_WRN( "HTML_UI: template stat failed path=%s rc=%d", template_path, rc);
        return rc;
    }

    if (entry.type != FS_DIR_ENTRY_FILE) {
        LOG_WRN( "HTML_UI: template is not a file path=%s", template_path);
        return -ENOENT;
    }

    if (entry.size <= 0) {
        LOG_WRN( "HTML_UI: template is empty path=%s", template_path);
        return -ENODATA;
    }

    LOG_DBG(
                 "HTML_UI: template load path=%s size=%zd buffer=%u",
                 template_path,
                 entry.size,
                 (unsigned)buffer_size);

    if ((size_t)entry.size >= buffer_size) {
        LOG_ERR(
                     "HTML_UI: template too large path=%s size=%zd buffer=%u",
                     template_path,
                     entry.size,
                     (unsigned)buffer_size);
        return -ENOSPC;
    }

    fs_file_t_init(&file);
    rc = fs_open(&file, template_path, FS_O_READ);
    if (rc < 0) {
        LOG_WRN( "HTML_UI: template open failed path=%s rc=%d", template_path, rc);
        return rc;
    }

    read_len = fs_read(&file, buffer, buffer_size - 1U);
    rc = fs_close(&file);
    if (read_len < 0) {
        LOG_WRN( "HTML_UI: template read failed path=%s rc=%d", template_path, (int)read_len);
        return (int)read_len;
    }
    if (rc < 0) {
        LOG_WRN( "HTML_UI: template close failed path=%s rc=%d", template_path, rc);
        return rc;
    }
    if (read_len == 0) {
        LOG_WRN( "HTML_UI: template read returned no data path=%s", template_path);
        return -ENODATA;
    }

    buffer[read_len] = 0;
    return (int)read_len;
}

bool html_ui_render_template_from_lfs_or_fallback(html_ui_context_t *ctx,
                                                  const char *template_path,
                                                  const char *fallback_html,
                                                  char *buffer,
                                                  size_t buffer_size)
{
    int rc;

    if (ctx == NULL || fallback_html == NULL || *fallback_html == 0) {
        return false;
    }

    html_ui_ensure_template_file_seeded(template_path, fallback_html);

    if (template_path != NULL && *template_path != 0 && buffer != NULL && buffer_size > 1U) {
        rc = html_ui_load_template_from_lfs(template_path, buffer, buffer_size);
        if (rc > 0) {
            LOG_DBG( "HTML_UI: rendering LFS template path=%s bytes=%d", template_path, rc);
            html_ui_render(ctx, buffer);
            return true;
        }

        LOG_WRN(
                     "HTML_UI: falling back to embedded template path=%s rc=%d",
                     template_path,
                     rc);
    }

    html_ui_render(ctx, fallback_html);
    return false;
}