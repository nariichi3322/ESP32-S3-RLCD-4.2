// 构建并刷新小智 AI 工作页，复用反显时钟并以单色表情呈现官方情绪。
#include "ui_views.h"

#include "ui_battery.h"
#include "pomodoro_services.h"
#include "xiaozhi_ai.h"

namespace {
constexpr int kClockCardCount = 3;
constexpr int kTopLineX = 18;
constexpr int kTopLineY = 54;
constexpr int kTopLineW = 364;
constexpr int kTopLineH = 4;
constexpr int kInteractionPanelX = 18;
constexpr int kInteractionPanelY = 188;
constexpr int kInteractionPanelW = 364;
constexpr int kInteractionPanelH = 102;
constexpr int kInteractionPanelRadius = 18;
constexpr int kFaceX = 30;
constexpr int kFaceY = 201;
constexpr int kFaceW = 76;
constexpr int kFaceH = 76;
constexpr int kFaceCenterX = kFaceW / 2;
constexpr int kFaceCenterY = kFaceH / 2;
constexpr int kStateX = 118;
constexpr int kStateY = 196;
constexpr int kStateW = 248;
constexpr int kStateH = 28;
constexpr int kPreparingDotsX = 218;
constexpr int kPreparingDotsY = 201;
constexpr int kPreparingDotsW = 48;
constexpr int kPreparingDotsH = 16;
constexpr int kPreparingDotRadius = 4;
constexpr int kPreparingDotCenterY = kPreparingDotsH / 2;
constexpr int kPreparingDotCenterX[] = {6, 22, 38};
constexpr int kDetailX = 118;
constexpr int kDetailY = 224;
constexpr int kDetailW = 248;
constexpr int kDetailH = 58;
constexpr uint32_t kSubtitleCharacterIntervalMs = 80;
constexpr uint32_t kExpressionFrameIntervalMs = 250;
constexpr uint32_t kPreparingDotsIntervalMs = 400;
constexpr const char *kBindingPrefix = "绑定 ID: ";
constexpr const char *kPreparingStatus = "小智准备中";
constexpr int kPomodoroTitleY = 8;
constexpr int kPomodoroStateY = 44;
constexpr int kPomodoroModeY = 76;
constexpr int kPomodoroTextW = 112;
constexpr int kPomodoroTextH = 24;
constexpr int kPomodoroTitleH = 30;
constexpr const char *kPomodoroTitle = "番茄钟";
constexpr const char *kPomodoroRunningText = "专注中";
constexpr const char *kPomodoroCompletedText = "已完成";
constexpr const char *kPomodoroMinuteSecondMode = "分 / 秒";
constexpr const char *kWaveCanvasCreateFailedLog = "Xiaozhi wave canvas create failed";
constexpr const char *kPreparingDotsCanvasCreateFailedLog =
    "Xiaozhi preparing dots canvas create failed";

enum FaceEmotion {
    kFaceNeutral,
    kFaceHappy,
    kFaceSad,
    kFaceAngry,
    kFaceSurprised,
    kFaceThinking,
    kFaceSleepy,
    kFaceCool,
};

lv_obj_t *s_clock_cards[kClockCardCount] = {};
lv_color_t *s_clock_card_buffers[kClockCardCount] = {};
lv_color_t *s_face_canvas_buffer = nullptr;
int s_last_clock_values[kClockCardCount] = {-1, -1, -1};
lv_obj_t *s_last_face_canvas = nullptr;
XiaozhiAiState s_last_face_state = kXiaozhiAiInactive;
int s_last_face_frame = -1;
char s_last_face_emotion[24] = {};
XiaozhiAiState s_animation_state = kXiaozhiAiInactive;
char s_subtitle_target[192] = {};
char s_subtitle_visible[192] = {};
char s_subtitle_window[192] = {};
char s_subtitle_window_source[192] = {};
size_t s_subtitle_visible_bytes = 0;
size_t s_subtitle_visible_characters = 0;
uint32_t s_subtitle_last_tick = 0;
lv_obj_t *s_pomodoro_title_label = nullptr;
lv_obj_t *s_pomodoro_title_bold_label = nullptr;
lv_obj_t *s_pomodoro_state_label = nullptr;
lv_obj_t *s_pomodoro_mode_label = nullptr;
lv_obj_t *s_preparing_dots_canvas = nullptr;
lv_color_t *s_preparing_dots_buffer = nullptr;
PomodoroState s_last_pomodoro_state = kPomodoroIdle;
int s_last_preparing_dot_count = -1;

struct PomodoroDisplayValues {
    int second_card;
    int third_card;
};

constexpr PomodoroDisplayValues pomodoro_display_values(uint32_t remaining_ms)
{
    uint32_t remaining_seconds = pomodoro_display_seconds(remaining_ms);
    return {static_cast<int>(remaining_seconds / 60U),
            static_cast<int>(remaining_seconds % 60U)};
}

constexpr PomodoroDisplayValues kPomodoroAtSixtySeconds = pomodoro_display_values(60000U);
constexpr PomodoroDisplayValues kPomodoroAtFiftyNineSeconds = pomodoro_display_values(59000U);
static_assert(kPomodoroAtSixtySeconds.second_card == 1 &&
                  kPomodoroAtSixtySeconds.third_card == 0,
              "60 seconds must remain one minute and zero seconds");
static_assert(kPomodoroAtFiftyNineSeconds.second_card == 0 &&
                  kPomodoroAtFiftyNineSeconds.third_card == 59,
              "59 seconds must show zero minutes and 59 seconds");

lv_obj_t *create_xiaozhi_canvas(lv_obj_t *parent,
                                lv_color_t *buffer,
                                int x,
                                int y,
                                int width,
                                int height,
                                const char *failure_log)
{
    if (!parent || !buffer) {
        return nullptr;
    }
    lv_obj_t *canvas = lv_canvas_create(parent);
    if (!canvas) {
        ESP_LOGW(TAG, "%s", failure_log);
        return nullptr;
    }
    configure_canvas_base(canvas, buffer, x, y, width, height);
    return canvas;
}

lv_obj_t *make_pomodoro_card_label(lv_obj_t *parent,
                                    int y,
                                    int height,
                                    const char *text,
                                    const lv_font_t *font)
{
    lv_obj_t *label = make_label_with_font(parent,
                                           0,
                                           y,
                                           kPomodoroTextW,
                                           height,
                                           text,
                                           font);
    if (label) {
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    return label;
}

void set_pomodoro_labels_visible(bool visible)
{
    set_obj_visible(s_pomodoro_title_label, visible);
    set_obj_visible(s_pomodoro_title_bold_label, visible);
    set_obj_visible(s_pomodoro_state_label, visible);
    set_obj_visible(s_pomodoro_mode_label, visible);
}

void draw_preparing_dots(int count)
{
    if (!s_preparing_dots_canvas || count == s_last_preparing_dot_count) {
        return;
    }
    lv_canvas_fill_bg(s_preparing_dots_canvas, lv_color_black(), LV_OPA_COVER);
    for (int i = 0; i < count && i < 3; ++i) {
        canvas_draw_filled_circle(s_preparing_dots_canvas,
                                  kPreparingDotsW,
                                  kPreparingDotsH,
                                  kPreparingDotCenterX[i],
                                  kPreparingDotCenterY,
                                  kPreparingDotRadius,
                                  lv_color_white());
    }
    s_last_preparing_dot_count = count;
    lv_obj_invalidate(s_preparing_dots_canvas);
}

bool update_xiaozhi_clock_or_pomodoro(const struct tm &local,
                                      const PomodoroSnapshot &pomodoro)
{
    if (pomodoro.state == kPomodoroIdle) {
        if (s_last_pomodoro_state != kPomodoroIdle) {
            set_pomodoro_labels_visible(false);
            for (int &value : s_last_clock_values) {
                value = -1;
            }
        }
        s_last_pomodoro_state = kPomodoroIdle;
        return update_inverted_clock_cards(local, s_clock_cards, s_last_clock_values);
    }

    bool state_changed = pomodoro.state != s_last_pomodoro_state;
    PomodoroDisplayValues display = pomodoro_display_values(pomodoro.remaining_ms);
    bool changed = false;
    if (state_changed) {
        clear_inverted_clock_card(s_clock_cards[0]);
        set_pomodoro_labels_visible(true);
        s_last_clock_values[0] = -1;
        s_last_clock_values[1] = -1;
        s_last_clock_values[2] = -1;
        changed = true;
    }
    if (state_changed) {
        changed |= set_label_text_if_changed(s_pomodoro_title_label, kPomodoroTitle);
        changed |= set_label_text_if_changed(s_pomodoro_title_bold_label, kPomodoroTitle);
        changed |= set_label_text_if_changed(
            s_pomodoro_state_label,
            pomodoro.state == kPomodoroCompleted ? kPomodoroCompletedText : kPomodoroRunningText);
        changed |= set_label_text_if_changed(
            s_pomodoro_mode_label,
            pomodoro.state == kPomodoroCompleted
                ? ""
                : kPomodoroMinuteSecondMode);
    }

    int second_card_value = 0;
    int third_card_value = 0;
    if (pomodoro.state == kPomodoroRunning) {
        second_card_value = display.second_card;
        third_card_value = display.third_card;
    }
    changed |= update_inverted_clock_card_value(s_clock_cards[1],
                                                second_card_value,
                                                &s_last_clock_values[1]);
    changed |= update_inverted_clock_card_value(s_clock_cards[2],
                                                third_card_value,
                                                &s_last_clock_values[2]);
    s_last_pomodoro_state = pomodoro.state;
    return changed;
}

size_t utf8_codepoint_size(const char *text)
{
    if (!text || text[0] == '\0') {
        return 0;
    }
    const unsigned char first = static_cast<unsigned char>(text[0]);
    if (first < 0x80) return 1;
    if ((first & 0xe0) == 0xc0) return 2;
    if ((first & 0xf0) == 0xe0) return 3;
    if ((first & 0xf8) == 0xf0) return 4;
    return 1;
}

void reveal_next_subtitle_character()
{
    size_t target_len = strlen(s_subtitle_target);
    if (s_subtitle_visible_bytes >= target_len) {
        return;
    }
    size_t character_size = utf8_codepoint_size(s_subtitle_target + s_subtitle_visible_bytes);
    if (character_size == 0 ||
        s_subtitle_visible_bytes + character_size > target_len ||
        s_subtitle_visible_bytes + character_size >= sizeof(s_subtitle_visible)) {
        return;
    }
    memcpy(s_subtitle_visible + s_subtitle_visible_bytes,
           s_subtitle_target + s_subtitle_visible_bytes,
           character_size);
    s_subtitle_visible_bytes += character_size;
    ++s_subtitle_visible_characters;
    s_subtitle_visible[s_subtitle_visible_bytes] = '\0';
}

const char *progressive_subtitle(const XiaozhiAiSnapshot &snapshot, const char *detail)
{
    detail = detail ? detail : "";
    if (snapshot.state != kXiaozhiAiSpeaking) {
        s_subtitle_target[0] = '\0';
        s_subtitle_visible[0] = '\0';
        s_subtitle_visible_bytes = 0;
        s_subtitle_visible_characters = 0;
        return detail;
    }
    uint32_t now = lv_tick_get();
    if (strcmp(s_subtitle_target, detail) != 0) {
        strlcpy(s_subtitle_target, detail, sizeof(s_subtitle_target));
        s_subtitle_visible[0] = '\0';
        s_subtitle_visible_bytes = 0;
        s_subtitle_visible_characters = 0;
        s_subtitle_last_tick = now;
        reveal_next_subtitle_character();
    } else {
        uint32_t elapsed = lv_tick_elaps(s_subtitle_last_tick);
        size_t due_characters = 1 + elapsed / kSubtitleCharacterIntervalMs;
        while (s_subtitle_visible_characters < due_characters &&
               s_subtitle_visible_bytes < strlen(s_subtitle_target)) {
            reveal_next_subtitle_character();
        }
    }
    return s_subtitle_visible;
}

const char *latest_visible_subtitle(const char *text)
{
    text = text ? text : "";
    if (strcmp(s_subtitle_window_source, text) == 0) {
        return s_subtitle_window;
    }
    const char *window_start = text;
    lv_point_t text_size = {};
    lv_txt_get_size(&text_size,
                    window_start,
                    &zh_font_16,
                    0,
                    0,
                    kDetailW,
                    LV_TEXT_FLAG_NONE);
    // Remove complete wrapped lines from the front until the remaining suffix
    // fits the fixed interaction area. This keeps the newest spoken content on
    // screen without splitting UTF-8 characters or changing the panel layout.
    while (text_size.y > kDetailH && window_start[0] != '\0') {
        uint32_t first_line_bytes = _lv_txt_get_next_line(window_start,
                                                         &zh_font_16,
                                                         0,
                                                         kDetailW,
                                                         nullptr,
                                                         LV_TEXT_FLAG_NONE);
        if (first_line_bytes == 0 || window_start[first_line_bytes] == '\0') {
            break;
        }
        window_start += first_line_bytes;
        lv_txt_get_size(&text_size,
                        window_start,
                        &zh_font_16,
                        0,
                        0,
                        kDetailW,
                        LV_TEXT_FLAG_NONE);
    }
    strlcpy(s_subtitle_window_source, text, sizeof(s_subtitle_window_source));
    strlcpy(s_subtitle_window, window_start, sizeof(s_subtitle_window));
    return s_subtitle_window;
}

bool emotion_is(const char *emotion, const char *value)
{
    return emotion && strcmp(emotion, value) == 0;
}

FaceEmotion face_emotion_for_snapshot(const XiaozhiAiSnapshot &snapshot)
{
    const char *emotion = snapshot.emotion;
    if (emotion_is(emotion, "sad") || emotion_is(emotion, "crying")) return kFaceSad;
    if (emotion_is(emotion, "angry")) return kFaceAngry;
    if (emotion_is(emotion, "surprised") || emotion_is(emotion, "shocked")) return kFaceSurprised;
    if (emotion_is(emotion, "thinking") || emotion_is(emotion, "confused") ||
        emotion_is(emotion, "embarrassed")) return kFaceThinking;
    if (emotion_is(emotion, "sleepy")) return kFaceSleepy;
    if (emotion_is(emotion, "cool")) return kFaceCool;
    if (emotion_is(emotion, "happy") || emotion_is(emotion, "laughing") ||
        emotion_is(emotion, "funny") || emotion_is(emotion, "loving") ||
        emotion_is(emotion, "winking") || emotion_is(emotion, "relaxed") ||
        emotion_is(emotion, "delicious") || emotion_is(emotion, "kissy") ||
        emotion_is(emotion, "confident") || emotion_is(emotion, "silly")) return kFaceHappy;
    if (snapshot.state == kXiaozhiAiError) return kFaceSad;
    return kFaceNeutral;
}

void draw_face_outline(lv_obj_t *canvas)
{
    canvas_draw_filled_circle(canvas, kFaceW, kFaceH, kFaceCenterX, kFaceCenterY, 32, lv_color_white());
    canvas_draw_filled_circle(canvas, kFaceW, kFaceH, kFaceCenterX, kFaceCenterY, 29, lv_color_black());
}

void draw_open_eye(lv_obj_t *canvas, int x, int y, int radius = 3)
{
    canvas_draw_filled_circle(canvas, kFaceW, kFaceH, x, y, radius, lv_color_white());
}

void draw_happy_mouth(lv_obj_t *canvas, bool inverted)
{
    const int direction = inverted ? -1 : 1;
    int y = inverted ? 53 : 45;
    canvas_draw_line(canvas, kFaceW, kFaceH, 25, y, 30, y + 5 * direction, lv_color_white());
    canvas_draw_line(canvas, kFaceW, kFaceH, 30, y + 5 * direction, 38, y + 7 * direction, lv_color_white());
    canvas_draw_line(canvas, kFaceW, kFaceH, 38, y + 7 * direction, 46, y + 5 * direction, lv_color_white());
    canvas_draw_line(canvas, kFaceW, kFaceH, 46, y + 5 * direction, 51, y, lv_color_white());
}

void draw_expression(const XiaozhiAiSnapshot &snapshot)
{
    if (!g_xiaozhi_wave_canvas || !s_face_canvas_buffer) {
        return;
    }
    bool animated = snapshot.state == kXiaozhiAiActivating ||
                    snapshot.state == kXiaozhiAiBinding ||
                    snapshot.state == kXiaozhiAiListening ||
                    snapshot.state == kXiaozhiAiSpeaking;
    int frame = animated ? static_cast<int>((lv_tick_get() / kExpressionFrameIntervalMs) % 4) : 0;
    if (s_last_face_canvas == g_xiaozhi_wave_canvas &&
        s_last_face_state == snapshot.state &&
        s_last_face_frame == frame &&
        strcmp(s_last_face_emotion, snapshot.emotion) == 0) {
        return;
    }

    lv_canvas_fill_bg(g_xiaozhi_wave_canvas, lv_color_black(), LV_OPA_COVER);
    draw_face_outline(g_xiaozhi_wave_canvas);
    FaceEmotion emotion = face_emotion_for_snapshot(snapshot);
    int eye_y = 30;

    if (emotion == kFaceHappy) {
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 22, 31, 27, 27, lv_color_white());
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 27, 27, 32, 31, lv_color_white());
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 44, 31, 49, 27, lv_color_white());
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 49, 27, 54, 31, lv_color_white());
    } else if (emotion == kFaceAngry) {
        draw_open_eye(g_xiaozhi_wave_canvas, 27, eye_y);
        draw_open_eye(g_xiaozhi_wave_canvas, 49, eye_y);
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 20, 23, 32, 27, lv_color_white());
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 44, 27, 56, 23, lv_color_white());
    } else if (emotion == kFaceSleepy) {
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 22, eye_y, 32, eye_y, lv_color_white());
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 44, eye_y, 54, eye_y, lv_color_white());
    } else if (emotion == kFaceCool) {
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 19, 27, 34, 27, lv_color_white());
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 42, 27, 57, 27, lv_color_white());
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 34, 27, 42, 27, lv_color_white());
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 20, 28, 23, 35, lv_color_white());
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 56, 28, 53, 35, lv_color_white());
    } else if (emotion == kFaceThinking) {
        draw_open_eye(g_xiaozhi_wave_canvas, 27, eye_y);
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 44, eye_y, 54, eye_y - 3, lv_color_white());
    } else {
        draw_open_eye(g_xiaozhi_wave_canvas, 27, eye_y,
                      snapshot.state == kXiaozhiAiReady && frame == 3 ? 1 : 3);
        draw_open_eye(g_xiaozhi_wave_canvas, 49, eye_y,
                      snapshot.state == kXiaozhiAiReady && frame == 3 ? 1 : 3);
    }

    if (snapshot.state == kXiaozhiAiSpeaking) {
        int mouth_radius = 5 + (frame % 2) * 3;
        canvas_draw_filled_circle(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 38, 51, mouth_radius, lv_color_white());
        if (mouth_radius > 5) {
            canvas_draw_filled_circle(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 38, 51, mouth_radius - 3, lv_color_black());
        }
    } else if (emotion == kFaceHappy) {
        draw_happy_mouth(g_xiaozhi_wave_canvas, false);
    } else if (emotion == kFaceSad || emotion == kFaceAngry) {
        draw_happy_mouth(g_xiaozhi_wave_canvas, true);
    } else if (emotion == kFaceSurprised) {
        canvas_draw_filled_circle(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 38, 50, 7, lv_color_white());
        canvas_draw_filled_circle(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 38, 50, 4, lv_color_black());
    } else if (emotion == kFaceThinking) {
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 33, 51, 47, 48, lv_color_white());
    } else {
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 29, 50, 47, 50, lv_color_white());
    }

    if (snapshot.state == kXiaozhiAiListening) {
        int inset = frame % 2;
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 3 + inset, 29, 3 + inset, 47, lv_color_white());
        canvas_draw_line(g_xiaozhi_wave_canvas, kFaceW, kFaceH, 72 - inset, 29, 72 - inset, 47, lv_color_white());
    } else if (snapshot.state == kXiaozhiAiActivating || snapshot.state == kXiaozhiAiBinding) {
        int dot_x[4] = {38, 58, 38, 18};
        int dot_y[4] = {10, 38, 66, 38};
        draw_open_eye(g_xiaozhi_wave_canvas, dot_x[frame], dot_y[frame], 2);
    }

    lv_obj_invalidate(g_xiaozhi_wave_canvas);
    s_last_face_canvas = g_xiaozhi_wave_canvas;
    s_last_face_state = snapshot.state;
    s_last_face_frame = frame;
    strlcpy(s_last_face_emotion, snapshot.emotion, sizeof(s_last_face_emotion));
}

