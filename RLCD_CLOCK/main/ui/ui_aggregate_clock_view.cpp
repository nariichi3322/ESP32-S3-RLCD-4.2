// 绘制聚合时钟四块差异化信息区，数字仅按变化的两位时间局部失效。
#include "ui_aggregate_clock_view.h"
#include "aggregate_sensor_icons.h"
#include "dseg_digits.h"
#include <cstring>
#include <initializer_list>

LV_FONT_DECLARE(zh_font_16);
LV_FONT_DECLARE(qweather_icons_36);
LV_FONT_DECLARE(zh_flip_lunar_22);
LV_FONT_DECLARE(aggregate_numeric_20);

namespace {
lv_obj_t *panel(lv_obj_t *root, int x, int y, int w, int h, bool black) {
    lv_obj_t *p = lv_obj_create(root);
    lv_obj_remove_style_all(p);
    lv_obj_set_pos(p,x,y); lv_obj_set_size(p,w,h);
    lv_obj_set_style_radius(p,6,0);
    lv_obj_set_style_bg_color(p,black ? lv_color_black() : lv_color_white(),0);
    lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);
    lv_obj_clear_flag(p,LV_OBJ_FLAG_SCROLLABLE);
    return p;
}
lv_obj_t *label(lv_obj_t *root,int x,int y,int w,int h,const char *text,
                const lv_font_t *font=&zh_font_16,bool white=false) {
    lv_obj_t *p=lv_label_create(root);
    lv_obj_set_pos(p,x,y); lv_obj_set_size(p,w,h);
    lv_obj_set_style_text_font(p,font,0);
    lv_obj_set_style_text_color(p,white ? lv_color_white() : lv_color_black(),0);
    lv_label_set_long_mode(p,LV_LABEL_LONG_DOT);
    lv_label_set_text(p,text);
    return p;
}
void embolden(lv_obj_t *obj) {
    // Reuse the lunar subset; a one-pixel overstrike avoids another Chinese font.
    lv_obj_add_event_cb(obj,[](lv_event_t *e) {
        lv_obj_t *label=lv_event_get_target(e);
        lv_area_t area; lv_obj_get_coords(label,&area);
        ++area.x1; ++area.x2;
        lv_draw_label_dsc_t style; lv_draw_label_dsc_init(&style);
        lv_obj_init_draw_label_dsc(label,LV_PART_MAIN,&style);
        lv_draw_label(lv_event_get_draw_ctx(e),&style,&area,lv_label_get_text(label),nullptr);
    },LV_EVENT_DRAW_MAIN,nullptr);
}
void stipple(lv_obj_t *root,int x,int y,int w,int h,int stride) {
    lv_obj_t *p=panel(root,x,y,w,h,false);
    lv_obj_add_event_cb(p,[](lv_event_t *e) {
        lv_obj_t *obj=lv_event_get_target(e);
        lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(e);
        lv_area_t area; lv_obj_get_coords(obj,&area);
        const int step=static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
        lv_draw_rect_dsc_t style; lv_draw_rect_dsc_init(&style);
        style.bg_color=lv_color_black();
        for(int y=area.y1+4;y<area.y2-2;y+=step)
            for(int x=area.x1+4;x<area.x2-2;x+=step) {
                lv_area_t dot={static_cast<lv_coord_t>(x),static_cast<lv_coord_t>(y),static_cast<lv_coord_t>(x),static_cast<lv_coord_t>(y)};
                lv_draw_rect(ctx,&style,&dot);
            }
    },LV_EVENT_DRAW_MAIN,reinterpret_cast<void *>(static_cast<uintptr_t>(stride)));
}
void sensor_icon(lv_obj_t *root,int x,int y,const uint8_t *bits) {
    lv_obj_t *p=panel(root,x,y,24,24,true);
    lv_obj_set_style_bg_opa(p,LV_OPA_TRANSP,0);
    lv_obj_add_event_cb(p,[](lv_event_t *e) {
        lv_area_t area; lv_obj_get_coords(lv_event_get_target(e),&area);
        const auto *bitmap=static_cast<const uint8_t *>(lv_event_get_user_data(e));
        lv_draw_rect_dsc_t style; lv_draw_rect_dsc_init(&style); style.bg_color=lv_color_white();
        for(int y=0;y<24;++y) for(int x=0;x<24;++x) if(bitmap[y*3+x/8] & (128U>>(x%8))) {
            lv_area_t dot={static_cast<lv_coord_t>(area.x1+x),static_cast<lv_coord_t>(area.y1+y),static_cast<lv_coord_t>(area.x1+x),static_cast<lv_coord_t>(area.y1+y)};
            lv_draw_rect(lv_event_get_draw_ctx(e),&style,&dot);
        }
    },LV_EVENT_DRAW_MAIN,const_cast<uint8_t *>(bits));
}
void draw_pair(lv_obj_t *canvas,int value) {
    lv_img_dsc_t *image=lv_canvas_get_img(canvas);
    for(int y=0;y<kAggregateDigitHeight;++y)
        for(int x=0;x<kAggregateDigitWidth;++x)
            lv_img_buf_set_px_color(image,x,y,lv_color_black());
    if(value>=0 && value<=99) {
        const int digits[2]={value/10,value%10};
        for(int d=0;d<2;++d) {
            const DsegGlyph &g=kDSEG84Glyphs[digits[d]];
            for(int y=0;y<63;++y) for(int x=0;x<52;++x) {
                int sx=x*4/3-g.x_offset;
                int sy=y*4/3-84-g.y_offset;
                if(sx<0 || sy<0 || sx>=g.width || sy>=g.height) continue;
                unsigned bit=sy*g.width+sx;
                if(kDSEG84Bitmaps[g.bitmap_offset+bit/8] & (128U>>(bit%8)))
                    lv_img_buf_set_px_color(image,d*52+x,8+y,lv_color_white());
            }
        }
    }
    lv_obj_invalidate(canvas);
}
}

