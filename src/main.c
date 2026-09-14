#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <zlib.h>
#include <zip.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#endif

// ========================
// Глобальные структуры и переменные
// ========================
typedef struct {
    GtkWidget *window;
    GtkWidget *main_vbox;
    GtkWidget *top_hbox;
    GtkWidget *second_hbox;
    GtkWidget *path_hbox;
    GtkWidget *action_hbox;

    GtkWidget *radio_md_to_pdf;
    GtkWidget *radio_pdf_to_md;
    GtkWidget *check_attach_sources;

    GtkWidget *select_folder_btn;
    GtkWidget *select_pdf_btn;
    GtkWidget *path_entry;

    GtkWidget *generate_btn;
    GtkWidget *import_btn;

    GtkWidget *lang_ru_btn;
    GtkWidget *lang_en_btn;
    GtkWidget *lang_de_btn;
    GtkWidget *lang_fr_btn;
    GtkWidget *about_btn;

    // Флаги языков
    GdkPixbuf *flag_ru;
    GdkPixbuf *flag_en;
    GdkPixbuf *flag_de;
    GdkPixbuf *flag_fr;
    GdkPixbuf *flag_gray_ru;
    GdkPixbuf *flag_gray_en;
    GdkPixbuf *flag_gray_de;
    GdkPixbuf *flag_gray_fr;

    // Текущее состояние
    gchar *folder_path;        // путь к папке для md_to_pdf
    gchar *selected_pdf;       // путь к PDF для pdf_to_md
    gboolean attach_sources;   // флаг встраивания исходников
    gchar *current_lang;       // текущий язык (ru, en, de, fr)

    // Словарь локализации
    GHashTable *strings;       // ключ -> значение для текущего языка
} App;

// Глобальная переменная приложения (для обработчиков)
App *app;
// Прототипы функций
static void about_cb(GtkWidget *widget, gpointer data);
static void generate_pdf_cb(GtkWidget *widget, gpointer data);
static void import_pdf_cb(GtkWidget *widget, gpointer data);
static void open_url(GtkWidget *widget, const gchar *url);
static gchar *extract_text_from_pdf(const gchar *pdf_path);
static gboolean extract_embedded_sources(const gchar *pdf_path, guchar **out_data, gsize *out_len);
static gboolean unpack_sources(const guchar *data, gsize len, const gchar *output_folder);
static gchar *get_exe_dir(void);
static void on_attach_sources_toggled(GtkWidget *widget, gpointer data);
static gboolean pack_sources(const gchar *folder, const gchar *md_file, guchar **out_data, gsize *out_len);

// ========================
// Загрузка локализации
// ========================
static void open_url(GtkWidget *widget, const gchar *url) {
    (void)widget;
    gtk_show_uri_on_window(GTK_WINDOW(app->window),
                           url,
                           GDK_CURRENT_TIME,
                           NULL);
}
static void load_locale(const gchar *lang) {
    if (app->strings) {
        g_hash_table_destroy(app->strings);
        app->strings = NULL;
    }

    app->strings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    //gchar *lang_file = g_strconcat(lang, ".txt", NULL);
    //gchar *locale_path = g_build_filename("assets", "locales", lang_file, NULL);
    //g_free(lang_file);
    gchar *lang_file = g_strconcat(lang, ".txt", NULL);
    gchar *exe_dir = get_exe_dir();
    gchar *locale_path = g_build_filename(exe_dir, "assets", "locales", lang_file, NULL);
    g_free(exe_dir);
    g_free(lang_file);

    gchar *content = NULL;
    gsize content_len = 0;
    if (!g_file_get_contents(locale_path, &content, &content_len, NULL)) {
        // Файла локализации нет, будет использоваться RU по умолчанию
        g_free(locale_path);
        return;
    }
    // Разбиваем содержимое на строки
    gchar **lines = g_strsplit(content, "\n", -1);
    g_free(content);

    for (int i = 0; lines[i] != NULL; i++) {
        gchar *line = lines[i];
        // убираем \r и пробелы в конце
        g_strchomp(line);
        if (line[0] == '#' || line[0] == '\0') continue;
        gchar **parts = g_strsplit(line, "=", 2);
        if (parts[0] && parts[1]) {
            // Заменяем литеральные \n на реальные переносы строк
            gchar *value = g_strcompress(parts[1]);
            g_hash_table_insert(app->strings, g_strdup(parts[0]), value);
        }
        g_strfreev(parts);
    }
    g_strfreev(lines);
    g_free(locale_path);
}
static const gchar *tr(const gchar *key) {
    if (!app->strings) return key;
    const gchar *val = g_hash_table_lookup(app->strings, key);
    return val ? val : key;
}
//Конвертация переменных из файлов локализации в C формат
static gchar *prepare_template(const gchar *template) {
    GString *result = g_string_new(template);
    // Заменяем {count} на %d
    gchar *pos;
    while ((pos = g_strstr_len(result->str, -1, "{count}")) != NULL) {
        gint index = pos - result->str;
        g_string_erase(result, index, strlen("{count}"));
        g_string_insert(result, index, "%d");
    }
    // Заменяем {folder} на %s
    while ((pos = g_strstr_len(result->str, -1, "{folder}")) != NULL) {
        gint index = pos - result->str;
        g_string_erase(result, index, strlen("{folder}"));
        g_string_insert(result, index, "%s");
    }
    return g_string_free(result, FALSE);
}
// ========================
// Обновление текстов интерфейса
// ========================
static void update_ui_texts() {
    gtk_button_set_label(GTK_BUTTON(app->select_folder_btn), tr("select_folder"));
    gtk_button_set_label(GTK_BUTTON(app->select_pdf_btn), tr("select_pdf"));
    gtk_button_set_label(GTK_BUTTON(app->check_attach_sources), tr("attach_sources"));
    gtk_button_set_label(GTK_BUTTON(app->generate_btn), tr("generate_pdf"));
    gtk_button_set_label(GTK_BUTTON(app->import_btn), tr("import_pdf"));
    gtk_button_set_label(GTK_BUTTON(app->radio_md_to_pdf), tr("mode_md_to_pdf"));
    gtk_button_set_label(GTK_BUTTON(app->radio_pdf_to_md), tr("mode_pdf_to_md"));
    gtk_window_set_title(GTK_WINDOW(app->window), tr("title"));
}

static void update_language_buttons() {
    GdkPixbuf *ru = (g_strcmp0(app->current_lang, "ru") == 0) ? app->flag_ru : app->flag_gray_ru;
    GdkPixbuf *en = (g_strcmp0(app->current_lang, "en") == 0) ? app->flag_en : app->flag_gray_en;
    GdkPixbuf *de = (g_strcmp0(app->current_lang, "de") == 0) ? app->flag_de : app->flag_gray_de;
    GdkPixbuf *fr = (g_strcmp0(app->current_lang, "fr") == 0) ? app->flag_fr : app->flag_gray_fr;

    gtk_button_set_image(GTK_BUTTON(app->lang_ru_btn), gtk_image_new_from_pixbuf(ru));
    gtk_button_set_image(GTK_BUTTON(app->lang_en_btn), gtk_image_new_from_pixbuf(en));
    gtk_button_set_image(GTK_BUTTON(app->lang_de_btn), gtk_image_new_from_pixbuf(de));
    gtk_button_set_image(GTK_BUTTON(app->lang_fr_btn), gtk_image_new_from_pixbuf(fr));
}

