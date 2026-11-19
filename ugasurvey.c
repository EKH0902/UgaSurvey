#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <wtypes.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define APP_NAME L"UgaSurvey"
#define DATA_FILE L"ugasurvey.json"
#define MAX_SURVEYS 24
#define MAX_QUESTIONS 40
#define MAX_RESPONSES 64
#define TEXT_LEN 256
#define OPTIONS_LEN 1024
#define TOKEN_CAP 12000

enum { HOME, EDITOR, RESPONDER };
enum { Q_SHORT, Q_LONG, Q_CHOICE, Q_CHECKBOX, Q_DROPDOWN, Q_SCALE, Q_RATING,
       Q_DATE, Q_TIME, Q_NUMBER, Q_RANK, Q_GRID, Q_GRID_CHECK, Q_IMAGE, Q_TYPES };
enum { ID_LIST = 100, ID_TITLE, ID_DESC, ID_QUESTIONS, ID_TYPE, ID_PROMPT, ID_OPTIONS,
       ID_REQUIRED, ID_ANSWER, ID_ADD, ID_DELETE, ID_DUPLICATE, ID_UP, ID_DOWN,
       ID_SAVE, ID_EDIT, ID_RESPOND, ID_RESULTS, ID_IMPORT, ID_EXPORT, ID_BACK,
       ID_PREVIEW, ID_PREV, ID_NEXT, ID_SUBMIT, ID_QDESC, ID_CSV, ID_TYPE_HINT,
       ID_STARTER, ID_GUIDE, ID_EXPORT_MENU, ID_EXPORT_HTML, ID_EXPORT_EXE,
       ID_IMAGE_PICK, ID_IMAGE_CLEAR, ID_IMAGE_PREVIEW, ID_IMAGE_NAME };
enum { ANSWER_TEXT, ANSWER_COMBO, ANSWER_LIST, ANSWER_MULTI, ANSWER_DATE, ANSWER_TIME };

typedef struct {
    wchar_t title[TEXT_LEN];
    wchar_t description[TEXT_LEN];
    wchar_t options[OPTIONS_LEN];
    wchar_t image_path[MAX_PATH];
    wchar_t image_name[TEXT_LEN];
    int type;
    int required;
    wchar_t answers[MAX_RESPONSES][TEXT_LEN];
} Question;

typedef struct {
    wchar_t title[TEXT_LEN];
    wchar_t description[TEXT_LEN];
    Question questions[MAX_QUESTIONS];
    int question_count;
    int response_count;
} Survey;

typedef struct { int start, end, size, parent; } Token;

static Survey surveys[MAX_SURVEYS];
static int survey_count, selected_survey = -1, selected_question = -1;
static int page = HOME, preview_mode, response_index;
static int answer_kind = ANSWER_TEXT;
static int data_load_error;
static int launch_as_respondent;
static int font_loaded;
static wchar_t draft[MAX_QUESTIONS][TEXT_LEN];
static wchar_t data_path[MAX_PATH];
static wchar_t font_path[MAX_PATH];
static wchar_t font_license_path[MAX_PATH];
static ULONG_PTR gdiplus_token;
static unsigned image_file_sequence;
static HWND main_window, controls[48];
static int control_count;
static int hovered_button = -1;
static HFONT app_font, heading_font;
static const wchar_t *type_names[Q_TYPES] = {
    L"단답형", L"장문형", L"객관식", L"체크박스", L"드롭다운",
    L"선형 배율", L"별점", L"날짜", L"시간", L"숫자 입력",
    L"순위형", L"그리드 객관식", L"그리드 체크박스", L"이미지 선택형"
};
static const wchar_t *type_hints[Q_TYPES] = {
    L"한 줄로 답변하세요.", L"긴 답변을 입력하세요.", L"선택지를 입력하고 응답자가 하나를 선택합니다.",
    L"선택지를 입력하고 여러 개를 선택합니다.", L"목록에서 하나를 선택합니다.",
    L"숫자 범위와 양 끝 설명을 선택지에 적어 주세요.", L"별점 개수를 선택지에 적어 주세요.",
    L"날짜를 입력하세요.", L"시간을 입력하세요.", L"숫자를 입력하세요.",
    L"순위를 줄마다 입력하고 응답에 원하는 순서를 적습니다.", L"행/열을 줄마다 입력하세요.",
    L"행/열을 줄마다 입력하세요.",     L"질문 이미지는 선택 항목으로 첨부하고, 선택지는 줄마다 입력하세요."
};

static void copy_text(wchar_t *dst, size_t cap, const wchar_t *src) {
    if (!cap) return;
    wcsncpy(dst, src ? src : L"", cap - 1);
    dst[cap - 1] = 0;
}

static void append_text(wchar_t *dst, size_t cap, size_t *used, const wchar_t *text) {
    if (*used >= cap - 1) return;
    size_t available = cap - *used - 1;
    size_t length = wcslen(text);
    if (length > available) length = available;
    wmemcpy(dst + *used, text, length);
    *used += length;
    dst[*used] = 0;
}

static int content_shift(void) {
    if (page == HOME) return survey_count ? 36 : 24;
    if (page == EDITOR) return 42;
    if (page == RESPONDER) return 34;
    return 0;
}