void aggregate_clock_view_build(lv_obj_t *root,AggregateClockView &v,lv_color_t *const buffers[3]) {
    v={};
    panel(root,18,66,364,100,true);
    for(int i=0;i<3;++i) {
        v.digits[i]=lv_canvas_create(root);
        lv_obj_remove_style_all(v.digits[i]);
        lv_obj_set_pos(v.digits[i],24+i*124,76);
        if(buffers[i]) {
            lv_canvas_set_buffer(v.digits[i],buffers[i],104,80,LV_IMG_CF_TRUE_COLOR);
            draw_pair(v.digits[i],-1);
        }
    }
    // Circle centers follow the visible digit bounds, not the font baseline.
    for(int x : {135,259}) for(int y : {100,124}) {
        lv_obj_t *dot=panel(root,x,y,6,6,false);
        lv_obj_set_style_radius(dot,LV_RADIUS_CIRCLE,0);
    }
    stipple(root,18,174,222,120,6);
    stipple(root,248,174,134,58,4);
    panel(root,248,238,134,56,true);
    v.city=label(root,28,180,120,20,"等待数据");
    label(root,160,181,72,18,"今日天气");
    v.icon=label(root,31,207,48,42,"",&qweather_icons_36);
    v.weather=label(root,28,250,88,20,"--");
    v.temperature=label(root,108,202,128,54,"-- C",&lv_font_montserrat_48);
    v.range=label(root,28,269,207,17,"最高 -- C  最低 -- C",&zh_font_16);
    v.day=label(root,251,176,74,56,"--",&lv_font_montserrat_48);
    lv_obj_set_style_text_align(v.day,LV_TEXT_ALIGN_CENTER,0);
    v.month=label(root,325,178,54,26,"--月",&zh_flip_lunar_22);
    v.lunar=label(root,325,204,54,27,"--",&zh_flip_lunar_22);
    lv_obj_set_style_text_align(v.month,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_align(v.lunar,LV_TEXT_ALIGN_CENTER,0);
    embolden(v.month);
    embolden(v.lunar);
    sensor_icon(root,255,240,aggregate_temperature_bits);
    sensor_icon(root,255,265,aggregate_humidity_bits);
    v.local_temp=label(root,285,241,93,24,"--.- C",&aggregate_numeric_20,true);
    v.humidity=label(root,285,266,93,24,"--%",&aggregate_numeric_20,true);
}

bool aggregate_clock_view_time(AggregateClockView &v,int hour,int minute,int second) {
    bool changed=false;
    const int next[3]={hour,minute,second};
    for(int i=0;i<3;++i) if(v.digits[i] && lv_canvas_get_img(v.digits[i])->data && next[i]!=v.values[i]) {
        draw_pair(v.digits[i],next[i]); v.values[i]=next[i]; changed=true;
    }
    return changed;
}
bool aggregate_clock_set_text(lv_obj_t *label,const char *text) {
    if(!label || !text || std::strcmp(lv_label_get_text(label),text)==0) return false;
    lv_label_set_text(label,text); return true;
}