// Файл настроек лежит рядом с exe (в portable-папке)
static gchar *get_settings_file(void) {
    gchar *exe_dir = get_exe_dir();
    gchar *file = g_build_filename(exe_dir, "settings.ini", NULL);
    g_free(exe_dir);
    return file;
}

// Сохраняем выбранный язык
static void save_language(const gchar *lang) {
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_string(kf, "app", "language", lang);
    gchar *data = g_key_file_to_data(kf, NULL, NULL);
    gchar *file = get_settings_file();
    g_file_set_contents(file, data, -1, NULL);   // если доступа для создания папки нет, то просто не сохранится
    g_free(file);
    g_free(data);
    g_key_file_free(kf);
}

// Читаем сохранённый язык; при любой проблеме по умолчанию будет "ru"
static gchar *load_language(void) {
    gchar *file = get_settings_file();
    GKeyFile *kf = g_key_file_new();
    gchar *lang = NULL;

    if (g_key_file_load_from_file(kf, file, G_KEY_FILE_NONE, NULL)) {
        gchar *tmp = g_key_file_get_string(kf, "app", "language", NULL);
        if (tmp && (g_strcmp0(tmp, "ru") == 0 || g_strcmp0(tmp, "en") == 0 ||
                    g_strcmp0(tmp, "de") == 0 || g_strcmp0(tmp, "fr") == 0)) {
            lang = tmp;   // валидный язык
        } else {
            g_free(tmp);  // неизвестный/повреждённый — игнорируем
        }
    }

    g_free(file);
    g_key_file_free(kf);

    if (!lang)
        lang = g_strdup("ru");   // по умолчанию
    return lang;
}

static void set_language(const gchar *lang) {
    g_free(app->current_lang);
    app->current_lang = g_strdup(lang);
    load_locale(lang);
    update_ui_texts();
    update_language_buttons();
    save_language(lang);   // запоминаем выбор
}

static void set_language_cb(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    set_language((const gchar *)user_data);
}

// ========================
// Переключение режима (md_to_pdf / pdf_to_md)
// ========================

static void on_mode_change(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    gboolean is_md = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->radio_md_to_pdf));

    // Скрываем все переключаемые элементы
    gtk_widget_hide(app->select_folder_btn);
    gtk_widget_hide(app->select_pdf_btn);
    gtk_widget_hide(app->check_attach_sources);
    gtk_widget_hide(app->generate_btn);
    gtk_widget_hide(app->import_btn);

    if (is_md) {
        // Режим "Текст → PDF"
        gtk_widget_show(app->select_folder_btn);
        gtk_widget_show(app->check_attach_sources);
        gtk_widget_show(app->generate_btn);
    } else {
        // Режим "PDF → Текст"
        gtk_widget_show(app->select_pdf_btn);
        gtk_widget_show(app->import_btn);
    }

    // Очищаем поле пути
    gtk_entry_set_text(GTK_ENTRY(app->path_entry), "");
    // Сбрасываем состояние кнопок
    gtk_widget_set_sensitive(app->generate_btn, FALSE);
    gtk_widget_set_sensitive(app->import_btn, FALSE);

    // Очищаем выбранные пути
    g_free(app->folder_path);
    app->folder_path = NULL;
    g_free(app->selected_pdf);
    app->selected_pdf = NULL;

    // Очищаем цвет кнопок при выборе файла или папки
    gtk_widget_set_sensitive(app->generate_btn, FALSE);
    gtk_widget_set_sensitive(app->import_btn, FALSE);
    gtk_style_context_remove_class(gtk_widget_get_style_context(app->generate_btn), "green");
    gtk_style_context_remove_class(gtk_widget_get_style_context(app->import_btn), "green");
    gtk_widget_queue_draw(app->generate_btn);
    gtk_widget_queue_draw(app->import_btn);
}
// ========================
// Диалоги выбора папки/файла
// ========================

static void select_folder_cb(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;

    gchar *title = g_strdup_printf("PDFuse — %s", tr("select_folder"));
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        title,
        GTK_WINDOW(app->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "gtk-cancel", GTK_RESPONSE_CANCEL,
        "gtk-open", GTK_RESPONSE_ACCEPT,
        NULL);
    g_free(title);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 400);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
    gtk_window_resize(GTK_WINDOW(dialog), 600, 400);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        app->folder_path = g_strdup(folder);
        g_free(folder);
        gtk_entry_set_text(GTK_ENTRY(app->path_entry), app->folder_path);
        gtk_widget_set_sensitive(app->generate_btn, TRUE);
        gtk_style_context_add_class(gtk_widget_get_style_context(app->generate_btn), "green");
        gtk_widget_queue_draw(app->generate_btn);
    }
    gtk_widget_destroy(dialog);
}

static void select_pdf_cb(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;

    gchar *title = g_strdup_printf("PDFuse — %s", tr("select_pdf"));
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        title,
        GTK_WINDOW(app->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "gtk-cancel", GTK_RESPONSE_CANCEL,
        "gtk-open", GTK_RESPONSE_ACCEPT,
        NULL);
    g_free(title);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 400);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
    gtk_window_resize(GTK_WINDOW(dialog), 600, 400);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "PDF files");
    gtk_file_filter_add_pattern(filter, "*.pdf");
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        app->selected_pdf = g_strdup(file);
        g_free(file);
        gtk_entry_set_text(GTK_ENTRY(app->path_entry), app->selected_pdf);
        gtk_widget_set_sensitive(app->import_btn, TRUE);
        gtk_style_context_add_class(gtk_widget_get_style_context(app->import_btn), "green");
        gtk_widget_queue_draw(app->import_btn);
    }
    gtk_widget_destroy(dialog);
}

// ========================
// Функции для вызова внешних программ (pandoc, weasyprint)
// ========================
static gboolean run_command(const gchar **argv, GError **error) {
    gint exit_status = 0;
    gchar *stdout_str = NULL, *stderr_str = NULL;
    gboolean success = g_spawn_sync(
        NULL,               // рабочая директория (NULL = текущая)
        (gchar **)argv,
        NULL,               // окружение (NULL = наследуется)
        G_SPAWN_SEARCH_PATH,
        NULL, NULL,
        &stdout_str, &stderr_str,
        &exit_status,
        error);

    if (success && exit_status == 0) {
        g_free(stdout_str);
        g_free(stderr_str);
        return TRUE;
    } else {
        if (error && *error == NULL) {
            g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                        tr("command_failed"), exit_status, stderr_str ? stderr_str : "");
        }
        g_free(stdout_str);
        g_free(stderr_str);
        return FALSE;
    }
}

// ========================
// Упаковка исходников в бинарный поток с заголовком PDFUSE_SOURCES
// ========================
// Рекурсивно добавляет папку в ZIP, сохраняя структуру
static void add_folder_to_zip(zip_t *zip, const gchar *folder,
                              const gchar *base_prefix) {
    GDir *dir = g_dir_open(folder, 0, NULL);
    if (!dir) return;

    const gchar *name;
    while ((name = g_dir_read_name(dir))) {
        gchar *full_path = g_build_filename(folder, name, NULL);
        gchar *archive_name;
        if (base_prefix && *base_prefix)
            archive_name = g_strdup_printf("%s/%s", base_prefix, name);
        else
            archive_name = g_strdup(name);

        if (g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
            add_folder_to_zip(zip, full_path, archive_name);
        } else {
            zip_source_t *src = zip_source_file(zip, full_path, 0, 0);
            if (src && zip_file_add(zip, archive_name, src,
                                    ZIP_FL_OVERWRITE) < 0) {
                zip_source_free(src);
            }
        }
        g_free(archive_name);
        g_free(full_path);
    }
    g_dir_close(dir);
}