static LRESULT CALLBACK button_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                    UINT_PTR subclass_id, DWORD_PTR reference_data) {
    (void)subclass_id;
    (void)reference_data;
    if (message == WM_SETCURSOR) {
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
    }
    if (message == WM_MOUSEMOVE && hovered_button != GetDlgCtrlID(hwnd)) {
        TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&track);
        hovered_button = GetDlgCtrlID(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
    } else if (message == WM_MOUSELEAVE && hovered_button == GetDlgCtrlID(hwnd)) {
        hovered_button = -1;
        InvalidateRect(hwnd, NULL, FALSE);
    }
    if (message == WM_NCDESTROY) {
        if (hovered_button == GetDlgCtrlID(hwnd)) hovered_button = -1;
        RemoveWindowSubclass(hwnd, button_proc, (UINT_PTR)GetDlgCtrlID(hwnd));
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

static HWND add_control(const wchar_t *klass, const wchar_t *text, DWORD style,
                        int x, int y, int w, int h, int id) {
    if (y >= 100) y += content_shift();
    HWND c = CreateWindowExW(0, klass, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, main_window, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (c) {
        SendMessageW(c, WM_SETFONT, (WPARAM)(y < 50 ? heading_font : app_font), TRUE);
        if (wcscmp(klass, L"BUTTON") == 0)
            SetWindowSubclass(c, button_proc, (UINT_PTR)id, 0);
        if (control_count < (int)(sizeof(controls) / sizeof(controls[0])))
            controls[control_count++] = c;
    }
    return c;
}

static HWND button(const wchar_t *text, int x, int y, int w, int id) {
    return add_control(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, x, y, w, 38, id);
}

static HWND label(const wchar_t *text, int x, int y, int w, int h) {
    return add_control(L"STATIC", text, SS_LEFT, x, y, w, h, 0);
}

static void clear_controls(void) {
    for (int i = 0; i < control_count; ++i)
        if (IsWindow(controls[i])) DestroyWindow(controls[i]);
    control_count = 0;
}

static int parse_json(const char *s, Token *t, int cap) {
    int n = 0, parent = -1;
    for (int i = 0; s[i];) {
        unsigned char c = (unsigned char)s[i];
        if (c == '{' || c == '[') {
            if (n >= cap) return -1;
            t[n] = (Token){ i, -1, 0, parent };
            if (parent >= 0) t[parent].size++;
            parent = n++;
            ++i;
        } else if (c == '}' || c == ']') {
            if (parent < 0) return -1;
            t[parent].end = i + 1;
            parent = t[parent].parent;
            ++i;
        } else if (c == '"') {
            int start = ++i;
            while (s[i] && s[i] != '"') {
                if (s[i] == '\\' && s[i + 1]) ++i;
                ++i;
            }
            if (!s[i] || n >= cap) return -1;
            t[n] = (Token){ start, i, 0, parent };
            if (parent >= 0) t[parent].size++;
            ++n; ++i;
        } else if (c == ':' || c == ',' || c <= 32) {
            ++i;
        } else {
            int start = i;
            while (s[i] && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] > 32) ++i;
            if (n >= cap) return -1;
            t[n] = (Token){ start, i, 0, parent };
            if (parent >= 0) t[parent].size++;
            ++n;
        }
    }
    return parent == -1 ? n : -1;
}

static int token_after(const Token *t, int i, int count) {
    int end = t[i].end;
    ++i;
    while (i < count && t[i].start < end) ++i;
    return i;
}

static int object_value(const char *s, const Token *t, int count, int object, const char *key) {
    int i = object + 1;
    while (i < count && t[i].parent == object) {
        int value = i + 1;
        if (value >= count) break;
        int len = t[i].end - t[i].start;
        if (len == (int)strlen(key) && !memcmp(s + t[i].start, key, len)) return value;
        i = token_after(t, value, count);
    }
    return -1;
}

static int token_int(const char *s, Token *t, int count, int object, const char *key, int fallback) {
    int i = object_value(s, t, count, object, key);
    return i >= 0 ? atoi(s + t[i].start) : fallback;
}

static void token_wide(const char *s, Token *t, int index, wchar_t *out, size_t cap) {
    size_t used = 0;
    int i = t[index].start, end = t[index].end;
    while (i < end && used + 1 < cap) {
        if (s[i] == '\\' && i + 1 < end) {
            ++i;
            if (s[i] == 'u' && i + 4 < end) {
                unsigned value = 0;
                for (int k = 1; k <= 4; ++k) {
                    char c = s[i + k];
                    value = value * 16 + (c >= '0' && c <= '9' ? c - '0' :
                        c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                        c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
                }
                out[used++] = (wchar_t)value;
                i += 5;
                continue;
            }
            wchar_t wc = (unsigned char)s[i++];
            if (wc == 'n') wc = L'\n'; else if (wc == 'r') wc = L'\r';
            else if (wc == 't') wc = L'\t';
            out[used++] = wc;
        } else {
            unsigned char first = (unsigned char)s[i];
            int bytes = first < 0x80 ? 1 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4;
            if (i + bytes > end) { ++i; continue; }
            wchar_t decoded[2] = {0, 0};
            int read = MultiByteToWideChar(CP_UTF8, 0, s + i, bytes, decoded, 2);
            if (!read) { ++i; continue; }
            if (bytes == 4 && used + 2 < cap) {
                unsigned char b0 = (unsigned char)s[i], b1 = (unsigned char)s[i + 1];
                unsigned char b2 = (unsigned char)s[i + 2], b3 = (unsigned char)s[i + 3];
                unsigned cp = ((b0 & 7) << 18) | ((b1 & 0x3f) << 12) | ((b2 & 0x3f) << 6) | (b3 & 0x3f);
                cp -= 0x10000;
                out[used++] = (wchar_t)(0xD800 + (cp >> 10));
                out[used++] = (wchar_t)(0xDC00 + (cp & 0x3ff));
                i += 4;
            } else {
                out[used++] = decoded[0];
                i += bytes;
            }
        }
    }
    out[used] = 0;
}

static int load_json_buffer(const char *data) {
    Token *t = (Token *)calloc(TOKEN_CAP, sizeof(Token));
    if (!t) return 0;
    int count = parse_json(data, t, TOKEN_CAP);
    if (count < 1 || data[t[0].start] != '{') { free(t); return 0; }
    int root = object_value(data, t, count, 0, "surveys");
    if (root < 0 || data[t[root].start] != '[') { free(t); return 0; }
    Survey *loaded = (Survey *)calloc(MAX_SURVEYS, sizeof(Survey));
    if (!loaded) { free(t); return 0; }
    int loaded_count = 0;
    for (int si = root + 1; si < count && t[si].parent == root && loaded_count < MAX_SURVEYS; si = token_after(t, si, count)) {
        if (data[t[si].start] != '{') { free(loaded); free(t); return 0; }
        Survey *s = &loaded[loaded_count++];
        int v = object_value(data, t, count, si, "title");
        if (v >= 0) token_wide(data, t, v, s->title, TEXT_LEN);
        v = object_value(data, t, count, si, "description");
        if (v >= 0) token_wide(data, t, v, s->description, TEXT_LEN);
        s->response_count = token_int(data, t, count, si, "responseCount", 0);
        if (s->response_count < 0) s->response_count = 0;
        if (s->response_count > MAX_RESPONSES) s->response_count = MAX_RESPONSES;
        int qs = object_value(data, t, count, si, "questions");
        if (qs < 0) continue;
        for (int qi = qs + 1; qi < count && t[qi].parent == qs && s->question_count < MAX_QUESTIONS; qi = token_after(t, qi, count)) {
            if (data[t[qi].start] != '{') continue;
            Question *q = &s->questions[s->question_count++];
            v = object_value(data, t, count, qi, "title"); if (v >= 0) token_wide(data, t, v, q->title, TEXT_LEN);
            v = object_value(data, t, count, qi, "description"); if (v >= 0) token_wide(data, t, v, q->description, TEXT_LEN);
            v = object_value(data, t, count, qi, "options"); if (v >= 0) token_wide(data, t, v, q->options, OPTIONS_LEN);
            v = object_value(data, t, count, qi, "image"); if (v >= 0) token_wide(data, t, v, q->image_path, MAX_PATH);
            v = object_value(data, t, count, qi, "imageName"); if (v >= 0) token_wide(data, t, v, q->image_name, TEXT_LEN);
            q->type = token_int(data, t, count, qi, "type", 0);
            if (q->type < 0 || q->type >= Q_TYPES) q->type = 0;
            q->required = token_int(data, t, count, qi, "required", 0) != 0;
            int ans = object_value(data, t, count, qi, "answers");
            if (ans >= 0) {
                int ri = 0;
                for (int ai = ans + 1; ai < count && t[ai].parent == ans && ri < MAX_RESPONSES; ai = token_after(t, ai, count))
                    token_wide(data, t, ai, q->answers[ri++], TEXT_LEN);
            }
        }
    }
    memcpy(surveys, loaded, (size_t)loaded_count * sizeof(Survey));
    survey_count = loaded_count;
    data_load_error = 0;
    free(loaded);
    free(t);
    return 1;
}

static void write_json_string(FILE *f, const wchar_t *s) {
    fputc('"', f);
    for (; *s; ++s) {
        unsigned c = (unsigned)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc((int)c, f); }
        else if (c == '\n') fputs("\\n", f);
        else if (c == '\r') fputs("\\r", f);
        else if (c == '\t') fputs("\\t", f);
        else if (c == '<' || c == '>' || c == '&') fprintf(f, "\\u%04x", c);
        else if (c < 32 || c > 126) fprintf(f, "\\u%04x", c & 0xffff);
        else fputc((int)c, f);
    }
    fputc('"', f);
}

static void write_survey_object(FILE *f, const Survey *s, int include_responses) {
    fputs("{\"title\":", f);
    write_json_string(f, s->title);
    fputs(",\"description\":", f);
    write_json_string(f, s->description);
    fprintf(f, ",\"responseCount\":%d,\"questions\":[", include_responses ? s->response_count : 0);
    for (int j = 0; j < s->question_count; ++j) {
        const Question *q = &s->questions[j];
        if (j) fputc(',', f);
        fputs("{\"title\":", f);
        write_json_string(f, q->title);
        fputs(",\"description\":", f);
        write_json_string(f, q->description);
        fputs(",\"options\":", f);
        write_json_string(f, q->options);
        fputs(",\"image\":", f);
        write_json_string(f, q->image_path);
        fputs(",\"imageName\":", f);
        write_json_string(f, q->image_name);
        fprintf(f, ",\"type\":%d,\"required\":%d,\"answers\":[", q->type, q->required);
        for (int r = 0; include_responses && r < s->response_count && r < MAX_RESPONSES; ++r) {
            if (r) fputc(',', f);
            write_json_string(f, q->answers[r]);
        }
        fputs("]}", f);
    }
    fputs("]}", f);
}

static void write_survey_json(FILE *f, const Survey *s) {
    fputs("{\"version\":1,\"surveys\":[", f);
    write_survey_object(f, s, 0);
    fputs("]}\n", f);
}

static int write_survey_json_file(const wchar_t *path, const Survey *s) {
    FILE *f = _wfopen(path, L"wb");
    if (!f) return 0;
    write_survey_json(f, s);
    int failed = ferror(f) != 0;
    if (fclose(f) != 0) failed = 1;
    return !failed;
}

static int write_base64_file(FILE *output, const wchar_t *path) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    FILE *input = _wfopen(path, L"rb");
    if (!input) return 0;
    if (fseek(input, 0, SEEK_END) != 0) { fclose(input); return 0; }
    long length = ftell(input);
    if (length <= 0 || length > 32 * 1024 * 1024 || fseek(input, 0, SEEK_SET) != 0) {
        fclose(input);
        return 0;
    }
    unsigned char *data = (unsigned char *)malloc((size_t)length);
    if (!data) { fclose(input); return 0; }
    size_t read = fread(data, 1, (size_t)length, input);
    int failed = read != (size_t)length || ferror(input);
    if (fclose(input) != 0) failed = 1;
    if (!failed) {
        for (size_t i = 0; i < read; i += 3) {
            unsigned a = data[i];
            unsigned b = i + 1 < read ? data[i + 1] : 0;
            unsigned c = i + 2 < read ? data[i + 2] : 0;
            fputc(alphabet[a >> 2], output);
            fputc(alphabet[((a & 3) << 4) | (b >> 4)], output);
            fputc(i + 1 < read ? alphabet[((b & 15) << 2) | (c >> 6)] : '=', output);
            fputc(i + 2 < read ? alphabet[c & 63] : '=', output);
        }
        if (ferror(output)) failed = 1;
    }
    free(data);
    return !failed;
}

static int write_html_escaped_file(FILE *output, const wchar_t *path) {
    FILE *input = _wfopen(path, L"rb");
    if (!input) return 0;
    int value;
    while ((value = fgetc(input)) != EOF) {
        if (value == '&') fputs("&amp;", output);
        else if (value == '<') fputs("&lt;", output);
        else if (value == '>') fputs("&gt;", output);
        else fputc(value, output);
    }
    int failed = ferror(input) || ferror(output);
    if (fclose(input) != 0) failed = 1;
    return !failed;
}

static int save_data(void) {
    if (data_load_error) return 0;
    FILE *f = _wfopen(data_path[0] ? data_path : DATA_FILE, L"wb");
    if (!f) return 0;
    fputs("{\"version\":1,\"surveys\":[", f);
    for (int i = 0; i < survey_count; ++i) {
        if (i) fputc(',', f);
        write_survey_object(f, &surveys[i], 1);
    }
    fputs("]}\n", f);
    int failed = ferror(f) != 0;
    if (fclose(f) != 0) failed = 1;
    return !failed;
}

static void load_data(void) {
    FILE *f = _wfopen(data_path[0] ? data_path : DATA_FILE, L"rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    rewind(f);
    if (length <= 0 || length > 16 * 1024 * 1024) { fclose(f); return; }
    char *data = (char *)malloc((size_t)length + 1);
    if (!data) { fclose(f); return; }
    size_t read = fread(data, 1, (size_t)length, f);
    fclose(f);
    data[read] = 0;
    if (!load_json_buffer(data)) data_load_error = 1;
    free(data);
}

static void get_text(int id, wchar_t *out, size_t cap) {
    HWND h = GetDlgItem(main_window, id);
    if (h) GetWindowTextW(h, out, (int)cap);
    else if (cap) out[0] = 0;
}

static void fill_question_list(void) {
    HWND list = GetDlgItem(main_window, ID_QUESTIONS);
    if (!list || selected_survey < 0) return;
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    Survey *s = &surveys[selected_survey];
    for (int i = 0; i < s->question_count; ++i) {
        wchar_t line[TEXT_LEN + 96];
        swprintf(line, sizeof(line) / sizeof(line[0]), L"%02d. [%ls] %ls%ls%ls", i + 1,
            type_names[s->questions[i].type >= 0 && s->questions[i].type < Q_TYPES
                ? s->questions[i].type : 0],
            s->questions[i].title[0] ? s->questions[i].title : L"(질문 제목)",
            s->questions[i].image_path[0] ? L"  [이미지]" : L"",
            s->questions[i].required ? L"  *" : L"");
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)line);
    }
}

static void show_choice_hint(int type);

static void refresh_image_controls(void) {
    Survey *s = selected_survey >= 0 ? &surveys[selected_survey] : NULL;
    Question *q = s && selected_question >= 0 && selected_question < s->question_count
        ? &s->questions[selected_question] : NULL;
    HWND pick = GetDlgItem(main_window, ID_IMAGE_PICK);
    HWND clear = GetDlgItem(main_window, ID_IMAGE_CLEAR);
    HWND name = GetDlgItem(main_window, ID_IMAGE_NAME);
    HWND preview = GetDlgItem(main_window, ID_IMAGE_PREVIEW);
    if (pick) EnableWindow(pick, q != NULL);
    if (clear) EnableWindow(clear, q && q->image_path[0]);
    if (name) SetWindowTextW(name, q && q->image_path[0]
        ? (q->image_name[0] ? q->image_name : q->image_path)
        : L"첨부된 이미지 없음");
    if (preview) InvalidateRect(preview, NULL, TRUE);
}

static void load_question_editor(int index) {
    if (selected_survey < 0) return;
    Survey *s = &surveys[selected_survey];
    if (index < 0 || index >= s->question_count) return;
    selected_question = index;
    Question *q = &s->questions[index];
    SetWindowTextW(GetDlgItem(main_window, ID_PROMPT), q->title);
    SetWindowTextW(GetDlgItem(main_window, ID_QDESC), q->description);
    SetWindowTextW(GetDlgItem(main_window, ID_OPTIONS), q->options);
    SendMessageW(GetDlgItem(main_window, ID_TYPE), CB_SETCURSEL, q->type, 0);
    SendMessageW(GetDlgItem(main_window, ID_REQUIRED), BM_SETCHECK, q->required ? BST_CHECKED : BST_UNCHECKED, 0);
    show_choice_hint(q->type);
    fill_question_list();
    SendMessageW(GetDlgItem(main_window, ID_QUESTIONS), LB_SETCURSEL, index, 0);
    SetWindowTextW(GetDlgItem(main_window, ID_DESC), s->description);
    refresh_image_controls();
}

static void flush_question_editor(void) {
    if (selected_survey < 0) return;
    Survey *s = &surveys[selected_survey];
    get_text(ID_TITLE, s->title, TEXT_LEN);
    get_text(ID_DESC, s->description, TEXT_LEN);
    if (selected_question < 0 || selected_question >= s->question_count) return;
    Question *q = &s->questions[selected_question];
    get_text(ID_PROMPT, q->title, TEXT_LEN);
    get_text(ID_QDESC, q->description, TEXT_LEN);
    get_text(ID_OPTIONS, q->options, OPTIONS_LEN);
    int type = (int)SendMessageW(GetDlgItem(main_window, ID_TYPE), CB_GETCURSEL, 0, 0);
    q->type = type >= 0 && type < Q_TYPES ? type : 0;
    q->required = SendMessageW(GetDlgItem(main_window, ID_REQUIRED), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void show_home(void);
static void show_editor(void);
static void show_responder(void);
static void export_csv(void);
static int resolve_data_asset_path(const wchar_t *json_path, const wchar_t *asset,
                                   wchar_t *out, size_t capacity);
static int copy_survey_assets(const Survey *survey, const wchar_t *source_json,
                              const wchar_t *target_json);
static int write_html_image_map(FILE *f, const Survey *survey);
static int copy_image_into_data_folder(const wchar_t *source, Question *question);

static void show_home(void) {
    page = HOME;
    clear_controls();
    label(L"UgaSurvey", 32, 22, 400, 40);
    label(L"설문 만들기부터 응답 확인까지, 한 화면에서 간편하게", 32, 65, 760, 28);
    if (!survey_count) {
        label(L"첫 설문, 쉽게 시작하기", 42, 145, 720, 42);
        label(L"추천 양식으로 시작하면 예시 문항을 바로 편집할 수 있어요. 빈 설문도 언제든 만들 수 있습니다.",
            42, 192, 900, 38);
        button(L"추천 기본 양식으로 시작", 42, 252, 290, ID_STARTER);
        button(L"빈 설문으로 시작", 350, 252, 250, ID_ADD);
        button(L"설문 파일 불러오기", 618, 252, 260, ID_IMPORT);
        label(L"처음 사용 순서", 42, 336, 300, 34);
        label(L"1. 제목과 설명을 적어요", 42, 380, 260, 36);
        label(L"2. 문항을 추가해요", 330, 380, 240, 36);
        label(L"3. 미리보기 후 응답을 받아요", 590, 380, 360, 36);
        label(L"설문과 응답은 이 폴더의 ugasurvey.json 파일에 저장됩니다.", 42, 446, 880, 30);
        button(L"사용 안내", 42, 510, 180, ID_GUIDE);
        InvalidateRect(main_window, NULL, TRUE);
        return;
    }

    label(L"내 설문", 32, 132, 500, 32);
    add_control(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | WS_BORDER | WS_TABSTOP,
        32, 174, 650, 438, ID_LIST);
    button(L"추천 양식으로 만들기", 718, 174, 250, ID_STARTER);
    button(L"빈 설문 만들기", 718, 222, 250, ID_ADD);
    button(L"선택 설문 편집", 718, 286, 250, ID_EDIT);
    button(L"응답 미리보기 / 입력", 718, 334, 250, ID_RESPOND);
    button(L"응답 결과 보기", 718, 382, 250, ID_RESULTS);
    button(L"JSON 불러오기", 718, 446, 250, ID_IMPORT);
    button(L"설문 HTML / EXE 내보내기", 718, 494, 250, ID_EXPORT_MENU);
    button(L"선택 설문 삭제", 718, 542, 250, ID_DELETE);
    button(L"처음 사용 안내", 32, 646, 200, ID_GUIDE);
    label(L"로컬 저장 · 인터넷 연결 불필요 · 설문 파일은 실행 파일 옆에 보관됩니다.",
        248, 650, 700, 30);
    HWND list = GetDlgItem(main_window, ID_LIST);
    SendMessageW(list, LB_SETHORIZONTALEXTENT, 900, 0);
    for (int i = 0; i < survey_count; ++i) {
        wchar_t line[TEXT_LEN + 80];
        swprintf(line, sizeof(line) / sizeof(line[0]), L"%ls     |     문항 %d개     |     응답 %d개",
            surveys[i].title[0] ? surveys[i].title : L"(제목 없음)",
            surveys[i].question_count, surveys[i].response_count);
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)line);
    }
    InvalidateRect(main_window, NULL, TRUE);
}