void style_panel_label(lv_obj_t *label, lv_text_align_t align)
{
    if (!label) {
        return;
    }
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
}
} // namespace

void build_xiaozhi_page()
{
    if (g_xiaozhi_root) {
        return;
    }
    g_xiaozhi_root = create_page_root();
    if (!g_xiaozhi_root) {
        return;
    }
    build_work_page_status_bar(g_xiaozhi_root,
                               kWorkPageXiaozhiAI,
                               &g_xiaozhi_date_label,
                               &g_xiaozhi_summary_label,
                               &g_xiaozhi_status_time_label,
                               true);
    set_obj_visible(g_xiaozhi_status_time_label, false);
    lv_obj_t *top_line = make_bar(g_xiaozhi_root, kTopLineX, kTopLineY, kTopLineW, kTopLineH);
    set_obj_black(top_line, true);
    build_work_page_day_progress(g_xiaozhi_root, kWorkPageXiaozhiAI);
    build_inverted_clock_cards(g_xiaozhi_root, s_clock_cards, s_clock_card_buffers);
    s_pomodoro_title_label = make_pomodoro_card_label(s_clock_cards[0],
                                                       kPomodoroTitleY,
                                                       kPomodoroTitleH,
                                                       kPomodoroTitle,
                                                       &zh_pomodoro_title_24);
    s_pomodoro_title_bold_label = make_pomodoro_card_label(s_clock_cards[0],
                                                            kPomodoroTitleY,
                                                            kPomodoroTitleH,
                                                            kPomodoroTitle,
                                                            &zh_pomodoro_title_24);
    if (s_pomodoro_title_bold_label) {
        lv_obj_set_x(s_pomodoro_title_bold_label, 1);
    }
    s_pomodoro_state_label = make_pomodoro_card_label(s_clock_cards[0],
                                                       kPomodoroStateY,
                                                       kPomodoroTextH,
                                                       kPomodoroRunningText,
                                                       &zh_font_16);
    s_pomodoro_mode_label = make_pomodoro_card_label(s_clock_cards[0],
                                                      kPomodoroModeY,
                                                      kPomodoroTextH,
                                                      kPomodoroMinuteSecondMode,
                                                      &zh_font_16);
    set_pomodoro_labels_visible(false);
    s_last_clock_values[0] = -1;
    s_last_clock_values[1] = -1;
    s_last_clock_values[2] = -1;

    lv_obj_t *interaction_panel = make_bar(g_xiaozhi_root,
                                           kInteractionPanelX,
                                           kInteractionPanelY,
                                           kInteractionPanelW,
                                           kInteractionPanelH);
    set_obj_black(interaction_panel, true);
    if (interaction_panel) {
        lv_obj_set_style_radius(interaction_panel, kInteractionPanelRadius, LV_PART_MAIN);
        lv_obj_set_style_clip_corner(interaction_panel, true, LV_PART_MAIN);
    }

    if (!s_face_canvas_buffer) {
        s_face_canvas_buffer = alloc_canvas_buffer(kFaceW, kFaceH);
    }
    g_xiaozhi_wave_canvas = create_xiaozhi_canvas(g_xiaozhi_root,
                                                   s_face_canvas_buffer,
                                                   kFaceX,
                                                   kFaceY,
                                                   kFaceW,
                                                   kFaceH,
                                                   kWaveCanvasCreateFailedLog);

    g_xiaozhi_state_label = make_label_with_font(g_xiaozhi_root,
                                                  kStateX,
                                                  kStateY,
                                                  kStateW,
                                                  kStateH,
                                                  "",
                                                  &zh_font_16);
    g_xiaozhi_detail_label = make_label_with_font(g_xiaozhi_root,
                                                   kDetailX,
                                                   kDetailY,
                                                   kDetailW,
                                                   kDetailH,
                                                   "",
                                                   &zh_font_16);
    style_panel_label(g_xiaozhi_state_label, LV_TEXT_ALIGN_LEFT);
    style_panel_label(g_xiaozhi_detail_label, LV_TEXT_ALIGN_LEFT);
    if (!s_preparing_dots_buffer) {
        s_preparing_dots_buffer = alloc_canvas_buffer(kPreparingDotsW, kPreparingDotsH);
    }
    s_preparing_dots_canvas = create_xiaozhi_canvas(g_xiaozhi_root,
                                                     s_preparing_dots_buffer,
                                                     kPreparingDotsX,
                                                     kPreparingDotsY,
                                                     kPreparingDotsW,
                                                     kPreparingDotsH,
                                                     kPreparingDotsCanvasCreateFailedLog);
    if (s_preparing_dots_canvas) {
        set_obj_visible(s_preparing_dots_canvas, false);
    }
    s_last_preparing_dot_count = -1;
    if (g_xiaozhi_detail_label) {
        lv_label_set_long_mode(g_xiaozhi_detail_label, LV_LABEL_LONG_WRAP);
    }
    build_battery_icon(g_xiaozhi_root, g_xiaozhi_battery_segments);
}