static gboolean pack_sources(const gchar *folder, const gchar *md_file,
                             guchar **out_data, gsize *out_len) {
    // Проверка входных параметров
    if (!folder || !md_file || !out_data || !out_len) {
        return FALSE;
    }
    gchar *tmpdir = g_dir_make_tmp("sources_XXXXXX", NULL);
    if (!tmpdir) return FALSE;

    gchar *zip_path = g_build_filename(tmpdir, "sources.zip", NULL);
    int errorp = 0;
    zip_t *zip = zip_open(zip_path, ZIP_CREATE | ZIP_TRUNCATE, &errorp);
    if (!zip) {
        // Не удалось создать ZIP-архив — ошибка возвращена с помощью команды return FALSE
        g_free(zip_path);
        g_free(tmpdir);
        return FALSE;
    }

    // 1. Добавляем только обрабатываемый .md файл
    gchar *md_basename = g_path_get_basename(md_file);
    zip_source_t *src = zip_source_file(zip, md_file, 0, 0);
    if (src && zip_file_add(zip, md_basename, src, ZIP_FL_OVERWRITE) < 0) {
        zip_source_free(src);
    }
    g_free(md_basename);

    // 2. Добавляем всю папку style (включая style.css, логотипы и т.д.)
    gchar *style_dir = g_build_filename(folder, "style", NULL);
    if (g_file_test(style_dir, G_FILE_TEST_IS_DIR)) {
        add_folder_to_zip(zip, style_dir, "style");
    }
    g_free(style_dir);

    // 3. Добавляем только картинки, на которые ссылается .md
    gchar *content = NULL;
    if (g_file_get_contents(md_file, &content, NULL, NULL)) {
        const gchar *p = content;
        while ((p = strstr(p, "](")) != NULL) {
            p += 2;
            const gchar *end = p;
            while (*end && *end != ')' && *end != ' ' &&
                   *end != '"' && *end != '\n')
                end++;

            if (end > p) {
                gchar *ref = g_strndup(p, end - p);
                if (g_str_has_suffix(ref, ".png") ||
                    g_str_has_suffix(ref, ".jpg") ||
                    g_str_has_suffix(ref, ".jpeg")) {
                    gchar *base = g_path_get_basename(ref);
                    gchar *img_path = g_build_filename(folder, "images",
                                                       base, NULL);
                    if (g_file_test(img_path, G_FILE_TEST_EXISTS)) {
                        gchar *archive_name = g_strdup_printf("images/%s", base);
                        zip_source_t *src = zip_source_file(zip, img_path, 0, 0);
                        if (src && zip_file_add(zip, archive_name, src,
                                                ZIP_FL_OVERWRITE) < 0) {
                            zip_source_free(src);
                        }
                        g_free(archive_name);
                    }
                    g_free(img_path);
                    g_free(base);
                }
                g_free(ref);
            }
            p = end;
        }
        g_free(content);
    }

    zip_close(zip);

    gsize zip_len;
    gchar *zip_data;
    if (!g_file_get_contents(zip_path, &zip_data, &zip_len, NULL)) {
        g_free(zip_path);
        g_free(tmpdir);
        return FALSE;
    }

    uLongf compressed_len = compressBound(zip_len);
    guchar *compressed = g_malloc(compressed_len);
    if (compress2(compressed, &compressed_len, (const Bytef *)zip_data,
                  zip_len, 6) != Z_OK) {
        g_free(zip_data);
        g_free(compressed);
        g_free(zip_path);
        g_free(tmpdir);
        return FALSE;
    }

    const gchar header[] = "PDFUSE_SOURCES\x00\x00";
    const gsize header_len = 17;
    guchar *result = g_malloc(header_len + compressed_len);
    memcpy(result, header, header_len);
    memcpy(result + header_len, compressed, compressed_len);

    *out_data = result;
    *out_len = header_len + compressed_len;

    g_free(zip_data);
    g_free(compressed);
    g_unlink(zip_path);
    g_free(zip_path);
    g_free(tmpdir);
    return TRUE;
}

// ========================
// Распаковка исходников из данных
// ========================
static gboolean unpack_sources(const guchar *data, gsize len, const gchar *output_folder) {
// Проверка входных параметров
if (!data || !output_folder || len == 0) {
    return FALSE;
}
    const gchar header[] = "PDFUSE_SOURCES\x00\x00";
    const gsize header_len = 17;
    if (len < header_len || memcmp(data, header, header_len) != 0) {
        return FALSE;
    }

    // Распаковываем zlib
    const guchar *compressed = data + header_len;
    gsize compressed_len = len - header_len;

    // Определяем размер распакованных данных
    uLongf zip_len = compressed_len * 4;
    guchar *zip_data = NULL;
    int ret;
    do {
        zip_data = g_realloc(zip_data, zip_len);
        ret = uncompress(zip_data, &zip_len, compressed, compressed_len);
        if (ret == Z_BUF_ERROR) {
            zip_len *= 2;
        } else if (ret != Z_OK) {
            g_free(zip_data);
            return FALSE;
        }
    } while (ret == Z_BUF_ERROR);

    // Записываем во временный ZIP файл
    gchar *tmpzip = g_build_filename(output_folder, "_temp.zip", NULL);
    if (!g_file_set_contents(tmpzip, (const gchar *)zip_data, zip_len, NULL)) {
        g_free(zip_data);
        g_free(tmpzip);
        return FALSE;
    }
    g_free(zip_data);

    // Извлекаем
    int errorp = 0;
    zip_t *zip = zip_open(tmpzip, ZIP_RDONLY, &errorp);
    if (!zip) {
        g_unlink(tmpzip);
        g_free(tmpzip);
        return FALSE;
    }

    gint num_entries = zip_get_num_entries(zip, 0);
    for (gint i = 0; i < num_entries; i++) {
        const char *fname = zip_get_name(zip, i, 0);
        if (!fname) continue;
        gchar *out_path = g_build_filename(output_folder, fname, NULL);
        // Создаём промежуточные каталоги, если нужно
        gchar *dirname = g_path_get_dirname(out_path);
        g_mkdir_with_parents(dirname, 0755);
        g_free(dirname);

        zip_file_t *zf = zip_fopen_index(zip, i, 0);
        if (zf) {
            GByteArray *content = g_byte_array_new();
            guchar buf[4096];
            zip_int64_t n;
            while ((n = zip_fread(zf, buf, sizeof(buf))) > 0) {
                g_byte_array_append(content, buf, n);
            }
            zip_fclose(zf);
            g_file_set_contents(out_path, (const gchar *)content->data, content->len, NULL);
            g_byte_array_free(content, TRUE);
        }
        g_free(out_path);
    }

    zip_close(zip);
    g_unlink(tmpzip);
    g_free(tmpzip);
    return TRUE;
}

// ========================
// Функции для работы с MuPDF
// ========================
static pdf_obj *resolve_if_indirect(fz_context *ctx, pdf_obj *obj) {
    if (obj && pdf_is_indirect(ctx, obj))
        return pdf_resolve_indirect(ctx, obj);
    return obj;
}