static void show_editor(void) {
    if (selected_survey < 0) return;
    page = EDITOR;
    selected_question = -1;
    clear_controls();
    label(L"설문 만들기", 28, 20, 400, 38);
    label(L"1. 설문 정보를 적고  2. 문항을 추가한 뒤  3. 미리보기로 확인하세요", 28, 62, 850, 28);
    label(L"1. 설문 제목", 28, 116, 180, 24);
    add_control(L"EDIT", surveys[selected_survey].title, WS_BORDER | ES_AUTOHSCROLL, 28, 142, 320, 34, ID_TITLE);
    label(L"2. 설문 설명", 28, 188, 180, 24);
    add_control(L"EDIT", surveys[selected_survey].description, WS_BORDER | ES_AUTOHSCROLL, 28, 214, 320, 34, ID_DESC);
    label(L"3. 문항 목록 · 순서대로 응답합니다", 28, 270, 330, 24);
    add_control(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER, 28, 298, 330, 220, ID_QUESTIONS);
    button(L"+ 문항 추가", 28, 530, 150, ID_ADD);
    button(L"복제", 188, 530, 82, ID_DUPLICATE);
    button(L"삭제", 278, 530, 80, ID_DELETE);
    button(L"위로", 28, 574, 100, ID_UP);
    button(L"아래로", 138, 574, 100, ID_DOWN);
    label(L"4. 문항 제목", 390, 116, 300, 24);
    add_control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 390, 142, 540, 34, ID_PROMPT);
    label(L"5. 질문 설명 (선택)", 390, 184, 300, 24);
    add_control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 390, 210, 540, 32, ID_QDESC);
    label(L"6. 문항 유형", 390, 254, 200, 24);
    HWND combo = add_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 390, 282, 300, 250, ID_TYPE);
    for (int i = 0; i < Q_TYPES; ++i) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)type_names[i]);
    label(L"7. 선택지 또는 설정 (한 줄에 하나씩)", 390, 326, 460, 24);
    add_control(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        390, 354, 540, 150, ID_OPTIONS);
    add_control(L"BUTTON", L"필수 응답", BS_AUTOCHECKBOX | WS_TABSTOP, 390, 512, 180, 30, ID_REQUIRED);
    add_control(L"STATIC", L"", SS_LEFT, 390, 544, 350, 28, ID_TYPE_HINT);
    label(L"8. 질문 이미지 (선택)", 390, 574, 260, 24);
    button(L"이미지 첨부", 390, 600, 130, ID_IMAGE_PICK);
    button(L"이미지 제거", 530, 600, 110, ID_IMAGE_CLEAR);
    add_control(L"STATIC", L"첨부된 이미지 없음", SS_LEFT | SS_NOPREFIX, 390, 632, 340, 26, ID_IMAGE_NAME);
    add_control(L"STATIC", L"", SS_OWNERDRAW | WS_BORDER, 748, 532, 200, 116, ID_IMAGE_PREVIEW);
    button(L"저장", 430, 660, 94, ID_SAVE);
    button(L"미리보기", 530, 660, 100, ID_PREVIEW);
    button(L"HTML 저장", 636, 660, 112, ID_EXPORT_HTML);
    button(L"EXE 저장", 754, 660, 112, ID_EXPORT_EXE);
    button(L"목록으로", 872, 660, 100, ID_BACK);
    fill_question_list();
    SetWindowTextW(GetDlgItem(main_window, ID_DESC), surveys[selected_survey].description);
    if (surveys[selected_survey].question_count) load_question_editor(0);
    else {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        show_choice_hint(0);
        refresh_image_controls();
    }
}

static int add_answer_options(HWND control, int combo, const wchar_t *options, int default_count) {
    wchar_t buffer[OPTIONS_LEN];
    copy_text(buffer, OPTIONS_LEN, options);
    int count = 0;
    wchar_t *line = buffer;
    for (wchar_t *p = buffer;; ++p) {
        if (*p == L'\r') *p = L'\n';
        if (*p == L'\n' || *p == 0) {
            wchar_t end = *p;
            *p = 0;
            if (*line) {
                SendMessageW(control, combo ? CB_ADDSTRING : LB_ADDSTRING, 0, (LPARAM)line);
                ++count;
            }
            if (!end) break;
            line = p + 1;
        }
    }
    if (!count && default_count > 0) {
        for (int i = 1; i <= default_count; ++i) {
            wchar_t value[16];
            swprintf(value, sizeof(value) / sizeof(value[0]), L"%d", i);
            SendMessageW(control, combo ? CB_ADDSTRING : LB_ADDSTRING, 0, (LPARAM)value);
        }
        count = default_count;
    }
    return count;
}

static void restore_answer_selection(HWND control, int combo, int multi, const wchar_t *answer) {
    if (!answer[0]) return;
    if (!multi) {
        LRESULT index = SendMessageW(control,
            combo ? CB_FINDSTRINGEXACT : LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)answer);
        if (index != CB_ERR && index != LB_ERR)
            SendMessageW(control, combo ? CB_SETCURSEL : LB_SETCURSEL, index, 0);
        return;
    }
    wchar_t buffer[TEXT_LEN];
    copy_text(buffer, TEXT_LEN, answer);
    wchar_t *line = buffer;
    for (wchar_t *p = buffer;; ++p) {
        if (*p == L'\n' || *p == 0) {
            wchar_t end = *p;
            *p = 0;
            if (*line) {
                LRESULT index = SendMessageW(control, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)line);
                if (index != LB_ERR) SendMessageW(control, LB_SETSEL, TRUE, index);
            }
            if (!end) break;
            line = p + 1;
        }
    }
}

static void create_answer_control(const Question *q) {
    int type = q->type;
    int combo = 0, multi = 0;
    HWND control = NULL;
    DWORD style = WS_BORDER | WS_TABSTOP;
    answer_kind = ANSWER_TEXT;
    switch (type) {
    case Q_CHOICE:
        answer_kind = ANSWER_LIST;
        control = add_control(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
            28, 418, 850, 120, ID_ANSWER);
        break;
    case Q_CHECKBOX:
    case Q_GRID_CHECK:
    case Q_IMAGE:
        answer_kind = ANSWER_MULTI;
        multi = 1;
        control = add_control(L"LISTBOX", L"", LBS_EXTENDEDSEL | WS_VSCROLL | WS_BORDER | WS_TABSTOP,
            28, 418, 850, 120, ID_ANSWER);
        break;
    case Q_DROPDOWN:
    case Q_SCALE:
    case Q_RATING:
        answer_kind = ANSWER_COMBO;
        combo = 1;
        control = add_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            28, 418, 420, 180, ID_ANSWER);
        break;
    case Q_DATE:
        answer_kind = ANSWER_DATE;
        control = add_control(DATETIMEPICK_CLASSW, L"", DTS_SHORTDATEFORMAT | WS_TABSTOP,
            28, 418, 250, 32, ID_ANSWER);
        break;
    case Q_TIME:
        answer_kind = ANSWER_TIME;
        control = add_control(DATETIMEPICK_CLASSW, L"", DTS_TIMEFORMAT | WS_TABSTOP,
            28, 418, 250, 32, ID_ANSWER);
        break;
    case Q_LONG:
    case Q_RANK:
    case Q_GRID:
        answer_kind = ANSWER_TEXT;
        style |= ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL;
        control = add_control(L"EDIT", L"", style, 28, 418, 850, 120, ID_ANSWER);
        break;
    default:
        answer_kind = ANSWER_TEXT;
        style |= ES_AUTOHSCROLL;
        if (type == Q_NUMBER) style |= ES_NUMBER;
        control = add_control(L"EDIT", L"", style, 28, 418, 850, 40, ID_ANSWER);
        break;
    }
    if (!control) return;
    if (combo || answer_kind == ANSWER_LIST || multi) {
        add_answer_options(control, combo, q->options, (type == Q_SCALE || type == Q_RATING) ? 5 : 0);
        restore_answer_selection(control, combo, multi, draft[response_index]);
    } else if (answer_kind == ANSWER_TEXT) {
        SetWindowTextW(control, draft[response_index]);
    } else if ((answer_kind == ANSWER_DATE || answer_kind == ANSWER_TIME) && draft[response_index][0]) {
        SYSTEMTIME saved = {0};
        int first = 0, second = 0, third = 0;
        int parsed = answer_kind == ANSWER_DATE
            ? swscanf(draft[response_index], L"%d-%d-%d", &first, &second, &third)
            : swscanf(draft[response_index], L"%d:%d", &first, &second);
        if (answer_kind == ANSWER_DATE && parsed == 3 && first >= 1601 && first <= 9999 &&
            second >= 1 && second <= 12 && third >= 1 && third <= 31) {
            saved.wYear = (WORD)first; saved.wMonth = (WORD)second; saved.wDay = (WORD)third;
        } else if (answer_kind == ANSWER_TIME && parsed == 2 && first >= 0 && first <= 23 &&
            second >= 0 && second <= 59) {
            GetLocalTime(&saved);
            saved.wHour = (WORD)first; saved.wMinute = (WORD)second;
        }
        if (saved.wYear) SendMessageW(control, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&saved);
    }
}

static void capture_current_answer(void) {
    if (page != RESPONDER || selected_survey < 0) return;
    Survey *s = &surveys[selected_survey];
    if (response_index < 0 || response_index >= s->question_count) return;
    HWND control = GetDlgItem(main_window, ID_ANSWER);
    if (!control) return;
    wchar_t result[TEXT_LEN] = L"";
    if (answer_kind == ANSWER_COMBO) {
        LRESULT index = SendMessageW(control, CB_GETCURSEL, 0, 0);
        if (index != CB_ERR) SendMessageW(control, CB_GETLBTEXT, index, (LPARAM)result);
    } else if (answer_kind == ANSWER_LIST) {
        LRESULT index = SendMessageW(control, LB_GETCURSEL, 0, 0);
        if (index != LB_ERR) SendMessageW(control, LB_GETTEXT, index, (LPARAM)result);
    } else if (answer_kind == ANSWER_MULTI) {
        int indices[64];
        LRESULT count = SendMessageW(control, LB_GETSELCOUNT, 0, 0);
        if (count > 64) count = 64;
        if (count > 0 && SendMessageW(control, LB_GETSELITEMS, count, (LPARAM)indices) != LB_ERR) {
            size_t used = 0;
            for (int i = 0; i < count; ++i) {
                wchar_t value[TEXT_LEN];
                if (SendMessageW(control, LB_GETTEXT, indices[i], (LPARAM)value) == LB_ERR) continue;
                if (used) append_text(result, TEXT_LEN, &used, L"\n");
                append_text(result, TEXT_LEN, &used, value);
            }
        }
    } else if (answer_kind == ANSWER_DATE || answer_kind == ANSWER_TIME) {
        SYSTEMTIME time_value;
        if (SendMessageW(control, DTM_GETSYSTEMTIME, 0, (LPARAM)&time_value) == GDT_VALID) {
            if (answer_kind == ANSWER_DATE)
                swprintf(result, TEXT_LEN, L"%04d-%02d-%02d", time_value.wYear, time_value.wMonth, time_value.wDay);
            else
                swprintf(result, TEXT_LEN, L"%02d:%02d", time_value.wHour, time_value.wMinute);
        }
    } else {
        GetWindowTextW(control, result, TEXT_LEN);
    }
    copy_text(draft[response_index], TEXT_LEN, result);
}