bool update_xiaozhi_page(const struct tm &local)
{
    if (!g_xiaozhi_root) {
        build_xiaozhi_page();
    }
    XiaozhiAiSnapshot snapshot = {};
    xiaozhi_ai_get_snapshot(&snapshot);
    PomodoroSnapshot pomodoro = {};
    pomodoro_get_snapshot(&pomodoro);
    s_animation_state = snapshot.state;

    char detail[192] = {};
    if (snapshot.binding_code[0] != '\0') {
        snprintf(detail, sizeof(detail), "%s%s", kBindingPrefix, snapshot.binding_code);
    } else {
        strlcpy(detail, snapshot.detail, sizeof(detail));
    }
    const char *display_detail = latest_visible_subtitle(
        progressive_subtitle(snapshot, detail));
    bool changed = false;
    const char *display_status = snapshot.status;
    if (snapshot.state == kXiaozhiAiInactive) {
        display_status = kPreparingStatus;
        int dots = 1 + static_cast<int>((lv_tick_get() / kPreparingDotsIntervalMs) % 3U);
        if (dots != s_last_preparing_dot_count) {
            draw_preparing_dots(dots);
            changed = true;
        }
        changed |= set_obj_visible(s_preparing_dots_canvas, true);
    } else {
        changed |= set_obj_visible(s_preparing_dots_canvas, false);
    }
    changed |= set_label_text_if_changed(g_xiaozhi_state_label, display_status);
    changed |= set_label_text_if_changed(g_xiaozhi_detail_label, display_detail);
    const bool status_time_visible = pomodoro.state != kPomodoroIdle;
    changed |= set_obj_visible(g_xiaozhi_status_time_label, status_time_visible);
    if (status_time_visible) {
        changed |= update_work_page_status_time(g_xiaozhi_status_time_label, local);
    }
    changed |= update_work_page_sensor_summary(g_xiaozhi_summary_label);
    changed |= update_work_page_status_icons(kWorkPageXiaozhiAI);
    changed |= update_xiaozhi_clock_or_pomodoro(local, pomodoro);
    int previous_face_frame = s_last_face_frame;
    XiaozhiAiState previous_face_state = s_last_face_state;
    char previous_emotion[sizeof(s_last_face_emotion)] = {};
    strlcpy(previous_emotion, s_last_face_emotion, sizeof(previous_emotion));
    draw_expression(snapshot);
    changed |= previous_face_frame != s_last_face_frame ||
               previous_face_state != s_last_face_state ||
               strcmp(previous_emotion, s_last_face_emotion) != 0;
    return changed;
}