// Встраивание упакованных исходников в PDF
static gboolean embed_sources_into_pdf(const gchar *pdf_path,
                                       const guchar *data,
                                       gsize len)
{
// Проверка входных параметров
if (!pdf_path || !data || len == 0) {
    return FALSE;
}
    gboolean success = FALSE;
    fz_context *ctx = NULL;
    fz_document *doc = NULL;
    gchar *tmp_pdf = NULL;

    ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx)
        return FALSE;

    fz_register_document_handlers(ctx);
    fz_try(ctx) {
        doc = fz_open_document(ctx, pdf_path);
        pdf_document *pdf = pdf_specifics(ctx, doc);
        if (!pdf)
            fz_throw(ctx, FZ_ERROR_GENERIC, "document is not a PDF");

        fz_buffer *buf = fz_new_buffer_from_copied_data(ctx, data, len);

        // Создаём поток как косвенный объект и записываем в него данные
        pdf_obj *stream_dict = pdf_new_dict(ctx, pdf, 2);
        pdf_obj *stream_ref = pdf_add_object(ctx, pdf, stream_dict);
        pdf_drop_obj(ctx, stream_dict);
        pdf_update_stream(ctx, pdf, stream_ref, buf, 0);
        fz_drop_buffer(ctx, buf);
        buf = NULL;
        if (!stream_ref)
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot create PDF stream");

        pdf_obj *fs_dict = pdf_add_new_dict(ctx, pdf, 6);
        pdf_dict_put(ctx, fs_dict, PDF_NAME(Type), PDF_NAME(Filespec));
        pdf_dict_put_text_string(ctx, fs_dict, PDF_NAME(F), "SOURCES.bin");
        pdf_dict_put_text_string(ctx, fs_dict, PDF_NAME(UF), "SOURCES.bin");

        pdf_obj *ef_ref = pdf_add_new_dict(ctx, pdf, 2);
        pdf_dict_put(ctx, ef_ref, PDF_NAME(F), stream_ref);
        pdf_dict_put(ctx, fs_dict, PDF_NAME(EF), ef_ref);

        pdf_obj *names_arr = pdf_new_array(ctx, pdf, 2);
        pdf_array_push_text_string(ctx, names_arr, "SOURCES.bin");
        pdf_array_push(ctx, names_arr, fs_dict);

        pdf_obj *trailer = pdf_trailer(ctx, pdf);
        pdf_obj *root = pdf_dict_get(ctx, trailer, PDF_NAME(Root));
        if (!root)
            fz_throw(ctx, FZ_ERROR_GENERIC, "PDF root not found");

        if (pdf_is_indirect(ctx, root))
            root = pdf_resolve_indirect(ctx, root);

        pdf_obj *names = pdf_dict_get(ctx, root, PDF_NAME(Names));
        if (!names || !pdf_is_dict(ctx, names)) {
            names = pdf_add_new_dict(ctx, pdf, 2);
            pdf_dict_put(ctx, root, PDF_NAME(Names), names);
        }

        pdf_obj *embedded = pdf_dict_get(ctx, names, PDF_NAME(EmbeddedFiles));
        if (!embedded || !pdf_is_dict(ctx, embedded)) {
            embedded = pdf_add_new_dict(ctx, pdf, 2);
            pdf_dict_put(ctx, names, PDF_NAME(EmbeddedFiles), embedded);
        }

        pdf_dict_put(ctx, embedded, PDF_NAME(Names), names_arr);

        tmp_pdf = g_strdup_printf("%s.tmp.%d.pdf", pdf_path, (int)getpid());
        pdf_save_document(ctx, pdf, tmp_pdf, NULL);

        success = TRUE;
    }
    fz_catch(ctx) {
        // Ошибка MuPDF — статус остается FALSE, обработка будет произведена ниже
        success = FALSE;
    }

    fz_drop_document(ctx, doc);
    fz_drop_context(ctx);
    // Если добавление не удалось, очищаем временный файл и возвращаем FALSE
    // Функция (generate_pdf_cb) игнорирует эту ошибку — PDF-файл остается действительным и без вложений.
    if (!success) {
        if (tmp_pdf) {
            g_unlink(tmp_pdf);
            g_free(tmp_pdf);
        }
        return FALSE;
    }

    if (g_rename(tmp_pdf, pdf_path) != 0) {
        g_unlink(tmp_pdf);
        g_free(tmp_pdf);
        return FALSE;
    }

    g_free(tmp_pdf);
    return TRUE;
}
// Конвертация в графический монолит
static gboolean convert_to_image_pdf(const gchar *pdf_path) {
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) return FALSE;
    fz_register_document_handlers(ctx);

    gboolean ok = FALSE;
    fz_document *doc = NULL;
    pdf_document *new_pdf = NULL;
    gchar *tmp_pdf = NULL;

    fz_try(ctx) {
        doc = fz_open_document(ctx, pdf_path);
        new_pdf = pdf_create_document(ctx);

        int num_pages = fz_count_pages(ctx, doc);
        for (int i = 0; i < num_pages; i++) {
    fz_page *page = fz_load_page(ctx, doc, i);
    fz_rect bounds = fz_bound_page(ctx, page);

    fz_matrix ctm = fz_scale(150.0f / 72.0f, 150.0f / 72.0f);
    fz_pixmap *pix = fz_new_pixmap_from_page(ctx, page, ctm,
                                             fz_device_rgb(ctx), 0);

    gchar *tmp_jpg = g_strdup_printf("%s.page%d.jpg", pdf_path, i);
    fz_save_pixmap_as_jpeg(ctx, pix, tmp_jpg, 90);
    fz_drop_pixmap(ctx, pix);

    fz_image *img = fz_new_image_from_file(ctx, tmp_jpg);
    g_unlink(tmp_jpg);
    g_free(tmp_jpg);

    pdf_obj *page_obj = pdf_add_page(ctx, new_pdf, bounds, 0,
                                     NULL, NULL);
    pdf_obj *img_obj = pdf_add_image(ctx, new_pdf, img);
    fz_drop_image(ctx, img);

    float w = bounds.x1 - bounds.x0;
    float h = bounds.y1 - bounds.y0;
    fz_buffer *buf = fz_new_buffer(ctx, 256);
    fz_append_printf(ctx, buf,
                     "q %g 0 0 %g 0 0 cm /Im1 Do Q\n", w, h);
    pdf_obj *contents = pdf_add_stream(ctx, new_pdf, buf, NULL, 0);
    fz_drop_buffer(ctx, buf);
    pdf_dict_put(ctx, page_obj, PDF_NAME(Contents), contents);

    pdf_obj *resources = pdf_dict_get(ctx, page_obj,
                                      PDF_NAME(Resources));
    if (!resources) {
        resources = pdf_add_new_dict(ctx, new_pdf, 2);
        pdf_dict_put(ctx, page_obj, PDF_NAME(Resources), resources);
    }
    pdf_obj *xobjects = pdf_dict_get(ctx, resources,
                                     PDF_NAME(XObject));
    if (!xobjects) {
        xobjects = pdf_add_new_dict(ctx, new_pdf, 2);
        pdf_dict_put(ctx, resources, PDF_NAME(XObject), xobjects);
    }
    pdf_dict_puts(ctx, xobjects, "Im1", img_obj);

    pdf_insert_page(ctx, new_pdf, -1, page_obj);

    fz_drop_page(ctx, page);
}

        tmp_pdf = g_strdup_printf("%s.tmp.%d.pdf", pdf_path, (int)getpid());
        pdf_save_document(ctx, new_pdf, tmp_pdf, NULL);
        ok = TRUE;
    }
    fz_catch(ctx) {
        ok = FALSE;
    }

    pdf_drop_document(ctx, new_pdf);
    fz_drop_document(ctx, doc);
    fz_drop_context(ctx);

    if (!ok) {
        if (tmp_pdf) {
            g_unlink(tmp_pdf);
            g_free(tmp_pdf);
        }
        return FALSE;
    }

    if (g_rename(tmp_pdf, pdf_path) != 0) {
        g_unlink(tmp_pdf);
        g_free(tmp_pdf);
        return FALSE;
    }

    g_free(tmp_pdf);
    return TRUE;
}