static void show_responder(void) {
    if (selected_survey < 0) return;
    page = RESPONDER;
    clear_controls();
    Survey *s = &surveys[selected_survey];
    label(preview_mode ? L"미리보기 (응답은 저장되지 않습니다)" : L"설문 응답", 28, 22, 700, 36);
    label(s->title, 28, 70, 800, 36);
    label(s->description, 28, 132, 850, 42);
    wchar_t qlabel[TEXT_LEN + 64];
    if (s->question_count) {
        Question *q = &s->questions[response_index];
        swprintf(qlabel, sizeof(qlabel) / sizeof(qlabel[0]), L"%d / %d    %ls%ls",
            response_index + 1, s->question_count, q->title,
            q->required ? L"  *필수" : L"");
        label(qlabel, 28, 192, 900, 42);
        if (q->description[0]) label(q->description, 28, 238, q->image_path[0] ? 650 : 850, 32);
        if (q->options[0] && (q->type == Q_RANK || q->type == Q_GRID))
            label(q->options, 28, 274, q->image_path[0] ? 650 : 850, 100);
        if (q->image_path[0])
            add_control(L"STATIC", L"", SS_OWNERDRAW | WS_BORDER, 720, 274, 210, 108, ID_IMAGE_PREVIEW);
        label(type_hints[q->type >= 0 && q->type < Q_TYPES ? q->type : 0], 28, 382, 850, 28);
        create_answer_control(q);
    } else {
        label(L"이 설문에는 아직 문항이 없습니다.", 28, 192, 700, 40);
    }
    button(L"이전", 28, 570, 100, ID_PREV);
    button(L"다음", 138, 570, 100, ID_NEXT);
    if (!preview_mode) button(L"응답 제출", 248, 570, 140, ID_SUBMIT);
    button(L"돌아가기", 810, 570, 120, ID_BACK);
}

static int selected_from_list(int id) {
    LRESULT n = SendMessageW(GetDlgItem(main_window, id), LB_GETCURSEL, 0, 0);
    return n == LB_ERR ? -1 : (int)n;
}

static void save_editor(void) {
    flush_question_editor();
    if (!save_data()) MessageBoxW(main_window, L"JSON 파일을 저장하지 못했습니다.", APP_NAME, MB_ICONERROR);
    else {
        fill_question_list();
        if (selected_question >= 0) SendMessageW(GetDlgItem(main_window, ID_QUESTIONS), LB_SETCURSEL, selected_question, 0);
        MessageBoxW(main_window, L"저장했습니다.", APP_NAME, MB_ICONINFORMATION);
    }
}

static void import_json(void) {
    OPENFILENAMEW ofn = {0};
    wchar_t path[MAX_PATH] = L"";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = main_window; ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH; ofn.lpstrFilter = L"JSON 파일 (*.json)\0*.json\0모든 파일\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    FILE *f = _wfopen(path, L"rb");
    if (!f) { MessageBoxW(main_window, L"파일을 열 수 없습니다.", APP_NAME, MB_ICONERROR); return; }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    if (n <= 0 || n > 16 * 1024 * 1024) { fclose(f); MessageBoxW(main_window, L"JSON 파일 크기가 올바르지 않습니다.", APP_NAME, MB_ICONERROR); return; }
    char *data = (char *)malloc((size_t)n + 1);
    if (!data) { fclose(f); return; }
    size_t got = fread(data, 1, (size_t)n, f); fclose(f); data[got] = 0;
    int imported = load_json_buffer(data);
    free(data);
    if (!imported) {
        MessageBoxW(main_window, L"올바른 UgaSurvey JSON 파일이 아닙니다. 기존 데이터는 유지했습니다.",
            APP_NAME, MB_ICONERROR);
        return;
    }
    int assets_ok = 1;
    for (int i = 0; i < survey_count; ++i) {
        Survey relative_assets = surveys[i];
        for (int q = 0; q < surveys[i].question_count; ++q) {
            Question *question = &surveys[i].questions[q];
            if ((question->image_path[0] && question->image_path[1] == L':') ||
                question->image_path[0] == L'\\' || question->image_path[0] == L'/') {
                wchar_t original_name[TEXT_LEN];
                copy_text(original_name, TEXT_LEN, question->image_name);
                if (!copy_image_into_data_folder(question->image_path, question)) {
                    assets_ok = 0;
                    break;
                }
                if (original_name[0]) copy_text(question->image_name, TEXT_LEN, original_name);
                relative_assets.questions[q].image_path[0] = 0;
            }
        }
        if (!assets_ok || !copy_survey_assets(&relative_assets, path, data_path)) {
            assets_ok = 0;
            break;
        }
    }
    if (!save_data()) MessageBoxW(main_window, L"가져온 데이터를 기본 저장 파일에 저장하지 못했습니다.",
        APP_NAME, MB_ICONERROR);
    if (!assets_ok) MessageBoxW(main_window,
        L"설문 데이터는 가져왔지만 일부 이미지 파일을 찾거나 복사하지 못했습니다. JSON과 함께 제공된 ugasurvey-assets 폴더가 있는지 확인하세요.",
        APP_NAME, MB_ICONWARNING);
    show_home();
}

