// 用真实LVGL验证聚合时钟秒变化只失效秒牌，其他模块缓冲保持不变。
#include "ui_aggregate_clock_view.h"
#include "ui_aggregate_weather_policy.h"
#include "ui_aggregate_weather_texture.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdio>
LV_FONT_DECLARE(weather_icons_36);

static std::vector<lv_area_t> areas;
static void flush(lv_disp_drv_t *driver,const lv_area_t *area,lv_color_t *) {
    areas.push_back(*area); lv_disp_flush_ready(driver);
}
int main() {
    int sun_pixels=0,rain_pixels=0;
    for(int y=0;y<120;++y) for(int x=0;x<222;++x) {
        sun_pixels+=aggregate_weather_texture_pixel(1,x,y);
        rain_pixels+=aggregate_weather_texture_pixel(2,x,y);
        assert(!aggregate_weather_texture_pixel(0,x,y));
        if(y<28 || y>98) assert(!aggregate_weather_texture_pixel(2,x,y));
    }
    assert(sun_pixels>50 && rain_pixels>300);
    for(const auto &origin:{std::pair<int,int>{204,43},{184,57},{202,73},{185,85}}) {
        int pixels=0;
        for(int y=0;y<kAggregateSnowflakeSize;++y) for(int x=0;x<kAggregateSnowflakeSize;++x)
            pixels+=aggregate_weather_texture_pixel(3,origin.first+x,origin.second+y,35,180);
        assert(pixels>20);
    }
    for(int y=43;y<59;++y) for(int x=203;x<219;++x)
        assert(!aggregate_weather_texture_pixel(3,x,y,35,215));
    for(int y=85;y<99;++y) for(int x=185;x<199;++x)
        assert(!aggregate_weather_texture_pixel(3,x,y,35,192));
    for(int x=2;x<192;++x) for(int y=36;y<44;++y)
        assert(!aggregate_weather_texture_pixel(2,x,y,35));
    assert(aggregate_weather_kind(WeatherIconKind::kClear)==1);
    assert(aggregate_weather_kind(WeatherIconKind::kPartlyCloudy)==0);
    assert(aggregate_weather_kind(WeatherIconKind::kCloudy)==0);
    assert(aggregate_weather_kind(WeatherIconKind::kDrizzle)==2);
    assert(aggregate_weather_kind(WeatherIconKind::kRain)==2);
    assert(aggregate_weather_kind(WeatherIconKind::kThunderstorm)==2);
    assert(aggregate_weather_kind(WeatherIconKind::kSnow)==3);
    assert(aggregate_weather_kind(WeatherIconKind::kFog)==0);
    assert(aggregate_weather_kind(WeatherIconKind::kUnknown)==0);
    lv_init();
    lv_font_glyph_dsc_t weather_glyph;
    assert(lv_font_get_glyph_dsc(&weather_icons_36,&weather_glyph,'O',0));
    assert(weather_glyph.box_w>0 && weather_glyph.box_h>0);
    static lv_color_t display_pixels[400*300];
    static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw,display_pixels,nullptr,400*300);
    static lv_disp_drv_t driver; lv_disp_drv_init(&driver);
    driver.hor_res=400;driver.ver_res=300;driver.draw_buf=&draw;driver.flush_cb=flush;
    lv_disp_drv_register(&driver);
    static lv_color_t pixels[3][104*80];
    lv_color_t *buffers[3]={pixels[0],pixels[1],pixels[2]};
    AggregateClockView view;
    aggregate_clock_view_build(lv_scr_act(),view,buffers);
    lv_obj_update_layout(lv_scr_act());
    assert(std::strcmp(lv_label_get_text(view.local_temp),"--.-")==0);
    assert(std::strcmp(lv_label_get_text(view.local_temp_unit),"°C")==0);
    assert(aggregate_clock_view_set_local_temperature(view,true,25.3f));
    assert(std::strcmp(lv_label_get_text(view.local_temp),"25.3")==0);
    assert(std::strcmp(lv_label_get_text(view.local_temp_unit),"°C")==0);
    assert(!aggregate_clock_view_set_local_temperature(view,true,25.3f));
    assert(aggregate_clock_view_set_local_temperature(view,false,0.0f));
    assert(std::strcmp(lv_label_get_text(view.local_temp),"--.-")==0);
    assert(std::strcmp(lv_label_get_text(view.local_temp_unit),"°C")==0);
    assert(lv_obj_get_x(view.digits[0])==24);
    assert(lv_obj_get_x(view.digits[1])==148);
    assert(lv_obj_get_x(view.digits[2])==272);
    assert(lv_obj_get_x(view.separators[0][0])==135);
    assert(lv_obj_get_x(view.separators[0][1])==135);
    assert(lv_obj_get_x(view.separators[1][0])==259);
    assert(lv_obj_get_x(view.separators[1][1])==259);
    for(int i=0;i<3;++i) assert(!lv_obj_has_flag(view.digits[i],LV_OBJ_FLAG_HIDDEN));
    for(int separator=0;separator<2;++separator)
        for(int dot=0;dot<2;++dot)
            assert(!lv_obj_has_flag(view.separators[separator][dot],LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_y(view.icon)>174+40);
    assert(lv_obj_get_height(view.icon)>=lv_obj_get_style_text_font(view.icon,0)->line_height);
    for(int day=1;day<=31;++day) {
        char text[3]; std::snprintf(text,sizeof(text),"%d",day);
        aggregate_clock_set_text(view.day,text);
        lv_point_t size;
        lv_txt_get_size(&size,text,lv_obj_get_style_text_font(view.day,0),0,0,400,LV_TEXT_FLAG_NONE);
        assert(size.x<=lv_obj_get_width(view.day));
        assert(size.y<=lv_obj_get_height(view.day));
    }
    assert(aggregate_clock_view_time(view,14,36,0));
    assert(aggregate_clock_weather_theme(view,1));
    lv_refr_now(nullptr);
    std::vector<lv_color_t> hour(pixels[0],pixels[0]+104*80);
    std::vector<lv_color_t> minute(pixels[1],pixels[1]+104*80);
    areas.clear();
    assert(!aggregate_clock_view_time(view,14,36,0));
    lv_refr_now(nullptr); assert(areas.empty());
    assert(aggregate_clock_weather_theme(view,aggregate_weather_kind(WeatherIconKind::kUnknown)));
    assert(view.weather_kind==0);
    aggregate_clock_set_text(view.temperature,"-40 C");
    assert(!aggregate_clock_weather_theme(view,0));
    assert(!aggregate_clock_weather_theme(view,99));
    lv_refr_now(nullptr); areas.clear();
    assert(aggregate_clock_view_time(view,14,36,1));
    lv_refr_now(nullptr); assert(!areas.empty());
    for(const auto &area:areas) {
        std::fprintf(stderr,"second flush: %d,%d-%d,%d\n",area.x1,area.y1,area.x2,area.y2);
        // LVGL canvas reserves five pixels of transform draw margin.
        assert(area.x1>=267 && area.x2<=380);
        assert(area.y1>=71 && area.y2<=160);
    }
    assert(std::memcmp(hour.data(),pixels[0],sizeof(pixels[0]))==0);
    assert(std::memcmp(minute.data(),pixels[1],sizeof(pixels[1]))==0);
    assert(aggregate_clock_view_set_seconds_visible(view,false));
    assert(lv_obj_get_x(view.digits[0])==86);
    assert(lv_obj_get_x(view.digits[1])==210);
    assert(lv_obj_get_x(view.separators[0][0])==197);
    assert(lv_obj_get_x(view.separators[0][1])==197);
    assert(!lv_obj_has_flag(view.digits[0],LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(view.digits[1],LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(view.digits[2],LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(view.separators[0][0],LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(view.separators[0][1],LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(view.separators[1][0],LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(view.separators[1][1],LV_OBJ_FLAG_HIDDEN));
    lv_refr_now(nullptr);
    areas.clear();
    assert(!aggregate_clock_view_time(view,14,36,2));
    lv_refr_now(nullptr);
    assert(areas.empty());
    assert(std::memcmp(hour.data(),pixels[0],sizeof(pixels[0]))==0);
    assert(std::memcmp(minute.data(),pixels[1],sizeof(pixels[1]))==0);
    assert(aggregate_clock_view_set_seconds_visible(view,true));
    assert(lv_obj_get_x(view.digits[0])==24);
    assert(lv_obj_get_x(view.digits[1])==148);
    assert(lv_obj_get_x(view.digits[2])==272);
    assert(lv_obj_get_x(view.separators[0][0])==135);
    assert(lv_obj_get_x(view.separators[1][0])==259);
    for(int i=0;i<3;++i) assert(!lv_obj_has_flag(view.digits[i],LV_OBJ_FLAG_HIDDEN));
    for(int separator=0;separator<2;++separator)
        for(int dot=0;dot<2;++dot)
            assert(!lv_obj_has_flag(view.separators[separator][dot],LV_OBJ_FLAG_HIDDEN));
    lv_refr_now(nullptr);
    areas.clear();
    assert(aggregate_clock_view_time(view,14,36,2));
    lv_refr_now(nullptr);
    assert(!areas.empty());
    for(const auto &area:areas) {
        assert(area.x1>=267 && area.x2<=380);
        assert(area.y1>=71 && area.y2<=160);
    }
    assert(std::memcmp(hour.data(),pixels[0],sizeof(pixels[0]))==0);
    assert(std::memcmp(minute.data(),pixels[1],sizeof(pixels[1]))==0);
    assert(aggregate_clock_view_time(view,23,59,59));
    assert(aggregate_clock_view_time(view,0,0,0));
    assert(!aggregate_clock_view_time(view,0,0,0));
    // Simulate repeated hourly weather commits and second ticks on one long-lived page.
    lv_mem_monitor_t before,after;
    lv_mem_monitor(&before);
    for(int i=0;i<5000;++i) {
        aggregate_clock_set_text(view.temperature,i%2?"-2 C":"26 C");
        aggregate_clock_set_text(view.weather,i%2?"晴":"小雨");
        aggregate_clock_weather_theme(view,i%4);
        aggregate_clock_view_time(view,(i/3600)%24,(i/60)%60,i%60);
    }
    lv_refr_now(nullptr);
    lv_mem_monitor(&after);
    assert(after.free_size+1024>=before.free_size);
    assert(lv_mem_test()==LV_RES_OK);
    areas.clear();
    lv_refr_now(nullptr); areas.clear();
    assert(aggregate_clock_weather_theme(view,2));
    lv_refr_now(nullptr);
    for(const auto &area:areas) {std::fprintf(stderr,"theme flush: %d,%d-%d,%d\n",area.x1,area.y1,area.x2,area.y2);assert(area.y1>=169 && area.x1>=13 && area.x2<=253);}
    areas.clear();
    assert(!aggregate_clock_weather_theme(view,2));
    lv_refr_now(nullptr); assert(areas.empty());
    // Canvas allocation failure must leave recoverable objects, not missing slots.
    lv_obj_clean(lv_scr_act());
    lv_color_t *missing[3]={nullptr,nullptr,nullptr};
    aggregate_clock_view_build(lv_scr_act(),view,missing);
    assert(!aggregate_clock_view_time(view,12,34,56));
    for(int i=0;i<3;++i) {
        assert(view.digits[i]);
        lv_canvas_set_buffer(view.digits[i],buffers[i],104,80,LV_IMG_CF_TRUE_COLOR);
    }
    assert(aggregate_clock_view_time(view,12,34,56));
    // Retain a conservative eight-page object load then add settings controls.
    lv_obj_clean(lv_scr_act());
    for(int page=0;page<8;++page) {
        lv_obj_t *root=lv_obj_create(lv_scr_act());
        for(int item=0;item<60;++item) {
            lv_obj_t *text=lv_label_create(root);
            lv_label_set_text(text,"clock status 12:34:56");
        }
    }
    lv_mem_monitor_t memory; lv_mem_monitor(&memory);
    std::fprintf(stderr,"8-page object pressure: free=%u largest=%u\n",
                 (unsigned)memory.free_size,(unsigned)memory.free_biggest_size);
    assert(memory.free_biggest_size>=32768);
    lv_obj_t *settings=lv_obj_create(lv_scr_act());
    for(int i=0;i<32;++i) assert(lv_obj_create(settings));
    assert(lv_mem_test()==LV_RES_OK);
}