// Извлечение данных из PDF
static gboolean extract_embedded_sources(const gchar *pdf_path,
                                         guchar **out_data,
                                         gsize *out_len)
{
    gboolean found = FALSE;
    fz_context *ctx = NULL;
    fz_document *doc = NULL;

    *out_data = NULL;
    *out_len = 0;

    ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx)
        return FALSE;

    fz_register_document_handlers(ctx);

    fz_try(ctx) {
        doc = fz_open_document(ctx, pdf_path);
        if (!doc)
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot open document");

        pdf_document *pdf = pdf_specifics(ctx, doc);
        if (!pdf)
            fz_throw(ctx, FZ_ERROR_GENERIC, "document is not PDF");

        pdf_obj *root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf),
                                     PDF_NAME(Root));
        root = resolve_if_indirect(ctx, root);
        if (!root)
            fz_throw(ctx, FZ_ERROR_GENERIC, "no root");

        pdf_obj *names = resolve_if_indirect(ctx,
            pdf_dict_get(ctx, root, PDF_NAME(Names)));
        if (!names)
            fz_throw(ctx, FZ_ERROR_GENERIC, "no /Names");

        pdf_obj *embedded = resolve_if_indirect(ctx,
            pdf_dict_get(ctx, names, PDF_NAME(EmbeddedFiles)));
        if (!embedded)
            fz_throw(ctx, FZ_ERROR_GENERIC, "no /EmbeddedFiles");

        pdf_obj *names_arr = resolve_if_indirect(ctx,
            pdf_dict_get(ctx, embedded, PDF_NAME(Names)));
        if (!names_arr || !pdf_is_array(ctx, names_arr))
            fz_throw(ctx, FZ_ERROR_GENERIC, "no Names array");

        int n = pdf_array_len(ctx, names_arr);

        for (int i = 0; i + 1 < n; i += 2) {
            pdf_obj *name_obj = pdf_array_get(ctx, names_arr, i);
            pdf_obj *fs_obj = pdf_array_get(ctx, names_arr, i + 1);

            const char *name = pdf_to_text_string(ctx, name_obj);

            if (name && strcmp(name, "SOURCES.bin") == 0) {
                fs_obj = resolve_if_indirect(ctx, fs_obj);

                // НЕ разворачиваем ссылки — пусть pdf_load_stream сам их разрешит
                pdf_obj *ef = pdf_dict_get(ctx, fs_obj, PDF_NAME(EF));
                pdf_obj *stream_ref = pdf_dict_get(ctx, ef, PDF_NAME(F));

                if (stream_ref) {
                    fz_buffer *buf = pdf_load_stream(ctx, stream_ref);
                    if (buf) {
                        unsigned char *ptr = NULL;
                        size_t size = fz_buffer_storage(ctx, buf, &ptr);

                        *out_data = g_malloc(size);
                        memcpy(*out_data, ptr, size);
                        *out_len = size;
                        found = TRUE;

                        fz_drop_buffer(ctx, buf);
                    }
                }
                break;
            }
        }
    }
    fz_catch(ctx) {
        // Ошибка MuPDF при извлечении — выполняем очистку и возвращаем FALSE
        // Вызов переключится на извлечение текста
        found = FALSE;
        if (*out_data) {
            g_free(*out_data);
            *out_data = NULL;
        }
        *out_len = 0;
    }

    if (doc)
        fz_drop_document(ctx, doc);
    fz_drop_context(ctx);

    return found;
}

// Извлечение текста из PDF (для fallback импорта)
static gchar *extract_text_from_pdf(const gchar *pdf_path) {
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) return NULL;
    fz_register_document_handlers(ctx);

    fz_document *doc = fz_open_document(ctx, pdf_path);
    if (!doc) {
        fz_drop_context(ctx);
        return NULL;
    }

    GString *text = g_string_new("");
    int num_pages = fz_count_pages(ctx, doc);
    for (int i = 0; i < num_pages; i++) {
        fz_page *page = fz_load_page(ctx, doc, i);
        fz_stext_page *stext = fz_new_stext_page_from_page(ctx, page, NULL);
        if (stext) {
            fz_stext_block *block;
            for (block = stext->first_block; block; block = block->next) {
                if (block->type == FZ_STEXT_BLOCK_TEXT) {
                    fz_stext_line *line;
                    for (line = block->u.t.first_line; line; line = line->next) {
                        fz_stext_char *ch;
                        for (ch = line->first_char; ch; ch = ch->next) {
                            g_string_append_unichar(text, ch->c);
                        }
                        g_string_append_c(text, '\n');
                    }
                }
            }
            fz_drop_stext_page(ctx, stext);
        }
        fz_drop_page(ctx, page);
    }

    fz_drop_document(ctx, doc);
    fz_drop_context(ctx);
    return g_string_free(text, FALSE);
}

//Поиск требуемых для работы инструментов
/// Каталог, в котором лежит исполняемый файл
static gchar *get_exe_dir(void) {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    gchar *utf8 = g_utf16_to_utf8((const gunichar2 *)path, -1,
                                  NULL, NULL, NULL);
    gchar *dir = g_path_get_dirname(utf8);
    g_free(utf8);
    return dir;
#else
    gchar *link = g_file_read_link("/proc/self/exe", NULL);
    if (link) {
        gchar *dir = g_path_get_dirname(link);
        g_free(link);
        return dir;
    }
    return g_strdup(".");
#endif
}

/// Ищем инструмент: сначала рядом с exe, затем в системном PATH
static gchar *find_tool(const gchar *name) {
    gchar *exe_dir = get_exe_dir();
    gchar *local = g_build_filename(exe_dir, name, NULL);
    gchar *local_exe = g_strconcat(local, ".exe", NULL);
    g_free(exe_dir);

    if (g_file_test(local, G_FILE_TEST_EXISTS)) {
        g_free(local_exe);
        return local;
    }
    g_free(local);

    if (g_file_test(local_exe, G_FILE_TEST_EXISTS)) {
        return local_exe;
    }
    g_free(local_exe);

    return g_find_program_in_path(name);
}