uint32_t xiaozhi_subtitle_animation_delay_ms()
{
    uint32_t delay_ms = 0;
    size_t target_len = strlen(s_subtitle_target);
    if (target_len > 0 && s_subtitle_visible_bytes < target_len) {
        uint32_t elapsed = lv_tick_elaps(s_subtitle_last_tick);
        uint32_t next_character_due = static_cast<uint32_t>(s_subtitle_visible_characters) *
                                      kSubtitleCharacterIntervalMs;
        delay_ms = elapsed >= next_character_due ? 1 : next_character_due - elapsed;
    }
    bool expression_animated = s_animation_state == kXiaozhiAiActivating ||
                               s_animation_state == kXiaozhiAiBinding ||
                               s_animation_state == kXiaozhiAiListening ||
                               s_animation_state == kXiaozhiAiSpeaking;
    if (expression_animated && (delay_ms == 0 || delay_ms > kExpressionFrameIntervalMs)) {
        delay_ms = kExpressionFrameIntervalMs;
    }
    if (s_animation_state == kXiaozhiAiInactive) {
        uint32_t preparing_delay = kPreparingDotsIntervalMs -
                                   (lv_tick_get() % kPreparingDotsIntervalMs);
        if (delay_ms == 0 || preparing_delay < delay_ms) {
            delay_ms = preparing_delay;
        }
    }
    return delay_ms;
}