static void export_json(void) {
    OPENFILENAMEW ofn = {0};
    wchar_t path[MAX_PATH] = L"ugasurvey.json";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = main_window; ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH; ofn.lpstrFilter = L"JSON 파일 (*.json)\0*.json\0";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;
    if (!save_data()) {
        MessageBoxW(main_window, L"JSON 데이터를 준비하지 못했습니다.", APP_NAME, MB_ICONERROR);
        return;
    }
    wchar_t full_path[MAX_PATH];
    DWORD full_length = GetFullPathNameW(path, MAX_PATH, full_path, NULL);
    if (full_length > 0 && full_length < MAX_PATH && _wcsicmp(full_path, data_path) == 0) {
        MessageBoxW(main_window, L"JSON으로 내보냈습니다.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    FILE *source = _wfopen(data_path[0] ? data_path : DATA_FILE, L"rb");
    FILE *target = _wfopen(path, L"wb");
    if (!source || !target) {
        if (source) fclose(source);
        if (target) fclose(target);
        MessageBoxW(main_window, L"JSON 파일을 내보내지 못했습니다.", APP_NAME, MB_ICONERROR);
        return;
    }
    char buffer[8192];
    size_t count;
    int failed = 0;
    while ((count = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        if (fwrite(buffer, 1, count, target) != count) { failed = 1; break; }
    }
    if (ferror(source)) failed = 1;
    if (fclose(source) != 0) failed = 1;
    if (fclose(target) != 0) failed = 1;
    if (!failed) {
        for (int i = 0; i < survey_count; ++i) {
            if (!copy_survey_assets(&surveys[i], data_path, path)) {
                failed = 1;
                break;
            }
        }
    }
    MessageBoxW(main_window, failed ? L"JSON 파일 내보내기에 실패했습니다." : L"JSON으로 내보냈습니다.",
        APP_NAME, failed ? MB_ICONERROR : MB_ICONINFORMATION);
}

static int write_html_document(FILE *f, const Survey *survey) {
    fputs(
        "<!doctype html>\n<html lang=\"ko\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>UgaSurvey 응답</title><style>"
        "@font-face{font-family:Pretendard;src:url(data:font/otf;base64,",
        f);
    if (!write_base64_file(f, font_path)) return 0;
    fputs(
        ") format('opentype');font-weight:400;font-display:swap}"
        "*{box-sizing:border-box}body{margin:0;background:#f2f5fa;color:#17233b;"
        "font:16px/1.6 Pretendard,sans-serif}.wrap{max-width:760px;margin:40px auto;"
        "padding:24px}.card{background:#fff;border:1px solid #dce3ef;border-radius:16px;"
        "padding:28px;box-shadow:0 8px 28px #14233b12}h1{margin-top:0}fieldset{border:1px solid #dce3ef;"
        "border-radius:12px;margin:18px 0;padding:16px}legend{font-weight:700;padding:0 6px}"
        "label.option{display:block;margin:8px 0}img.question-image{display:block;max-width:100%;"
        "max-height:360px;object-fit:contain;margin:12px 0;border-radius:8px}"
        "input[type=text],input[type=number],input[type=date],"
        "input[type=time],textarea,select{width:100%;padding:11px;border:1px solid #aebbd0;"
        "border-radius:8px;font:inherit}textarea{min-height:110px}button{background:#1255c7;color:white;"
        "border:0;border-radius:8px;padding:12px 20px;font:inherit;cursor:pointer}button:hover{background:#0b419f}"
        ".note{color:#56657b;font-size:14px}.error{color:#b42318}</style></head><body>"
        "<main class=\"wrap\"><section class=\"card\"><h1 id=\"title\"></h1><p id=\"description\"></p>"
        "<form id=\"form\"></form><p class=\"note\">이 파일은 오프라인 응답용입니다. 제출 결과는 이 기기의 브라우저에서 "
        "JSON 파일로 저장되며, 제작자에게 자동 전송되지 않습니다.</p><div id=\"done\" hidden>"
        "<h2>응답이 저장되었습니다.</h2><p>다운로드된 응답 JSON 파일을 설문 제작자에게 전달해 주세요.</p>"
        "</div></section></main><script id=\"survey-data\" type=\"application/json\">",
        f);
    write_survey_json(f, survey);
    fputs(
        "</script>",
        f);
    if (!write_html_image_map(f, survey)) return 0;
    fputs("<script>(function(){'use strict';"
        "const s=JSON.parse(document.getElementById('survey-data').textContent).surveys[0];"
        "const images=JSON.parse(document.getElementById('survey-images').textContent);"
        "const form=document.getElementById('form');"
        "document.getElementById('title').textContent=s.title;"
        "document.getElementById('description').textContent=s.description;"
        "function optionLines(q){return q.options.split(/\\r?\\n/).map(x=>x.trim()).filter(Boolean)}"
        "s.questions.forEach((q,i)=>{const box=document.createElement('fieldset');"
        "box.dataset.index=i;const legend=document.createElement('legend');"
        "legend.textContent=(i+1)+'. '+q.title+(q.required?' *':'');box.appendChild(legend);"
        "if(images[i]){const image=document.createElement('img');image.className='question-image';"
        "image.src=images[i];image.alt=q.imageName||q.title;box.appendChild(image)}"
        "if(q.description){const d=document.createElement('p');d.textContent=q.description;box.appendChild(d)}"
        "const opts=optionLines(q);let ctl;"
        "if(q.type===2||q.type===3||q.type===12||q.type===13){"
        "opts.forEach((v,n)=>{const lab=document.createElement('label');lab.className='option';"
        "const input=document.createElement('input');input.type=q.type===2?'radio':'checkbox';"
        "input.name='q'+i;input.value=v;if(q.type===2)input.required=!!q.required;"
        "lab.appendChild(input);lab.appendChild(document.createTextNode(' '+v));box.appendChild(lab)})"
        "}else if(q.type===4||q.type===5||q.type===6){ctl=document.createElement('select');"
        "const blank=document.createElement('option');blank.value='';blank.textContent='선택하세요';ctl.appendChild(blank);"
        "const values=opts.length?opts:(q.type===4?[]:['1','2','3','4','5']);"
        "values.forEach(v=>{const o=document.createElement('option');o.value=v;o.textContent=v;ctl.appendChild(o)});"
        "ctl.name='q'+i;ctl.required=!!q.required;box.appendChild(ctl)"
        "}else{ctl=document.createElement(q.type===1||q.type===10||q.type===11?'textarea':'input');"
        "if(ctl.tagName==='INPUT'){ctl.type=q.type===7?'date':q.type===8?'time':q.type===9?'number':'text'}"
        "ctl.name='q'+i;ctl.required=!!q.required;box.appendChild(ctl)}"
        "form.appendChild(box)});"
        "const submit=document.createElement('button');submit.type='submit';submit.textContent='응답 제출';form.appendChild(submit);"
        "form.addEventListener('submit',e=>{e.preventDefault();const answers=[];"
        "for(const box of form.querySelectorAll('fieldset')){const i=Number(box.dataset.index),q=s.questions[i];"
        "const inputs=box.querySelectorAll('input,select,textarea');"
        "const chosen=Array.from(box.querySelectorAll('input:checked')).map(x=>x.value);"
        "let value=q.type===2?(chosen[0]||''):(q.type===3||q.type===12||q.type===13?chosen:"
        "(inputs[0]?inputs[0].value:''));"
        "const empty=Array.isArray(value)?value.length===0:!String(value).trim();"
        "let error=box.querySelector('.error');if(error)error.remove();"
        "if(q.required&&empty){error=document.createElement('p');error.className='error';"
        "error.textContent='필수 항목에 답변해 주세요.';box.appendChild(error);"
        "const focus=box.querySelector('input,select,textarea');if(focus)focus.focus();return}"
        "answers.push({question:q.title,answer:value})}"
        "const result={survey:s.title,submittedAt:new Date().toISOString(),answers:answers};"
        "const blob=new Blob([JSON.stringify(result,null,2)],{type:'application/json'});"
        "const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;"
        "a.download='ugasurvey-response.json';document.body.appendChild(a);a.click();a.remove();"
        "setTimeout(()=>URL.revokeObjectURL(url),1000);form.hidden=true;"
        "document.getElementById('done').hidden=false})})();</script>"
        "<details class=\"note\"><summary>Pretendard 글꼴 라이선스</summary><pre>",
        f);
    if (!write_html_escaped_file(f, font_license_path)) return 0;
    fputs("</pre></details></body></html>\n", f);
    return ferror(f) == 0;
}

static void export_html(void) {
    if (selected_survey < 0 || !surveys[selected_survey].question_count) {
        MessageBoxW(main_window, L"먼저 문항이 하나 이상 있는 설문을 선택하세요.", APP_NAME, MB_ICONWARNING);
        return;
    }
    OPENFILENAMEW ofn = {0};
    wchar_t path[MAX_PATH] = L"survey.html";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = main_window;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"HTML 파일 (*.html)\0*.html\0";
    ofn.lpstrDefExt = L"html";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    FILE *f = _wfopen(path, L"wb");
    if (!f) {
        MessageBoxW(main_window, L"HTML 파일을 만들 수 없습니다. 저장 위치의 권한을 확인하세요.",
            APP_NAME, MB_ICONERROR);
        return;
    }
    fputs("\xEF\xBB\xBF", f);
    int failed = !write_html_document(f, &surveys[selected_survey]);
    if (fclose(f) != 0) failed = 1;
    if (failed) DeleteFileW(path);
    MessageBoxW(main_window, failed ? L"HTML 파일을 완전히 저장하지 못했습니다. 글꼴과 첨부 이미지 파일, 저장 위치를 확인하세요."
                                    : L"Pretendard가 포함된 오프라인 응답용 HTML을 내보냈습니다. 응답은 브라우저에서 JSON으로 다운로드됩니다.",
        APP_NAME, failed ? MB_ICONERROR : MB_ICONINFORMATION);
}

static int change_extension(const wchar_t *path, const wchar_t *extension,
                            wchar_t *out, size_t capacity) {
    size_t length = wcslen(path);
    const wchar_t *slash = wcsrchr(path, L'\\');
    const wchar_t *dot = wcsrchr(path, L'.');
    if (dot && (!slash || dot > slash)) length = (size_t)(dot - path);
    if (length + wcslen(extension) >= capacity) return 0;
    wmemcpy(out, path, length);
    wcscpy(out + length, extension);
    return 1;
}

static int make_adjacent_path(const wchar_t *base_path, const wchar_t *name,
                              wchar_t *out, size_t capacity) {
    const wchar_t *slash = wcsrchr(base_path, L'\\');
    size_t directory_length = slash ? (size_t)(slash + 1 - base_path) : 0;
    size_t name_length = wcslen(name);
    if (directory_length + name_length >= capacity) return 0;
    if (directory_length) wmemcpy(out, base_path, directory_length);
    wcscpy(out + directory_length, name);
    return 1;
}

static int resolve_data_asset_path(const wchar_t *json_path, const wchar_t *asset,
                                   wchar_t *out, size_t capacity) {
    if (!asset || !asset[0] || !capacity) return 0;
    if ((asset[0] && asset[1] == L':') || asset[0] == L'\\' || asset[0] == L'/') {
        if (wcslen(asset) >= capacity) return 0;
        copy_text(out, capacity, asset);
        return 1;
    }
    const wchar_t *slash = wcsrchr(json_path, L'\\');
    const wchar_t *forward_slash = wcsrchr(json_path, L'/');
    if (!slash || (forward_slash && forward_slash > slash)) slash = forward_slash;
    size_t directory_length = slash ? (size_t)(slash + 1 - json_path) : 0;
    size_t asset_length = wcslen(asset);
    if (directory_length + asset_length >= capacity) return 0;
    if (directory_length) wmemcpy(out, json_path, directory_length);
    wcscpy(out + directory_length, asset);
    return 1;
}

static int ensure_asset_directory(const wchar_t *json_path, wchar_t *out, size_t capacity) {
    if (!make_adjacent_path(json_path, L"ugasurvey-assets", out, capacity)) return 0;
    if (!CreateDirectoryW(out, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    DWORD attributes = GetFileAttributesW(out);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static int copy_survey_assets(const Survey *survey, const wchar_t *source_json,
                              const wchar_t *target_json) {
    for (int i = 0; i < survey->question_count; ++i) {
        const Question *q = &survey->questions[i];
        if (!q->image_path[0]) continue;
        wchar_t source[MAX_PATH], target[MAX_PATH], source_full[MAX_PATH], target_full[MAX_PATH];
        if (!resolve_data_asset_path(source_json, q->image_path, source, MAX_PATH) ||
            !resolve_data_asset_path(target_json, q->image_path, target, MAX_PATH))
            return 0;
        DWORD source_length = GetFullPathNameW(source, MAX_PATH, source_full, NULL);
        DWORD target_length = GetFullPathNameW(target, MAX_PATH, target_full, NULL);
        if (!source_length || source_length >= MAX_PATH || !target_length || target_length >= MAX_PATH)
            return 0;
        if (GetFileAttributesW(source_full) == INVALID_FILE_ATTRIBUTES) return 0;
        if (_wcsicmp(source_full, target_full) == 0) continue;
        wchar_t directory[MAX_PATH];
        if (!ensure_asset_directory(target_json, directory, MAX_PATH)) return 0;
        if (!CopyFileW(source_full, target_full, FALSE)) return 0;
    }
    return 1;
}

static int image_mime_type(const wchar_t *path, const char **mime) {
    const wchar_t *dot = wcsrchr(path, L'.');
    if (!dot) return 0;
    if (_wcsicmp(dot, L".png") == 0) *mime = "image/png";
    else if (_wcsicmp(dot, L".jpg") == 0 || _wcsicmp(dot, L".jpeg") == 0) *mime = "image/jpeg";
    else if (_wcsicmp(dot, L".gif") == 0) *mime = "image/gif";
    else if (_wcsicmp(dot, L".bmp") == 0) *mime = "image/bmp";
    else if (_wcsicmp(dot, L".tif") == 0 || _wcsicmp(dot, L".tiff") == 0) *mime = "image/tiff";
    else return 0;
    return 1;
}

static int copy_image_into_data_folder(const wchar_t *source, Question *question) {
    const wchar_t *dot = wcsrchr(source, L'.');
    const wchar_t *slash = wcsrchr(source, L'\\');
    if (!dot || (slash && dot < slash) || wcslen(dot) >= 12) return 0;
    const char *mime;
    if (!image_mime_type(source, &mime)) return 0;
    (void)mime;
    WIN32_FILE_ATTRIBUTE_DATA file_info;
    if (!GetFileAttributesExW(source, GetFileExInfoStandard, &file_info)) return 0;
    ULARGE_INTEGER file_size;
    file_size.HighPart = file_info.nFileSizeHigh;
    file_size.LowPart = file_info.nFileSizeLow;
    if (!file_size.QuadPart || file_size.QuadPart > 10 * 1024 * 1024) return 0;

    wchar_t directory[MAX_PATH];
    if (!ensure_asset_directory(data_path, directory, MAX_PATH)) return 0;
    wchar_t filename[80], relative[MAX_PATH], destination[MAX_PATH];
    int copied = 0;
    for (int attempt = 0; attempt < 32; ++attempt) {
        ++image_file_sequence;
        swprintf(filename, sizeof(filename) / sizeof(filename[0]), L"image_%08lx_%08lx_%u%ls",
            GetTickCount(), GetCurrentProcessId(), image_file_sequence, dot);
        if (swprintf(relative, MAX_PATH, L"ugasurvey-assets\\%ls", filename) < 0 ||
            !resolve_data_asset_path(data_path, relative, destination, MAX_PATH))
            return 0;
        if (CopyFileW(source, destination, TRUE)) { copied = 1; break; }
        if (GetLastError() != ERROR_FILE_EXISTS) return 0;
    }
    if (!copied) return 0;
    copy_text(question->image_path, MAX_PATH, relative);
    const wchar_t *name = slash ? slash + 1 : source;
    copy_text(question->image_name, TEXT_LEN, name);
    return 1;
}

static void pick_question_image(void) {
    if (selected_survey < 0 || selected_question < 0 ||
        selected_question >= surveys[selected_survey].question_count) return;
    flush_question_editor();
    OPENFILENAMEW ofn = {0};
    wchar_t path[MAX_PATH] = L"";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = main_window;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"이미지 파일 (*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.tif;*.tiff)\0"
        L"*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.tif;*.tiff\0모든 파일\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    if (!copy_image_into_data_folder(path, &surveys[selected_survey].questions[selected_question])) {
        MessageBoxW(main_window,
            L"이미지를 첨부하지 못했습니다. PNG, JPG, GIF, BMP, TIF 파일(10MB 이하)인지 확인하고 저장 폴더의 쓰기 권한을 확인하세요.",
            APP_NAME, MB_ICONERROR);
        return;
    }
    fill_question_list();
    SendMessageW(GetDlgItem(main_window, ID_QUESTIONS), LB_SETCURSEL, selected_question, 0);
    refresh_image_controls();
}

static void clear_question_image(void) {
    if (selected_survey < 0 || selected_question < 0 ||
        selected_question >= surveys[selected_survey].question_count) return;
    Question *q = &surveys[selected_survey].questions[selected_question];
    q->image_path[0] = 0;
    q->image_name[0] = 0;
    fill_question_list();
    SendMessageW(GetDlgItem(main_window, ID_QUESTIONS), LB_SETCURSEL, selected_question, 0);
    refresh_image_controls();
}

static int write_html_image_map(FILE *f, const Survey *survey) {
    fputs("<script id=\"survey-images\" type=\"application/json\">{", f);
    int written = 0;
    for (int i = 0; i < survey->question_count; ++i) {
        const Question *q = &survey->questions[i];
        if (!q->image_path[0]) continue;
        wchar_t image_path[MAX_PATH];
        const char *mime;
        if (!resolve_data_asset_path(data_path, q->image_path, image_path, MAX_PATH) ||
            !image_mime_type(image_path, &mime) ||
            GetFileAttributesW(image_path) == INVALID_FILE_ATTRIBUTES)
            return 0;
        if (written++) fputc(',', f);
        fprintf(f, "\"%d\":\"data:%s;base64,", i, mime);
        if (!write_base64_file(f, image_path)) return 0;
        fputc('"', f);
    }
    fputs("}</script>", f);
    return ferror(f) == 0;
}

static void export_exe(void) {
    if (selected_survey < 0 || !surveys[selected_survey].question_count) {
        MessageBoxW(main_window, L"먼저 문항이 하나 이상 있는 설문을 선택하세요.", APP_NAME, MB_ICONWARNING);
        return;
    }
    OPENFILENAMEW ofn = {0};
    wchar_t path[MAX_PATH] = L"UgaSurvey-응답용.exe";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = main_window;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"응답용 실행 파일 (*.exe)\0*.exe\0";
    ofn.lpstrDefExt = L"exe";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    wchar_t json_path[MAX_PATH], output_font[MAX_PATH], output_license[MAX_PATH], module_path[MAX_PATH];
    wchar_t temp_exe[MAX_PATH], temp_json[MAX_PATH], temp_font[MAX_PATH], temp_license[MAX_PATH];
    if (!change_extension(path, L".json", json_path, MAX_PATH) ||
        !make_adjacent_path(path, L"Pretendard-Regular.otf", output_font, MAX_PATH) ||
        !make_adjacent_path(path, L"OFL-Pretendard.txt", output_license, MAX_PATH) ||
        swprintf(temp_exe, MAX_PATH, L"%ls.tmp.%lu.exe", path, GetCurrentProcessId()) < 0 ||
        swprintf(temp_json, MAX_PATH, L"%ls.tmp.%lu.json", json_path, GetCurrentProcessId()) < 0 ||
        swprintf(temp_font, MAX_PATH, L"%ls.tmp.%lu.otf", output_font, GetCurrentProcessId()) < 0 ||
        swprintf(temp_license, MAX_PATH, L"%ls.tmp.%lu.txt", output_license, GetCurrentProcessId()) < 0) {
        MessageBoxW(main_window, L"내보낼 파일 경로가 너무 깁니다.", APP_NAME, MB_ICONERROR);
        return;
    }
    DWORD module_length = GetModuleFileNameW(NULL, module_path, MAX_PATH);
    if (!module_length || module_length >= MAX_PATH) {
        MessageBoxW(main_window, L"현재 실행 파일의 위치를 확인하지 못했습니다.", APP_NAME, MB_ICONERROR);
        return;
    }
    wchar_t full_target[MAX_PATH], full_module[MAX_PATH], full_json[MAX_PATH], full_data[MAX_PATH];
    DWORD target_length = GetFullPathNameW(path, MAX_PATH, full_target, NULL);
    DWORD module_full_length = GetFullPathNameW(module_path, MAX_PATH, full_module, NULL);
    DWORD json_length = GetFullPathNameW(json_path, MAX_PATH, full_json, NULL);
    DWORD data_length = GetFullPathNameW(data_path, MAX_PATH, full_data, NULL);
    if ((target_length > 0 && target_length < MAX_PATH && module_full_length > 0 &&
         module_full_length < MAX_PATH && _wcsicmp(full_target, full_module) == 0) ||
        (json_length > 0 && json_length < MAX_PATH && data_length > 0 &&
         data_length < MAX_PATH && _wcsicmp(full_json, full_data) == 0)) {
        MessageBoxW(main_window, L"현재 실행 파일이나 설문 데이터 파일을 내보내기 대상으로 덮어쓸 수 없습니다.",
            APP_NAME, MB_ICONWARNING);
        return;
    }
    if (GetFileAttributesW(json_path) != INVALID_FILE_ATTRIBUTES &&
        MessageBoxW(main_window, L"같은 이름의 JSON 파일이 이미 있습니다. 설문 템플릿으로 덮어쓸까요?",
            APP_NAME, MB_YESNO | MB_ICONWARNING) != IDYES) return;
    wchar_t full_output_font[MAX_PATH], full_source_font[MAX_PATH];
    wchar_t full_output_license[MAX_PATH], full_source_license[MAX_PATH];
    DWORD output_font_length = GetFullPathNameW(output_font, MAX_PATH, full_output_font, NULL);
    DWORD source_font_length = GetFullPathNameW(font_path, MAX_PATH, full_source_font, NULL);
    DWORD output_license_length = GetFullPathNameW(output_license, MAX_PATH, full_output_license, NULL);
    DWORD source_license_length = GetFullPathNameW(font_license_path, MAX_PATH, full_source_license, NULL);
    if (!output_font_length || output_font_length >= MAX_PATH || !source_font_length ||
        source_font_length >= MAX_PATH || !output_license_length || output_license_length >= MAX_PATH ||
        !source_license_length || source_license_length >= MAX_PATH) {
        MessageBoxW(main_window, L"글꼴 패키지 경로를 확인하지 못했습니다.", APP_NAME, MB_ICONERROR);
        return;
    }
    int copy_font = _wcsicmp(full_output_font, full_source_font) != 0;
    int copy_license = _wcsicmp(full_output_license, full_source_license) != 0;
    if (((copy_font && GetFileAttributesW(output_font) != INVALID_FILE_ATTRIBUTES) ||
         (copy_license && GetFileAttributesW(output_license) != INVALID_FILE_ATTRIBUTES)) &&
        MessageBoxW(main_window, L"대상 폴더에 같은 이름의 Pretendard 글꼴 또는 라이선스 파일이 있습니다. 덮어쓸까요?",
            APP_NAME, MB_YESNO | MB_ICONWARNING) != IDYES) return;
    if (!write_survey_json_file(temp_json, &surveys[selected_survey])) {
        DeleteFileW(temp_json);
        MessageBoxW(main_window, L"배포용 설문 JSON을 저장하지 못했습니다.", APP_NAME, MB_ICONERROR);
        return;
    }
    if (!CopyFileW(module_path, temp_exe, TRUE)) {
        DeleteFileW(temp_json);
        MessageBoxW(main_window, L"실행 파일 복사본을 만들지 못했습니다. 저장 위치를 확인하세요.",
            APP_NAME, MB_ICONERROR);
        return;
    }
    if (!copy_survey_assets(&surveys[selected_survey], data_path, json_path)) {
        DeleteFileW(temp_json);
        DeleteFileW(temp_exe);
        MessageBoxW(main_window, L"설문 이미지 파일을 배포 폴더로 복사하지 못했습니다. 저장 위치를 확인하세요.",
            APP_NAME, MB_ICONERROR);
        return;
    }
    if ((copy_font && !CopyFileW(font_path, temp_font, TRUE)) ||
        (copy_license && !CopyFileW(font_license_path, temp_license, TRUE))) {
        DeleteFileW(temp_json);
        DeleteFileW(temp_exe);
        DeleteFileW(temp_font);
        DeleteFileW(temp_license);
        MessageBoxW(main_window, L"응답용 Pretendard 글꼴이나 라이선스 파일을 복사하지 못했습니다.",
            APP_NAME, MB_ICONERROR);
        return;
    }
    if (!MoveFileExW(temp_json, json_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp_json);
        DeleteFileW(temp_exe);
        DeleteFileW(temp_font);
        DeleteFileW(temp_license);
        MessageBoxW(main_window, L"설문 데이터 파일을 내보내지 못했습니다.", APP_NAME, MB_ICONERROR);
        return;
    }
    if ((copy_font && !MoveFileExW(temp_font, output_font, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) ||
        (copy_license && !MoveFileExW(temp_license, output_license, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))) {
        DeleteFileW(temp_font);
        DeleteFileW(temp_license);
        DeleteFileW(temp_exe);
        MessageBoxW(main_window, L"설문 JSON은 저장했지만 글꼴 파일 묶음을 완성하지 못했습니다.",
            APP_NAME, MB_ICONERROR);
        return;
    }
    if (!MoveFileExW(temp_exe, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp_exe);
        MessageBoxW(main_window, L"설문 JSON과 Pretendard 파일은 저장했지만 실행 파일을 완성하지 못했습니다.",
            APP_NAME, MB_ICONERROR);
        return;
    }
    MessageBoxW(main_window,
        L"응답용 EXE, 같은 이름의 JSON, Pretendard 글꼴과 라이선스 파일을 내보냈습니다. 이미지가 있는 설문은 ugasurvey-assets 폴더도 함께 전달하세요. "
        L"실행 시 응답 화면이 바로 열리고 제출된 응답은 JSON 파일에 저장됩니다.",
        APP_NAME, MB_ICONINFORMATION);
}

static void show_export_menu(HWND owner, HWND anchor) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        MessageBoxW(owner, L"내보내기 메뉴를 열지 못했습니다.", APP_NAME, MB_ICONERROR);
        return;
    }
    AppendMenuW(menu, MF_STRING, ID_EXPORT_HTML, L"응답자용 오프라인 HTML");
    AppendMenuW(menu, MF_STRING, ID_EXPORT_EXE, L"응답용 EXE + JSON");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_EXPORT, L"설문 JSON 백업");
    AppendMenuW(menu, MF_STRING, ID_CSV, L"응답 CSV");
    RECT rect;
    GetWindowRect(anchor, &rect);
    SetForegroundWindow(owner);
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        rect.left, rect.bottom, 0, owner, NULL);
    DestroyMenu(menu);
    if (command == ID_EXPORT_HTML) export_html();
    else if (command == ID_EXPORT_EXE) export_exe();
    else if (command == ID_EXPORT) export_json();
    else if (command == ID_CSV) export_csv();
}

static void write_csv_field(FILE *f, const wchar_t *text) {
    char utf8[TEXT_LEN * 4] = {0};
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
    fputc('"', f);
    for (char *p = utf8; *p; ++p) {
        if (*p == '"') fputs("\"\"", f);
        else fputc(*p, f);
    }
    fputc('"', f);
}

static void export_csv(void) {
    OPENFILENAMEW ofn = {0};
    wchar_t path[MAX_PATH] = L"ugasurvey.csv";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = main_window; ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH; ofn.lpstrFilter = L"CSV 파일 (*.csv)\0*.csv\0";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return;
    Survey *s = &surveys[selected_survey];
    FILE *f = _wfopen(path, L"wb");
    if (!f) { MessageBoxW(main_window, L"CSV 파일을 저장하지 못했습니다.", APP_NAME, MB_ICONERROR); return; }
    fputs("\xEF\xBB\xBF", f);
    write_csv_field(f, L"응답 번호");
    for (int q = 0; q < s->question_count; ++q) {
        fputc(',', f);
        write_csv_field(f, s->questions[q].title);
    }
    fputs("\r\n", f);
    for (int r = 0; r < s->response_count; ++r) {
        fprintf(f, "%d", r + 1);
        for (int q = 0; q < s->question_count; ++q) {
            fputc(',', f);
            write_csv_field(f, s->questions[q].answers[r]);
        }
        fputs("\r\n", f);
    }
    fclose(f);
    MessageBoxW(main_window, L"CSV로 내보냈습니다.", APP_NAME, MB_ICONINFORMATION);
}

static void show_results(void) {
    Survey *s = &surveys[selected_survey];
    wchar_t report[12000] = L"";
    wchar_t line[800];
    size_t used = 0;
    swprintf(line, 800, L"설문: %ls\r\n응답 수: %d\r\n\r\n", s->title, s->response_count);
    append_text(report, sizeof(report) / sizeof(report[0]), &used, line);
    for (int q = 0; q < s->question_count; ++q) {
        swprintf(line, 800, L"%d. %ls\r\n", q + 1, s->questions[q].title);
        append_text(report, sizeof(report) / sizeof(report[0]), &used, line);
        int shown = 0;
        for (int r = 0; r < s->response_count && shown < 6; ++r) {
            if (!s->questions[q].answers[r][0]) continue;
            swprintf(line, 800, L"   - %ls\r\n", s->questions[q].answers[r]);
            append_text(report, sizeof(report) / sizeof(report[0]), &used, line);
            ++shown;
        }
        if (!shown) append_text(report, sizeof(report) / sizeof(report[0]), &used, L"   응답 없음\r\n");
        append_text(report, sizeof(report) / sizeof(report[0]), &used, L"\r\n");
    }
    MessageBoxW(main_window, report, L"응답 결과", MB_OK | MB_ICONINFORMATION);
}

static int create_starter_survey(void) {
    if (survey_count >= MAX_SURVEYS) {
        MessageBoxW(main_window, L"설문은 최대 24개까지 만들 수 있습니다.", APP_NAME, MB_ICONWARNING);
        return 0;
    }
    selected_survey = survey_count++;
    Survey *s = &surveys[selected_survey];
    memset(s, 0, sizeof(*s));
    copy_text(s->title, TEXT_LEN, L"새 설문");
    copy_text(s->description, TEXT_LEN, L"여러분의 소중한 의견을 들려주세요!");
    s->question_count = 3;

    Question *q = &s->questions[0];
    copy_text(q->title, TEXT_LEN, L"가장 선호하는 운영체제는 무엇인가요?");
    copy_text(q->description, TEXT_LEN, L"하나를 선택해 주세요.");
    copy_text(q->options, OPTIONS_LEN, L"Windows\r\nmacOS\r\nLinux\r\n기타 (직접 입력)");
    q->type = Q_CHOICE;
    q->required = 1;

    q = &s->questions[1];
    copy_text(q->title, TEXT_LEN, L"추가 의견을 작성해 주세요.");
    copy_text(q->description, TEXT_LEN, L"더 좋은 서비스가 될 수 있도록 의견을 남겨주세요.");
    q->type = Q_LONG;

    q = &s->questions[2];
    copy_text(q->title, TEXT_LEN, L"이 서비스를 다른 사람에게 추천하시겠어요?");
    copy_text(q->options, OPTIONS_LEN, L"추천해요\r\n잘 모르겠어요\r\n추천하지 않아요");
    q->type = Q_DROPDOWN;

    if (!save_data())
        MessageBoxW(main_window, L"설문을 만들었지만 파일 저장에 실패했습니다. 저장 위치를 확인하세요.",
            APP_NAME, MB_ICONERROR);
    show_editor();
    return 1;
}

static void show_guide(void) {
    MessageBoxW(main_window,
        L"UgaSurvey 시작 안내\r\n\r\n"
        L"1. '추천 양식으로 만들기'를 누르면 예시 문항이 들어 있는 설문이 만들어집니다.\r\n"
        L"   또는 '빈 설문으로 만들기'로 처음부터 시작할 수 있습니다.\r\n\r\n"
        L"2. 설문 편집 화면에서 제목과 설명을 바꾸고, 왼쪽의 '+ 문항 추가'로 문항을 만드세요.\r\n"
        L"   오른쪽에서 질문 제목, 유형, 선택지와 필수 응답을 설정하고 질문 이미지를 첨부할 수 있습니다.\r\n\r\n"
        L"3. '미리보기'로 응답자 화면을 확인하고 '저장하기'를 눌러 보관하세요.\r\n"
        L"   실제 응답은 홈 화면에서 설문을 선택하고 '응답 미리보기 / 입력'을 누르면 됩니다.\r\n\r\n"
        L"설문과 응답은 UgaSurvey.exe 옆의 ugasurvey.json에 저장됩니다. 첨부 이미지는 ugasurvey-assets 폴더에 보관되며, HTML 내보내기에는 이미지가 포함됩니다. JSON 백업과 CSV 응답 내보내기도 홈 화면에서 사용할 수 있습니다.",
        L"처음 사용 안내", MB_OK | MB_ICONINFORMATION);
}

static void show_choice_hint(int type) {
    if (type < 0 || type >= Q_TYPES) return;
    HWND hint = GetDlgItem(main_window, ID_TYPE_HINT);
    SetWindowTextW(hint, type_hints[type]);
    RedrawWindow(hint, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

static void handle_command(int id, int notification) {
    if (page == HOME) {
        if (id == ID_GUIDE) {
            show_guide();
        } else if (id == ID_STARTER) {
            create_starter_survey();
        } else if (id == ID_ADD) {
            if (survey_count >= MAX_SURVEYS) { MessageBoxW(main_window, L"설문은 최대 24개까지 만들 수 있습니다.", APP_NAME, MB_ICONWARNING); return; }
            selected_survey = survey_count++;
            memset(&surveys[selected_survey], 0, sizeof(Survey));
            copy_text(surveys[selected_survey].title, TEXT_LEN, L"새 설문");
            show_editor();
        } else if (id == ID_EXPORT_MENU) {
            selected_survey = selected_from_list(ID_LIST);
            if (selected_survey < 0) {
                MessageBoxW(main_window, L"먼저 설문을 선택하세요.", APP_NAME, MB_ICONWARNING);
                return;
            }
            show_export_menu(main_window, GetDlgItem(main_window, ID_EXPORT_MENU));
        } else if (id == ID_EDIT || id == ID_RESPOND || id == ID_RESULTS || id == ID_DELETE ||
                   id == ID_EXPORT || id == ID_CSV) {
            selected_survey = selected_from_list(ID_LIST);
            if (selected_survey < 0) { MessageBoxW(main_window, L"먼저 설문을 선택하세요.", APP_NAME, MB_ICONWARNING); return; }
            if (id == ID_EDIT) show_editor();
            else if (id == ID_RESPOND) { preview_mode = 0; response_index = 0; memset(draft, 0, sizeof(draft)); show_responder(); }
            else if (id == ID_RESULTS) show_results();
            else if (id == ID_EXPORT) export_json();
            else if (id == ID_CSV) export_csv();
            else if (MessageBoxW(main_window, L"선택한 설문과 응답을 삭제할까요?", APP_NAME, MB_YESNO | MB_ICONWARNING) == IDYES) {
                for (int i = selected_survey; i + 1 < survey_count; ++i) surveys[i] = surveys[i + 1];
                --survey_count;
                if (!save_data()) MessageBoxW(main_window, L"삭제했지만 JSON 파일에 저장하지 못했습니다.",
                    APP_NAME, MB_ICONERROR);
                show_home();
            }
        } else if (id == ID_IMPORT) import_json();
    } else if (page == EDITOR) {
        Survey *s = &surveys[selected_survey];
        if (id == ID_QUESTIONS && notification == LBN_SELCHANGE) {
            int n = selected_from_list(ID_QUESTIONS);
            if (selected_question >= 0) flush_question_editor();
            if (n >= 0) load_question_editor(n);
        } else if (id == ID_TYPE && notification == CBN_SELCHANGE) {
            int n = (int)SendMessageW(GetDlgItem(main_window, ID_TYPE), CB_GETCURSEL, 0, 0);
            show_choice_hint(n);
        } else if (id == ID_IMAGE_PICK) {
            pick_question_image();
        } else if (id == ID_IMAGE_CLEAR) {
            clear_question_image();
        } else if (id == ID_ADD) {
            flush_question_editor();
            if (s->question_count >= MAX_QUESTIONS) { MessageBoxW(main_window, L"문항은 최대 40개까지 추가할 수 있습니다.", APP_NAME, MB_ICONWARNING); return; }
            selected_question = s->question_count++;
            memset(&s->questions[selected_question], 0, sizeof(Question));
            s->questions[selected_question].type = 0;
            fill_question_list(); load_question_editor(selected_question);
        } else if (id == ID_DELETE && selected_question >= 0) {
            flush_question_editor();
            for (int i = selected_question; i + 1 < s->question_count; ++i) s->questions[i] = s->questions[i + 1];
            --s->question_count;
            selected_question = s->question_count ? (selected_question < s->question_count ? selected_question : s->question_count - 1) : -1;
            fill_question_list();
            if (selected_question >= 0) load_question_editor(selected_question);
            else {
                SetWindowTextW(GetDlgItem(main_window, ID_PROMPT), L"");
                SetWindowTextW(GetDlgItem(main_window, ID_OPTIONS), L"");
                SetWindowTextW(GetDlgItem(main_window, ID_QDESC), L"");
                refresh_image_controls();
            }
        } else if (id == ID_DUPLICATE && selected_question >= 0 && s->question_count < MAX_QUESTIONS) {
            flush_question_editor();
            for (int i = s->question_count; i > selected_question + 1; --i) s->questions[i] = s->questions[i - 1];
            s->questions[selected_question + 1] = s->questions[selected_question];
            ++s->question_count; ++selected_question; fill_question_list(); load_question_editor(selected_question);
        } else if ((id == ID_UP || id == ID_DOWN) && selected_question >= 0) {
            flush_question_editor();
            int to = selected_question + (id == ID_UP ? -1 : 1);
            if (to >= 0 && to < s->question_count) {
                Question temp = s->questions[to]; s->questions[to] = s->questions[selected_question];
                s->questions[selected_question] = temp; selected_question = to;
                fill_question_list(); load_question_editor(selected_question);
            }
        } else if (id == ID_SAVE) save_editor();
        else if (id == ID_EXPORT_HTML || id == ID_EXPORT_EXE) {
            flush_question_editor();
            if (!save_data()) {
                MessageBoxW(main_window, L"내보내기 전에 현재 설문을 저장하지 못했습니다.",
                    APP_NAME, MB_ICONERROR);
                return;
            }
            if (id == ID_EXPORT_HTML) export_html();
            else export_exe();
        }
        else if (id == ID_PREVIEW) {
            flush_question_editor();
            if (!save_data()) MessageBoxW(main_window, L"현재 변경사항을 JSON 파일에 저장하지 못했습니다.",
                APP_NAME, MB_ICONERROR);
            preview_mode = 1; response_index = 0;
            memset(draft, 0, sizeof(draft)); show_responder();
        } else if (id == ID_BACK) {
            flush_question_editor();
            if (!save_data()) MessageBoxW(main_window, L"현재 변경사항을 JSON 파일에 저장하지 못했습니다.",
                APP_NAME, MB_ICONERROR);
            show_home();
        }
    } else if (page == RESPONDER) {
        if (id == ID_ANSWER && notification == EN_CHANGE) {
            capture_current_answer();
            return;
        }
        if (selected_survey < 0) return;
        Survey *s = &surveys[selected_survey];
        capture_current_answer();
        if (id == ID_PREV && response_index > 0) { --response_index; show_responder(); }
        else if (id == ID_NEXT && response_index + 1 < s->question_count) { ++response_index; show_responder(); }
        else if (id == ID_SUBMIT && !preview_mode) {
            for (int i = 0; i < s->question_count; ++i) {
                if (s->questions[i].required && !draft[i][0]) {
                    response_index = i; show_responder();
                    MessageBoxW(main_window, L"필수 문항에 답변해 주세요.", APP_NAME, MB_ICONWARNING); return;
                }
            }
            if (MessageBoxW(main_window, L"응답을 제출할까요?", APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                if (s->response_count >= MAX_RESPONSES) { MessageBoxW(main_window, L"응답은 설문당 최대 64개까지 저장할 수 있습니다.", APP_NAME, MB_ICONWARNING); return; }
                int r = s->response_count;
                for (int i = 0; i < s->question_count; ++i) copy_text(s->questions[i].answers[r], TEXT_LEN, draft[i]);
                ++s->response_count;
                if (!save_data()) {
                    --s->response_count;
                    for (int i = 0; i < s->question_count; ++i) s->questions[i].answers[r][0] = 0;
                    MessageBoxW(main_window, L"응답 저장에 실패했습니다. 저장 위치의 쓰기 권한을 확인하고 다시 제출하세요.",
                        APP_NAME, MB_ICONERROR);
                } else {
                    MessageBoxW(main_window, L"응답을 저장했습니다.", APP_NAME, MB_ICONINFORMATION);
                    if (launch_as_respondent) PostMessageW(main_window, WM_CLOSE, 0, 0);
                    else show_home();
                }
            }
        } else if (id == ID_BACK) {
            if (launch_as_respondent) PostMessageW(main_window, WM_CLOSE, 0, 0);
            else if (preview_mode) show_editor(); else show_home();
        }
    }
}

static void fill_wave(HDC dc, LONG width, COLORREF color, const POINT curve[7]) {
    BeginPath(dc);
    MoveToEx(dc, 0, curve[0].y, NULL);
    PolyBezierTo(dc, curve + 1, 6);
    LineTo(dc, width, 190);
    LineTo(dc, 0, 190);
    CloseFigure(dc);
    EndPath(dc);
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    FillPath(dc);
    SelectObject(dc, old_brush);
    DeleteObject(brush);
}

static void fill_card(HDC dc, int left, int top, int right, int bottom) {
    RECT rect = { left, top, right, bottom };
    HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(218, 228, 249));
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 22, 22);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void paint_background(HWND hwnd, HDC dc) {
    RECT client;
    GetClientRect(hwnd, &client);
    RECT body = client;
    body.top = 112;
    HBRUSH body_brush = CreateSolidBrush(RGB(239, 244, 255));
    FillRect(dc, &body, body_brush);
    DeleteObject(body_brush);

    TRIVERTEX vertices[2] = {
        { 0, 0, (COLOR16)(RGB(4, 57, 199) & 0xff) << 8,
          (COLOR16)(RGB(4, 57, 199) >> 8 & 0xff) << 8,
          (COLOR16)(RGB(4, 57, 199) >> 16 & 0xff) << 8, 0 },
        { client.right, 182, (COLOR16)(RGB(35, 122, 255) & 0xff) << 8,
          (COLOR16)(RGB(35, 122, 255) >> 8 & 0xff) << 8,
          (COLOR16)(RGB(35, 122, 255) >> 16 & 0xff) << 8, 0 }
    };
    GRADIENT_RECT gradient = { 0, 1 };
    GradientFill(dc, vertices, 2, &gradient, 1, GRADIENT_FILL_RECT_H);
    POINT upper_wave[7] = {
        { 0, 101 },
        { client.right / 6, 134 }, { client.right / 3, 151 }, { client.right / 2, 160 },
        { client.right * 2 / 3, 166 }, { client.right * 5 / 6, 151 }, { client.right, 100 }
    };
    POINT middle_wave[7] = {
        { 0, 121 },
        { client.right / 6, 161 }, { client.right / 3, 175 }, { client.right / 2, 180 },
        { client.right * 2 / 3, 177 }, { client.right * 5 / 6, 131 }, { client.right, 78 }
    };
    POINT lower_wave[7] = {
        { 0, 140 },
        { client.right / 6, 181 }, { client.right / 3, 190 }, { client.right / 2, 190 },
        { client.right * 2 / 3, 185 }, { client.right * 5 / 6, 138 }, { client.right, 96 }
    };
    fill_wave(dc, client.right, RGB(39, 117, 244), upper_wave);
    fill_wave(dc, client.right, RGB(104, 164, 251), middle_wave);
    fill_wave(dc, client.right, RGB(191, 215, 255), lower_wave);

    if (page == HOME) {
        if (survey_count) {
            fill_card(dc, 20, 152, 692, 676);
            fill_card(dc, 704, 152, 990, 676);
        } else {
            fill_card(dc, 24, 156, client.right - 24, 636);
        }
    } else if (page == EDITOR) {
        fill_card(dc, 20, 146, client.right - 20, client.bottom - 20);
    } else if (page == RESPONDER) {
        fill_card(dc, 20, 150, client.right - 20, 672);
    }
}

static void draw_button(const DRAWITEMSTRUCT *item) {
    RECT rect = item->rcItem;
    int primary = item->CtlID == ID_ADD || item->CtlID == ID_STARTER ||
                  item->CtlID == ID_SAVE || item->CtlID == ID_SUBMIT;
    int pressed = (item->itemState & ODS_SELECTED) != 0;
    int disabled = (item->itemState & ODS_DISABLED) != 0;
    int hovered = hovered_button == (int)item->CtlID;
    COLORREF fill = primary
        ? (pressed ? RGB(8, 63, 193) : RGB(22, 94, 255))
        : (pressed ? RGB(221, 234, 255) : hovered ? RGB(232, 241, 255) : RGB(255, 255, 255));
    COLORREF edge = primary ? fill : RGB(183, 207, 251);
    COLORREF text = disabled ? RGB(145, 153, 170) : (primary ? RGB(255, 255, 255) : RGB(26, 62, 132));
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ old_brush = SelectObject(item->hDC, brush);
    HGDIOBJ old_pen = SelectObject(item->hDC, pen);
    RoundRect(item->hDC, rect.left, rect.top, rect.right, rect.bottom, 14, 14);
    SelectObject(item->hDC, old_brush);
    SelectObject(item->hDC, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);

    wchar_t text_buffer[128];
    GetWindowTextW(item->hwndItem, text_buffer, (int)(sizeof(text_buffer) / sizeof(text_buffer[0])));
    HGDIOBJ old_font = app_font ? SelectObject(item->hDC, app_font) : NULL;
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, text);
    RECT text_rect = rect;
    InflateRect(&text_rect, -8, -3);
    DrawTextW(item->hDC, text_buffer, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (old_font) SelectObject(item->hDC, old_font);
    if (item->itemState & ODS_FOCUS) {
        InflateRect(&text_rect, 2, 1);
        DrawFocusRect(item->hDC, &text_rect);
    }
}

static void draw_image_preview(const DRAWITEMSTRUCT *item) {
    RECT rect = item->rcItem;
    FillRect(item->hDC, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(198, 208, 224));
    HGDIOBJ old_pen = SelectObject(item->hDC, pen);
    HGDIOBJ old_brush = SelectObject(item->hDC, GetStockObject(HOLLOW_BRUSH));
    Rectangle(item->hDC, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(item->hDC, old_pen);
    SelectObject(item->hDC, old_brush);
    DeleteObject(pen);

    const Question *q = NULL;
    if (selected_survey >= 0) {
        Survey *s = &surveys[selected_survey];
        int index = page == RESPONDER ? response_index : selected_question;
        if (index >= 0 && index < s->question_count) q = &s->questions[index];
    }
    int displayed = 0;
    if (q && q->image_path[0] && gdiplus_token) {
        wchar_t path[MAX_PATH];
        GpImage *image = NULL;
        if (resolve_data_asset_path(data_path, q->image_path, path, MAX_PATH) &&
            GdipLoadImageFromFile(path, &image) == Ok && image) {
            UINT width = 0, height = 0;
            if (GdipGetImageWidth(image, &width) == Ok && GdipGetImageHeight(image, &height) == Ok &&
                width && height) {
                int available_width = rect.right - rect.left - 12;
                int available_height = rect.bottom - rect.top - 12;
                double scale_x = (double)available_width / width;
                double scale_y = (double)available_height / height;
                double scale = scale_x < scale_y ? scale_x : scale_y;
                int draw_width = (int)(width * scale);
                int draw_height = (int)(height * scale);
                if (draw_width < 1) draw_width = 1;
                if (draw_height < 1) draw_height = 1;
                int x = rect.left + ((rect.right - rect.left) - draw_width) / 2;
                int y = rect.top + ((rect.bottom - rect.top) - draw_height) / 2;
                GpGraphics *graphics = NULL;
                if (GdipCreateFromHDC(item->hDC, &graphics) == Ok && graphics) {
                    GdipSetInterpolationMode(graphics, InterpolationModeHighQualityBicubic);
                    displayed = GdipDrawImageRectI(graphics, image, x, y, draw_width, draw_height) == Ok;
                    GdipDeleteGraphics(graphics);
                }
            }
            GdipDisposeImage(image);
        }
    }
    if (!displayed) {
        RECT text = rect;
        InflateRect(&text, -10, -8);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, RGB(92, 105, 127));
        HGDIOBJ old_font = app_font ? SelectObject(item->hDC, app_font) : NULL;
        const wchar_t *message = q && q->image_path[0]
            ? L"이미지를 불러오지 못했습니다."
            : L"이미지를 첨부하면 미리보기가 표시됩니다.";
        DrawTextW(item->hDC, message, -1, &text,
            DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
        if (old_font) SelectObject(item->hDC, old_font);
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        main_window = hwnd;
        app_font = CreateFontW(-19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Pretendard");
        heading_font = CreateFontW(-27, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Pretendard");
        if (launch_as_respondent) {
            selected_survey = 0;
            preview_mode = 0;
            response_index = 0;
            show_responder();
        } else {
            show_home();
        }
        if (data_load_error)
            MessageBoxW(hwnd, L"기존 JSON 파일을 읽지 못했습니다. 올바른 백업 파일을 가져오기 전까지 자동 저장은 중지됩니다.",
                APP_NAME, MB_ICONERROR);
        return 0;
    case WM_COMMAND:
        handle_command(LOWORD(wParam), HIWORD(wParam));
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)lParam;
        limits->ptMinTrackSize.x = 1010;
        limits->ptMinTrackSize.y = 820;
        return 0;
    }
    case WM_DRAWITEM:
        if (((DRAWITEMSTRUCT *)lParam)->CtlType == ODT_BUTTON) {
            draw_button((DRAWITEMSTRUCT *)lParam);
            return TRUE;
        }
        if (((DRAWITEMSTRUCT *)lParam)->CtlType == ODT_STATIC &&
            ((DRAWITEMSTRUCT *)lParam)->CtlID == ID_IMAGE_PREVIEW) {
            draw_image_preview((DRAWITEMSTRUCT *)lParam);
            return TRUE;
        }
        break;
    case WM_ERASEBKGND:
        paint_background(hwnd, (HDC)wParam);
        return 1;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wParam;
        HWND child = (HWND)lParam;
        RECT rect;
        GetWindowRect(child, &rect);
        POINT origin = { rect.left, rect.top };
        ScreenToClient(hwnd, &origin);
        int in_header = origin.y < 112;
        SetTextColor(dc, in_header ? RGB(255, 255, 255) : RGB(25, 43, 78));
        if (in_header) {
            SetBkMode(dc, TRANSPARENT);
            return (LRESULT)GetStockObject(HOLLOW_BRUSH);
        }
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, RGB(255, 255, 255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, RGB(24, 39, 68));
        SetBkColor(dc, RGB(255, 255, 255));
        return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    case WM_DESTROY:
        if (page == EDITOR) flush_question_editor();
        save_data();
        if (app_font) DeleteObject(app_font);
        if (heading_font) DeleteObject(heading_font);
        if (font_loaded) RemoveFontResourceExW(font_path, FR_PRIVATE, NULL);
        if (gdiplus_token) GdiplusShutdown(gdiplus_token);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)previous;
    INITCOMMONCONTROLSEX common_controls = { sizeof(common_controls), ICC_DATE_CLASSES };
    if (!InitCommonControlsEx(&common_controls)) return 1;
    wchar_t module_path[MAX_PATH] = L"";
    DWORD path_length = GetModuleFileNameW(NULL, module_path, MAX_PATH);
    if (path_length && path_length < MAX_PATH) {
        if (command_line && wcsstr(command_line, L"--respond")) {
            launch_as_respondent = 1;
            if (!change_extension(module_path, L".json", data_path, MAX_PATH)) return 1;
        } else {
            wchar_t *slash = wcsrchr(module_path, L'\\');
            size_t directory_length = slash ? (size_t)(slash + 1 - module_path) : 0;
            if (slash && directory_length + wcslen(DATA_FILE) < MAX_PATH) {
                copy_text(data_path, MAX_PATH, module_path);
                wcscpy(data_path + directory_length, DATA_FILE);
            } else copy_text(data_path, MAX_PATH, DATA_FILE);
        }
    } else {
        copy_text(data_path, MAX_PATH, DATA_FILE);
    }
    if (!make_adjacent_path(module_path, L"Pretendard-Regular.otf", font_path, MAX_PATH) ||
        !make_adjacent_path(module_path, L"OFL-Pretendard.txt", font_license_path, MAX_PATH))
        return 1;
    font_loaded = AddFontResourceExW(font_path, FR_PRIVATE, NULL) != 0;
    if (!font_loaded || GetFileAttributesW(font_license_path) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(NULL, L"Pretendard 글꼴 파일이나 라이선스 파일을 찾지 못했습니다. build.bat으로 다시 빌드하고 두 파일을 실행 파일 옆에 두세요.",
            APP_NAME, MB_ICONWARNING);
    }
    if (!launch_as_respondent && path_length && path_length < MAX_PATH) {
        wchar_t companion_path[MAX_PATH];
        if (change_extension(module_path, L".json", companion_path, MAX_PATH) &&
            GetFileAttributesW(companion_path) != INVALID_FILE_ATTRIBUTES) {
            wchar_t *base_name = wcsrchr(companion_path, L'\\');
            base_name = base_name ? base_name + 1 : companion_path;
            if (_wcsicmp(base_name, DATA_FILE) != 0) {
                copy_text(data_path, MAX_PATH, companion_path);
                launch_as_respondent = 1;
            }
        }
    }
    load_data();
    if (launch_as_respondent && (data_load_error || survey_count != 1)) {
        MessageBoxW(NULL, L"응답용 설문 JSON을 읽지 못했습니다. EXE와 같은 폴더에 같은 이름의 올바른 JSON 파일이 있는지 확인하세요.",
            APP_NAME, MB_ICONERROR);
        return 1;
    }
    GdiplusStartupInput gdiplus_input = { 1, NULL, FALSE, FALSE };
    if (GdiplusStartup(&gdiplus_token, &gdiplus_input, NULL) != Ok) {
        MessageBoxW(NULL, L"이미지 미리보기를 초기화하지 못했습니다. Windows GDI+ 구성요소를 확인하세요.",
            APP_NAME, MB_ICONERROR);
        return 1;
    }
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"UgaSurveyWindow";
    if (!RegisterClassW(&wc)) { GdiplusShutdown(gdiplus_token); return 1; }
    main_window = CreateWindowExW(0, wc.lpszClassName, APP_NAME, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1080, 820, NULL, NULL, instance, NULL);
    if (!main_window) { GdiplusShutdown(gdiplus_token); return 1; }
    ShowWindow(main_window, show);
    UpdateWindow(main_window);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