// ========================
// Генерация PDF из Markdown
// ========================
static void generate_pdf_cb(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    // Всегда читаем фактическое состояние чекбокса
    app->attach_sources =
    gtk_toggle_button_get_active(
    GTK_TOGGLE_BUTTON(app->check_attach_sources));

    if (!app->folder_path) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   "%s", tr("folder_not_selected"));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    gchar *style_css = g_build_filename(app->folder_path,
                                   "style",
                                   "style.css",
                                   NULL);
    if (!g_file_test(style_css, G_FILE_TEST_EXISTS)) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   "%s", tr("no_style_css"));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_free(style_css);
        return;
    }

    // Собираем список .md файлов (кроме temp_ и .title.md)
    GDir *dir = g_dir_open(app->folder_path, 0, NULL);
    if (!dir) {
        g_free(style_css);
        return;
    }
    GPtrArray *md_files = g_ptr_array_new_with_free_func(g_free);
    const gchar *name;
    while ((name = g_dir_read_name(dir))) {
        if (g_str_has_suffix(name, ".md") && !g_str_has_prefix(name, "temp_") && !g_str_has_suffix(name, ".title.md")) {
            g_ptr_array_add(md_files, g_build_filename(app->folder_path, name, NULL));
        }
    }
    g_dir_close(dir);

    if (md_files->len == 0) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_INFO,
                                                   GTK_BUTTONS_OK,
                                                   "%s", tr("no_documents"));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_ptr_array_free(md_files, TRUE);
        g_free(style_css);
        return;
    }
    // Проверяем наличие pandoc и weasyprint
    gchar *pandoc_path = find_tool("pandoc");
    gchar *weasyprint_path = find_tool("weasyprint");
    if (!pandoc_path) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_OK,
                                               "%s", tr("pandoc_not_found"));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_ptr_array_free(md_files, TRUE);
    g_free(style_css);
    g_free(weasyprint_path);
    return;
    }
    if (!weasyprint_path) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_OK,
                                               "%s", tr("weasyprint_not_found"));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_ptr_array_free(md_files, TRUE);
    g_free(style_css);
    g_free(pandoc_path);
    return;
    }
    // Подтверждение
    gchar *template = prepare_template(tr("confirm_message"));
    gchar *confirm_msg = g_strdup_printf(template, md_files->len);
    g_free(template);

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "%s", confirm_msg);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(confirm_msg);
    if (response != GTK_RESPONSE_YES) {
        g_ptr_array_free(md_files, TRUE);
        g_free(style_css);
        return;
    }

    // Обработка каждого файла
    gint success_count = 0;
    for (guint i = 0; i < md_files->len; i++) {
        gchar *md_file = g_ptr_array_index(md_files, i);
        gchar *md_basename = g_path_get_basename(md_file);
        gchar *stem = g_strndup(md_basename, strlen(md_basename) - 3); // убираем .md
        gchar *temp_md = g_strdup_printf("%s/temp_%s", app->folder_path, md_basename);
        gchar *html_path = g_strdup_printf("%s/%s.html", app->folder_path, stem);
        gchar *pdf_path = g_strdup_printf("%s/%s.pdf", app->folder_path, stem);

        // Читаем содержимое
        gchar *content = NULL;
        if (g_file_get_contents(md_file, &content, NULL, NULL)) {

            // Записываем во временный файл
            g_file_set_contents(temp_md, content, -1, NULL);
            g_free(content);
        }
        // Вызываем pandoc
        const gchar *pandoc_argv[] = {pandoc_path, temp_md, "-o", html_path, NULL};
        GError *error = NULL;
        if (run_command(pandoc_argv, &error)) {
            // Вызываем weasyprint
            const gchar *weasy_argv[] = {weasyprint_path, html_path, pdf_path, "--stylesheet", style_css, NULL};
            if (run_command(weasy_argv, &error)) {
                // Встраиваем исходники или конвертируем в изображения
                if (app->attach_sources) {
                    // Возможность: встроить исходные файлы в PDF-файл.
                    // Если это не удастся, PDF-файл все равно будет успешно создан без вложений.
                    guchar *packed = NULL;
                    gsize packed_len = 0;
                    if (pack_sources(app->folder_path, md_file, &packed, &packed_len)) {
                        if (embed_sources_into_pdf(pdf_path, packed, packed_len)) {
                            // Источники успешно добавлены.
                        } else {
                            // Функция embed_sources_into_pdf завершилась с ошибкой — PDF-файл по-прежнему действителен.
                        }
                        g_free(packed);
                    } else {
                        // Ошибка в pack_sources — PDF-файл будет создан без вложений.
                    }
                } else {
                    convert_to_image_pdf(pdf_path);
                }
                success_count++;
            }
        }

        if (error) {
            // Преобразование не удалось — ошибка уже была показана в диалоговом окне.
            g_clear_error(&error);
        }

        // Удаляем временные файлы
        g_unlink(temp_md);
        g_unlink(html_path);

        g_free(md_basename);
        g_free(stem);
        g_free(temp_md);
        g_free(html_path);
        g_free(pdf_path);
    }

    g_ptr_array_free(md_files, TRUE);
    g_free(style_css);
    g_free(pandoc_path);
    g_free(weasyprint_path);

    gchar *template2 = prepare_template(tr("success_message"));
    gchar *success_msg = g_strdup_printf(template2, success_count);
    g_free(template2);

    dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                    GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_INFO,
                                    GTK_BUTTONS_OK,
                                    "%s", success_msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(success_msg);
}

// ========================
// Импорт PDF в Markdown
// ========================

static void import_pdf_cb(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    if (!app->selected_pdf) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   "%s", tr("pdf_not_selected"));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    gchar *pdf_file = g_strdup(app->selected_pdf);
    gchar *basename = g_path_get_basename(pdf_file);
    gchar *stem = g_strdup(basename);
    // Убираем расширение .pdf
    gchar *dot = g_strrstr(stem, ".pdf");
    if (dot) *dot = '\0';
    gchar *folder_name = g_strdup(stem);
    gchar *pdf_dir = g_path_get_dirname(pdf_file);
    gchar *output_folder = g_build_filename(pdf_dir, folder_name, NULL);
    g_free(pdf_dir);

    // Ищем встроенные исходники
    guchar *sources_data = NULL;
    gsize sources_len = 0;
    gboolean has_sources = extract_embedded_sources(pdf_file, &sources_data, &sources_len);

    if (has_sources) {
        gchar *template = prepare_template(tr("attachments_found"));
        gchar *attachments_msg = g_strdup_printf(template, output_folder);
        g_free(template);

        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_QUESTION,
                                                   GTK_BUTTONS_YES_NO,
                                                   "%s", attachments_msg);
        g_free(attachments_msg);
        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        if (response == GTK_RESPONSE_YES) {
            // Создаём папку после согласия
            g_mkdir_with_parents(output_folder, 0755);
            if (unpack_sources(sources_data, sources_len, output_folder)) {
                gchar *template = prepare_template(tr("import_success"));
                gchar *import_msg = g_strdup_printf(template, output_folder);
                g_free(template);

                dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_INFO,
                                                GTK_BUTTONS_OK,
                                                "%s", import_msg);
                g_free(import_msg);
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
            } else {
                dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_OK,
                                                "%s", tr("unpack_failed"));
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
            }
            g_free(sources_data);
            g_free(output_folder);
            g_free(folder_name);
            g_free(stem);
            g_free(basename);
            g_free(pdf_file);
            return;
        }
        g_free(sources_data);
    }

    // Fallback: извлечение текста
    // Сначала проверяем, есть ли вообще что извлекать
    gchar *text = extract_text_from_pdf(pdf_file);

    gboolean has_text = FALSE;
    if (text) {
        const gchar *p = text;
        while (*p) {
            if (!g_ascii_isspace(*p)) {
                has_text = TRUE;
                break;
            }
            p++;
        }
    }

    if (!has_text) {
        // PDF-файл представляет собой только изображение (монолит) — текстовый слой для извлечения отсутствует.
        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(app->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_OK,
            "%s",
            tr("no_text_layer"));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        g_free(text);
        g_free(output_folder);
        g_free(folder_name);
        g_free(stem);
        g_free(basename);
        g_free(pdf_file);
        return;
    }

    // Текст есть — спрашиваем подтверждение
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "%s", tr("no_attachments"));
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response != GTK_RESPONSE_YES) {
        g_free(text);
        g_free(output_folder);
        g_free(folder_name);
        g_free(stem);
        g_free(basename);
        g_free(pdf_file);
        return;
    }
// Создаём папку только сейчас
    g_mkdir_with_parents(output_folder, 0755);

    // Сохраняем весь текст в один файл
    gchar *md_name = g_strconcat(stem, ".md", NULL);
    gchar *md_path = g_build_filename(output_folder, md_name, NULL);
    g_file_set_contents(md_path, text, -1, NULL);
    g_free(md_name);
    g_free(md_path);

    // Скопируем стандартный style.css из assets (если он существует)
    //gchar *default_css = g_build_filename("assets", "style.css", NULL);
    gchar *exe_dir = get_exe_dir();
    gchar *default_css = g_build_filename(exe_dir, "assets", "style.css", NULL);
    g_free(exe_dir);
    if (g_file_test(default_css, G_FILE_TEST_EXISTS)) {
        gchar *dest_css = g_build_filename(output_folder, "style.css", NULL);
        gchar *css_data = NULL;
        gsize css_len = 0;
        if (g_file_get_contents(default_css, &css_data, &css_len, NULL)) {
            g_file_set_contents(dest_css, css_data, css_len, NULL);
            g_free(css_data);
        }
        g_free(dest_css);
    }
    g_free(default_css);

    // Создаём папку images
    gchar *images_dir = g_build_filename(output_folder, "images", NULL);
    g_mkdir_with_parents(images_dir, 0755);
    g_free(images_dir);

    gchar *template = prepare_template(tr("import_success"));
    gchar *import_msg = g_strdup_printf(template, output_folder);
    g_free(template);

    dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                    GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_INFO,
                                    GTK_BUTTONS_OK,
                                    "%s", import_msg);
    g_free(import_msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    g_free(text);
    g_free(output_folder);
    g_free(folder_name);
    g_free(stem);
    g_free(basename);
    g_free(pdf_file);
}
//Обработка нажатия чекбокса
static void on_attach_sources_toggled(GtkWidget *widget, gpointer data)
{
    (void)data;
    app->attach_sources =
    gtk_toggle_button_get_active(
    GTK_TOGGLE_BUTTON(widget));
}

// ========================
// Создание интерфейса
// ========================
static void about_cb(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;

    // Создаём немодальное окно
    GtkWidget *dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(dialog), tr("about_title"));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 550);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);

    // Контейнер для всего содержимого
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(dialog), vbox);

    // Прокручиваемая область с текстом
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 5);

    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(text_view), 10);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(text_view), 10);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(buffer, tr("about_text"), -1);

    gtk_container_add(GTK_CONTAINER(scrolled), text_view);

    // Блок с донатами
    GtkWidget *donate_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    GdkRGBA bg_color;
    if (gdk_rgba_parse(&bg_color, "#e0e0e0")) {
        gtk_widget_override_background_color(donate_vbox, GTK_STATE_FLAG_NORMAL, &bg_color);
    }
    gtk_box_pack_start(GTK_BOX(vbox), donate_vbox, FALSE, FALSE, 5);

    GtkWidget *donate_label = gtk_label_new(tr("donate_title"));
    gtk_widget_set_margin_top(donate_label, 5);
    gtk_widget_set_margin_bottom(donate_label, 5);
    gtk_widget_set_margin_start(donate_label, 10);
    gtk_label_set_xalign(GTK_LABEL(donate_label), 0.0);
    gtk_box_pack_start(GTK_BOX(donate_vbox), donate_label, FALSE, FALSE, 0);

    GtkWidget *boosty_btn = gtk_button_new_with_label(tr("donate_boosty"));
    g_signal_connect(boosty_btn, "clicked", G_CALLBACK(open_url), "https://boosty.to/automatonlab");
    gtk_widget_set_size_request(boosty_btn, -1, 25);
    gtk_button_set_relief(GTK_BUTTON(boosty_btn), GTK_RELIEF_NONE);
    gtk_widget_set_halign(boosty_btn, GTK_ALIGN_START);
    gtk_widget_set_margin_start(boosty_btn, 10);
    gtk_widget_set_margin_end(boosty_btn, 10);
    gtk_box_pack_start(GTK_BOX(donate_vbox), boosty_btn, FALSE, FALSE, 2);

    GtkWidget *kofi_btn = gtk_button_new_with_label(tr("donate_kofi"));
    g_signal_connect(kofi_btn, "clicked", G_CALLBACK(open_url), "https://ko-fi.com/automatonlab");
    gtk_widget_set_size_request(kofi_btn, -1, 25);
    gtk_button_set_relief(GTK_BUTTON(kofi_btn), GTK_RELIEF_NONE);
    gtk_widget_set_halign(kofi_btn, GTK_ALIGN_START);
    gtk_widget_set_margin_start(kofi_btn, 10);
    gtk_widget_set_margin_end(kofi_btn, 10);
    gtk_box_pack_start(GTK_BOX(donate_vbox), kofi_btn, FALSE, FALSE, 2);

    gtk_widget_show_all(dialog);
}

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    (void)user_data;
    app = g_new0(App, 1);
    app->attach_sources = TRUE;
    app->current_lang = load_language();
    app->strings = NULL;

    // Создаем CSS для зелёных кнопок
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "button.green { background-color: #a8c97e; background-image: none; }"
        "button.green:disabled { background-color: #a8c97e; background-image: none; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_USER);   // выше темы
    g_object_unref(css);

    app->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app->window), "PDF Builder C");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 650, 200);
    gtk_window_set_resizable(GTK_WINDOW(app->window), FALSE);

    // Загружаем флаги
    gchar *path;
    gchar *exe_dir = get_exe_dir();
    //path = g_build_filename("assets", "flags", "ru.png", NULL);
    path = g_build_filename(exe_dir, "assets", "flags", "ru.png", NULL);
    app->flag_ru = gdk_pixbuf_new_from_file(path, NULL);
    g_free(path);
    //path = g_build_filename("assets", "flags", "en.png", NULL);
    path = g_build_filename(exe_dir, "assets", "flags", "en.png", NULL);
    app->flag_en = gdk_pixbuf_new_from_file(path, NULL);
    g_free(path);
    //path = g_build_filename("assets", "flags", "de.png", NULL);
    path = g_build_filename(exe_dir, "assets", "flags", "de.png", NULL);
    app->flag_de = gdk_pixbuf_new_from_file(path, NULL);
    g_free(path);
    //path = g_build_filename("assets", "flags", "fr.png", NULL);
    path = g_build_filename(exe_dir, "assets", "flags", "fr.png", NULL);
    app->flag_fr = gdk_pixbuf_new_from_file(path, NULL);
    g_free(path);
    //path = g_build_filename("assets", "flags", "gray_ru.png", NULL);
    path = g_build_filename(exe_dir, "assets", "flags", "gray_ru.png", NULL);
    app->flag_gray_ru = gdk_pixbuf_new_from_file(path, NULL);
    g_free(path);
    //path = g_build_filename("assets", "flags", "gray_en.png", NULL);
    path = g_build_filename(exe_dir, "assets", "flags", "gray_en.png", NULL);
    app->flag_gray_en = gdk_pixbuf_new_from_file(path, NULL);
    g_free(path);
    //path = g_build_filename("assets", "flags", "gray_de.png", NULL);
    path = g_build_filename(exe_dir, "assets", "flags", "gray_de.png", NULL);
    app->flag_gray_de = gdk_pixbuf_new_from_file(path, NULL);
    g_free(path);
    //path = g_build_filename("assets", "flags", "gray_fr.png", NULL);
    path = g_build_filename(exe_dir, "assets", "flags", "gray_fr.png", NULL);
    app->flag_gray_fr = gdk_pixbuf_new_from_file(path, NULL);
    g_free(path);
    g_free(exe_dir);

    // Загружаем локализацию по умолчанию
    load_locale(app->current_lang);

    // Создаём основную вертикальную коробку
    app->main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(app->window), app->main_vbox);

    // ========================
    // ВЕРХНЯЯ СТРОКА
    // ========================
    app->top_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(app->main_vbox), app->top_hbox, FALSE, FALSE, 5);

    // Радиокнопка "Текст → PDF"
    app->radio_md_to_pdf = gtk_radio_button_new_with_label(NULL, tr("mode_md_to_pdf"));
    gtk_widget_set_size_request(app->radio_md_to_pdf, 130, -1);   // фиксированная ширина
    gtk_box_pack_start(GTK_BOX(app->top_hbox), app->radio_md_to_pdf, FALSE, FALSE, 0);
    g_signal_connect(app->radio_md_to_pdf, "toggled", G_CALLBACK(on_mode_change), NULL);

    // Чекбокс "Прикрепить исходники"
    app->check_attach_sources = gtk_check_button_new_with_label(tr("attach_sources"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->check_attach_sources), TRUE);
    g_signal_connect(app->check_attach_sources, "toggled", G_CALLBACK(on_attach_sources_toggled), NULL);
    app->attach_sources = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->check_attach_sources));
    gtk_box_pack_start(GTK_BOX(app->top_hbox), app->check_attach_sources, FALSE, FALSE, 15);

    // Правая часть: языковые кнопки и "О программе"
    GtkWidget *right_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_pack_end(GTK_BOX(app->top_hbox), right_hbox, FALSE, FALSE, 0);

    app->lang_ru_btn = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(app->lang_ru_btn), gtk_image_new_from_pixbuf(app->flag_ru));
    g_signal_connect(app->lang_ru_btn, "clicked", G_CALLBACK(set_language_cb), (gpointer)"ru");
    gtk_box_pack_start(GTK_BOX(right_hbox), app->lang_ru_btn, FALSE, FALSE, 2);

    app->lang_en_btn = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(app->lang_en_btn), gtk_image_new_from_pixbuf(app->flag_gray_en));
    g_signal_connect(app->lang_en_btn, "clicked", G_CALLBACK(set_language_cb), (gpointer)"en");
    gtk_box_pack_start(GTK_BOX(right_hbox), app->lang_en_btn, FALSE, FALSE, 2);

    app->lang_de_btn = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(app->lang_de_btn), gtk_image_new_from_pixbuf(app->flag_gray_de));
    g_signal_connect(app->lang_de_btn, "clicked", G_CALLBACK(set_language_cb), (gpointer)"de");
    gtk_box_pack_start(GTK_BOX(right_hbox), app->lang_de_btn, FALSE, FALSE, 2);

    app->lang_fr_btn = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(app->lang_fr_btn), gtk_image_new_from_pixbuf(app->flag_gray_fr));
    g_signal_connect(app->lang_fr_btn, "clicked", G_CALLBACK(set_language_cb), (gpointer)"fr");
    gtk_box_pack_start(GTK_BOX(right_hbox), app->lang_fr_btn, FALSE, FALSE, 2);

    app->about_btn = gtk_button_new_with_label("?");
    g_signal_connect(app->about_btn, "clicked", G_CALLBACK(about_cb), NULL);
    gtk_box_pack_start(GTK_BOX(right_hbox), app->about_btn, FALSE, FALSE, 10);

    // ========================
    // ВТОРАЯ СТРОКА
    // ========================
    app->second_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(app->main_vbox), app->second_hbox, FALSE, FALSE, 0);

    // Группа радиокнопок: привязываем вторую к первой
    GSList *group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(app->radio_md_to_pdf));
    app->radio_pdf_to_md = gtk_radio_button_new_with_label(group, tr("mode_pdf_to_md"));
    gtk_box_pack_start(GTK_BOX(app->second_hbox), app->radio_pdf_to_md, FALSE, FALSE, 0);
    g_signal_connect(app->radio_pdf_to_md, "toggled", G_CALLBACK(on_mode_change), NULL);

    // ========================
    // ТРЕТЬЯ СТРОКА: кнопка выбора + поле пути
    // ========================
    app->path_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(app->main_vbox), app->path_hbox, FALSE, FALSE, 0);

    app->select_folder_btn = gtk_button_new_with_label(tr("select_folder"));
    gtk_widget_set_size_request(app->select_folder_btn, 160, -1);   // фиксированная ширина
    g_signal_connect(app->select_folder_btn, "clicked", G_CALLBACK(select_folder_cb), NULL);
    gtk_box_pack_start(GTK_BOX(app->path_hbox), app->select_folder_btn, FALSE, FALSE, 5);

    app->select_pdf_btn = gtk_button_new_with_label(tr("select_pdf"));
    gtk_widget_set_size_request(app->select_pdf_btn, 160, -1);   // фиксированная ширина
    g_signal_connect(app->select_pdf_btn, "clicked", G_CALLBACK(select_pdf_cb), NULL);
    gtk_box_pack_start(GTK_BOX(app->path_hbox), app->select_pdf_btn, FALSE, FALSE, 5);

    app->path_entry = gtk_entry_new();
    gtk_editable_set_editable(GTK_EDITABLE(app->path_entry), FALSE);
    gtk_widget_set_margin_end(app->path_entry, 10);   // отступ справа
    gtk_box_pack_start(GTK_BOX(app->path_hbox), app->path_entry, TRUE, TRUE, 0);

    // ========================
    // ЧЕТВЁРТАЯ СТРОКА: кнопка действия
    // ========================
    app->action_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(app->main_vbox), app->action_hbox, FALSE, FALSE, 0);

    app->generate_btn = gtk_button_new_with_label(tr("generate_pdf"));
    gtk_widget_set_margin_start(app->generate_btn, 5);
    gtk_widget_set_margin_end(app->generate_btn, 10);
    g_signal_connect(app->generate_btn, "clicked", G_CALLBACK(generate_pdf_cb), NULL);
    gtk_widget_set_sensitive(app->generate_btn, FALSE);
    gtk_box_pack_start(GTK_BOX(app->action_hbox), app->generate_btn, TRUE, TRUE, 0);

    app->import_btn = gtk_button_new_with_label(tr("import_pdf"));
    gtk_widget_set_margin_start(app->import_btn, 5);
    gtk_widget_set_margin_end(app->import_btn, 10);
    g_signal_connect(app->import_btn, "clicked", G_CALLBACK(import_pdf_cb), NULL);
    gtk_widget_set_sensitive(app->import_btn, FALSE);
    gtk_box_pack_start(GTK_BOX(app->action_hbox), app->import_btn, TRUE, TRUE, 0);

    // Сначала показываем все виджеты
    gtk_widget_show_all(app->window);
    // Отражаем загруженный из конфига язык на кнопках и в текстах
    update_ui_texts();
    update_language_buttons();
    //(скрываем/показываем нужные кнопки)
    on_mode_change(NULL, NULL);
}

// ========================
// main
// ========================

int main(int argc, char **argv) {
    GtkApplication *gtk_app = gtk_application_new("org.example.pdfbuilder",
                                                   G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return status;
}