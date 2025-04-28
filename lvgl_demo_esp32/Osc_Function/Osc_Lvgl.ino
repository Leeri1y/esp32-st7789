#include <lvgl.h>
#include <Ticker.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

// 添加版本检查宏定义，确保不同版本兼容性
#ifndef LV_VERSION_CHECK
    #define LV_VERSION_CHECK(x,y,z) 0
#endif

// 触摸屏配置
#define CS_PIN  17  // 触摸屏芯片选择引脚
#define IRQ_PIN 16  // 触摸屏中断引脚
#define LVGL_TICK_PERIOD 20  // LVGL定时器周期
#define TFT_BL 21    // 添加TFT背光引脚定义

// 输入引脚定义
#define ADC_PIN    34   // 模拟输入引脚
#define DIGITAL_PIN 14  // 数字输入引脚

// 显示参数
const uint16_t WIDTH = 320;
const uint16_t HEIGHT = 240;
const uint16_t GRID_COLOR = TFT_DARKGREY;
const uint16_t BG_COLOR = TFT_BLACK;
const uint16_t WAVE_COLOR = TFT_GREEN;
const uint16_t DIGITAL_COLOR = TFT_CYAN;
const uint16_t TEXT_COLOR = TFT_GREEN;
const uint16_t PAUSE_COLOR = TFT_YELLOW;

// 时基配置（ms/div）
const float timebases[] = {0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 20.0};
uint8_t timebaseIndex = 3;
float currentTimebase = timebases[timebaseIndex];
bool digitalMode = false;  // 工作模式切换标志

// 波形幅度放缩参数
float amplitudeScale = 1.0;
const float scaleSteps[] = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0};
uint8_t scaleIndex = 3;

// 采样配置
const uint16_t MAX_SAMPLES = 2000;
uint16_t samplesBuffer[MAX_SAMPLES];
uint16_t sampleIndex = 0;
uint32_t lastSampleTime = 0;
float samplingFreq = 0;

// 显示区域参数 - 更新布局尺寸
const uint16_t RIGHT_PANEL_WIDTH = 70;    // 右侧控制面板宽度
const uint16_t TOP_PANEL_HEIGHT = 35;     // 顶部控制面板高度
const uint16_t BOTTOM_INFO_HEIGHT = 30;   // 底部信息显示区域高度

const uint16_t GRAPH_TOP = TOP_PANEL_HEIGHT;  
const uint16_t GRAPH_WIDTH = WIDTH - RIGHT_PANEL_WIDTH;
const uint16_t GRAPH_HEIGHT = HEIGHT - TOP_PANEL_HEIGHT - BOTTOM_INFO_HEIGHT;
const uint16_t GRAPH_BOTTOM = GRAPH_TOP + GRAPH_HEIGHT;
const uint16_t GRAPH_RIGHT = GRAPH_WIDTH;

// 光标测量相关变量
bool cursorMode = false;
uint16_t cursor1Pos = 80;
uint16_t cursor2Pos = 200;
bool cursor1Selected = true;
const uint16_t CURSOR1_COLOR = TFT_CYAN;
const uint16_t CURSOR2_COLOR = TFT_ORANGE;

// 频率检测变量
float detectedFrequency = 0.0;
float measuredFrequency = 0.0;
uint32_t lastZeroCrossTime = 0;
bool lastSignalState = false;
const uint16_t visibleSamples = 300;

// 暂停和滚动功能变量
bool isPaused = false;
int scrollOffset = 0;
uint16_t pausedSampleIndex = 0;

// LVGL UI组件
lv_obj_t *waveView;        // 波形显示区域
lv_obj_t *controlPanel;    // 控制面板区域
lv_obj_t *modeSwitch;      // 模式切换开关
lv_obj_t *pauseBtn;        // 暂停按钮
lv_obj_t *cursorSwitch;    // 光标模式开关
lv_obj_t *timebaseSlider;  // 时基调节滑块
lv_obj_t *ampSlider;       // 幅度调节滑块
lv_obj_t *cursorSelect;    // 光标选择下拉列表
lv_obj_t *cursorSlider;    // 光标位置滑块
lv_obj_t *scrollBtns;      // 滚动按钮组

// LVGL和TFT实例
Ticker tick;
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite waveSprite = TFT_eSprite(&tft);
XPT2046_Touchscreen ts(CS_PIN, IRQ_PIN);

    // 消抖相关变量
    uint32_t last_touch_time = 0;
    const uint16_t CLICK_DEBOUNCE_MS = 30;  // 单击消抖时间
    bool is_sliding = false;                // 标记是否在滑动

    // Calibration variables
    int16_t cal_x_min = 200, cal_x_max = 3900; // Raw X min/max from touchscreen
    int16_t cal_y_min = 200, cal_y_max = 3800; // Raw Y min/max from touchscreen

// LVGL缓冲区
static lv_disp_buf_t disp_buf;
static lv_color_t buf[LV_HOR_RES_MAX * 10];

// 主题色彩定义
const uint16_t TITLE_BG_COLOR = 0x6E7B;  // 柔和灰蓝色
const uint16_t PANEL_BG_COLOR = 0xEF7D;  // 浅灰色
const uint16_t ACCENT_COLOR = 0x05FF;    // 绿松石色
const uint16_t TEXT_PRIMARY_COLOR = 0x0000; // 黑色文本
const uint16_t CONTROL_BG_COLOR = 0xBDF7; // 浅蓝灰色

// LVGL显示刷新回调
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint16_t c;
    
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, (area->x2 - area->x1 + 1), (area->y2 - area->y1 + 1));
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            c = color_p->full;
            tft.writeColor(c, 1);
            color_p++;
        }
    }
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

// LVGL定时器回调
static void lv_tick_handler(void)
{
    lv_tick_inc(LVGL_TICK_PERIOD);
}

// 触摸屏读取回调
bool my_touchpad_read(lv_indev_drv_t *indev, lv_indev_data_t *data) {
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    uint32_t current_time = millis();

    bool is_touched = ts.touched();
    data->state = is_touched ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;

    if (is_touched) {
        TS_Point p = ts.getPoint();
        last_x = map(p.x, cal_x_min, cal_x_max, 0, 320);
        last_y = map(p.y, cal_y_max, cal_y_min, 0, 240);
        last_x = constrain(last_x, 0, 320);
        last_y = constrain(last_y, 0, 240);

        // 检测滑动：如果坐标变化较大，认为是滑动
        static lv_coord_t prev_x = last_x, prev_y = last_y;
        if (abs(last_x - prev_x) > 5 || abs(last_y - prev_y) > 5) {
            is_sliding = true;
        }
        prev_x = last_x;
        prev_y = last_y;

        // 滑动时直接更新坐标，不消抖
        if (is_sliding) {
            Serial.print("Sliding: ");
            Serial.print(last_x); Serial.print(", "); Serial.println(last_y);
        } 
        // 单击时消抖
        else if (current_time - last_touch_time >= CLICK_DEBOUNCE_MS) {
            Serial.print("Clicked: ");
            Serial.print(last_x); Serial.print(", "); Serial.println(last_y);
            last_touch_time = current_time;
        } else {
            data->state = LV_INDEV_STATE_REL; // 消抖期内忽略
        }
    } else {
        is_sliding = false; // 释放时重置滑动状态
    }

    data->point.x = last_x;
    data->point.y = last_y;
    return false;
}

// 初始化触摸屏和TFT
void tft_touch_init()
{
    ts.begin();
    tft.begin();
    tft.setRotation(1); // 横向显示
    
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
}

// 初始化LVGL库
void lvgl_setup()
{
    lv_init();
    
    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * 10);
    
    // 初始化显示驱动
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LV_HOR_RES_MAX;
    disp_drv.ver_res = LV_VER_RES_MAX;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);
    
    // 初始化触摸驱动
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);
    
    // 定时器初始化
    tick.attach_ms(LVGL_TICK_PERIOD, lv_tick_handler);
}

// UI事件回调函数
static void mode_switch_event_cb(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_VALUE_CHANGED) {
        #if LV_VERSION_CHECK(7, 0, 0)
            digitalMode = lv_switch_get_state(obj);
        #else
            digitalMode = lv_sw_get_state(obj);
        #endif
        Serial.print("模式切换: ");
        Serial.println(digitalMode ? "数字" : "模拟");
    }
}

static void cursor_switch_event_cb(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_VALUE_CHANGED) {
        // 获取开关状态
        #if LV_VERSION_CHECK(7, 0, 0)
            cursorMode = lv_switch_get_state(obj);
        #else
            cursorMode = lv_sw_get_state(obj);
        #endif
        
        Serial.print("光标模式: ");
        Serial.println(cursorMode ? "开启" : "关闭");
        
        // 显示或隐藏光标相关控件
        static lv_obj_t *cursorControls[3] = {NULL, NULL, NULL};
        
        // 获取光标控件
        if(cursorControls[0] == NULL) {
            lv_obj_t *parent = lv_obj_get_parent(obj);
            // 找到所有子对象
            lv_obj_t *child = lv_obj_get_child(parent, NULL);
            int count = 0;
            
            while(child && count < 3) {
                // 跳过光标模式标签和开关本身
                if(child != obj && child != lv_obj_get_child(parent, NULL)) {
                    cursorControls[count++] = child;
                }
                child = lv_obj_get_child(parent, child);
            }
        }
        
        // 设置所有光标控件的可见性
        for(int i = 0; i < 3; i++) {
            if(cursorControls[i]) {
                lv_obj_set_hidden(cursorControls[i], !cursorMode);
            }
        }
        
        // 更新波形显示
        updateWaveform();
    }
}

static void pause_btn_event_cb(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_CLICKED) {
        isPaused = !isPaused;
        if (isPaused) {
            pausedSampleIndex = sampleIndex;
            #if LV_VERSION_CHECK(7, 0, 0)
                lv_btn_set_state(pauseBtn, LV_BTN_STATE_CHECKED_RELEASED);
            #else
                lv_btn_set_state(pauseBtn, LV_BTN_STATE_TGL_REL);
            #endif
            Serial.println("暂停采集");
            
            // 显示滚动按钮
            lv_obj_set_hidden(scrollBtns, false);
        } else {
            #if LV_VERSION_CHECK(7, 0, 0)
                lv_btn_set_state(pauseBtn, LV_BTN_STATE_RELEASED);
            #else
                lv_btn_set_state(pauseBtn, LV_BTN_STATE_REL);
            #endif
            Serial.println("继续采集");
            
            // 隐藏滚动按钮
            lv_obj_set_hidden(scrollBtns, true);
        }
    }
}

static void timebase_roller_event_cb(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_VALUE_CHANGED) {
        timebaseIndex = lv_roller_get_selected(obj);
        currentTimebase = timebases[timebaseIndex];
        // 直接更新波形显示，确保时基变化即时生效
        updateWaveform();
        Serial.print("时基调整: ");
        Serial.print(currentTimebase);
        Serial.println(" ms/div");
    }
}

static void amp_roller_event_cb(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_VALUE_CHANGED) {
        scaleIndex = lv_roller_get_selected(obj);
        amplitudeScale = scaleSteps[scaleIndex];
        // 直接更新波形显示，确保幅度变化即时生效
        updateWaveform();
        Serial.print("幅度调整: ");
        Serial.println(amplitudeScale);
    }
}

static void cursor_select_event_cb(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_VALUE_CHANGED) {
        #if LV_VERSION_CHECK(7, 0, 0)
            cursor1Selected = (lv_dropdown_get_selected(obj) == 0);
        #else
            cursor1Selected = (lv_ddlist_get_selected(obj) == 0);
        #endif
        Serial.print("选择光标: ");
        Serial.println(cursor1Selected ? "1" : "2");
        
        // 更新光标滑块位置
        lv_slider_set_value(cursorSlider, 
                          cursor1Selected ? cursor1Pos : cursor2Pos, 
                          LV_ANIM_OFF);
    }
}

static void cursor_slider_event_cb(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_VALUE_CHANGED) {
        uint16_t pos = lv_slider_get_value(obj);
        if (cursor1Selected) {
            cursor1Pos = pos;
            Serial.print("光标1位置: ");
        } else {
            cursor2Pos = pos;
            Serial.print("光标2位置: ");
        }
        Serial.println(pos);
        
        // 更新光标位置显示标签
        // 使用不依赖lv_obj_get_type的方法查找标签
        static lv_obj_t *cursorPosLabel = NULL;
        
        // 如果没有找到标签，直接创建一个新的
        if (!cursorPosLabel) {
            lv_obj_t *parent = lv_obj_get_parent(obj);
            cursorPosLabel = lv_label_create(parent, NULL);
            lv_obj_align(cursorPosLabel, obj, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
        }
        
        // 更新标签文本
        char buf[20];
        sprintf(buf, "Pos: %d", pos);
        lv_label_set_text(cursorPosLabel, buf);
        
        // 重新计算光标频率
        calculateMeasuredFrequency();
    }
}

// 按钮连续按压的处理
static void scroll_btn_event_cb(lv_obj_t *obj, lv_event_t event)
{
    // 在暂停模式下使滚动更流畅
    if (isPaused) {
        // 判断是左滚动还是右滚动
        bool isLeftBtn = (obj == lv_obj_get_child(scrollBtns, 0));
        
        // 检测按钮按下或持续按下状态
        if (event == LV_EVENT_PRESSED || event == LV_EVENT_PRESSING || event == LV_EVENT_LONG_PRESSED) {
            // 快速滚动，取消消抖
            if (isLeftBtn) {
                // 向左滚动（向前看波形）
                scrollOffset = min(scrollOffset + 5, MAX_SAMPLES - visibleSamples);
            } else {
                // 向右滚动（向后看波形）
                scrollOffset = max(scrollOffset - 5, 0);
            }
            // 立即更新波形显示
            updateWaveform();
        }
    }
}

// 修改欢迎消息框部分，添加关闭回调函数
static void welcome_mbox_event_cb(lv_obj_t *obj, lv_event_t event) {
    if (event == LV_EVENT_VALUE_CHANGED) {
        // 隐藏消息框
        lv_obj_del(obj);
        // 显示波形区域
        lv_obj_set_hidden(waveView, false);
    }
}

// 创建UI界面
void create_ui()
{
    // 设置主题 - 使用更明亮的主题
    lv_theme_t *th = lv_theme_night_init(210, NULL);
    lv_theme_set_current(th);
    
    // 创建一个全屏容器作为基础
    lv_obj_t *mainContainer = lv_cont_create(lv_scr_act(), NULL);
    lv_obj_set_size(mainContainer, WIDTH, HEIGHT);
    lv_obj_set_pos(mainContainer, 0, 0);
    
    // 设置容器样式为透明
    static lv_style_t style_transparent;
    lv_style_copy(&style_transparent, &lv_style_plain);
    style_transparent.body.main_color = LV_COLOR_WHITE;
    style_transparent.body.grad_color = LV_COLOR_WHITE;
    style_transparent.body.opa = LV_OPA_TRANSP;
    style_transparent.body.border.width = 0;
    lv_obj_set_style(mainContainer, &style_transparent);
    
    // 1. 创建顶部控制面板
    controlPanel = lv_cont_create(mainContainer, NULL);
    lv_obj_set_size(controlPanel, WIDTH, TOP_PANEL_HEIGHT);
    lv_obj_set_pos(controlPanel, 0, 0);
    
    // 设置顶部面板样式
    static lv_style_t style_top_panel;
    lv_style_copy(&style_top_panel, &lv_style_plain);
    style_top_panel.body.main_color = lv_color_hex(TITLE_BG_COLOR);
    style_top_panel.body.grad_color = lv_color_hex(TITLE_BG_COLOR);
    style_top_panel.body.radius = 0;
    style_top_panel.body.border.width = 0;
    style_top_panel.text.color = lv_color_hex(TEXT_PRIMARY_COLOR);
    lv_obj_set_style(controlPanel, &style_top_panel);
    
    // 2. 创建右侧控制面板
    lv_obj_t *rightPanel = lv_cont_create(mainContainer, NULL);
    lv_obj_set_size(rightPanel, RIGHT_PANEL_WIDTH, HEIGHT - TOP_PANEL_HEIGHT);
    lv_obj_set_pos(rightPanel, GRAPH_WIDTH, TOP_PANEL_HEIGHT);
    
    // 设置右侧面板样式
    static lv_style_t style_right_panel;
    lv_style_copy(&style_right_panel, &lv_style_plain);
    style_right_panel.body.main_color = lv_color_hex(PANEL_BG_COLOR);
    style_right_panel.body.grad_color = lv_color_hex(PANEL_BG_COLOR);
    style_right_panel.body.radius = 0;
    style_right_panel.body.border.width = 0;
    style_right_panel.text.color = lv_color_hex(TEXT_PRIMARY_COLOR);
    lv_obj_set_style(rightPanel, &style_right_panel);
    
    // 3. 创建波形显示区域 - 作为一个容器，而不是直接绘制
    waveView = lv_obj_create(mainContainer, NULL);
    lv_obj_set_size(waveView, GRAPH_WIDTH, GRAPH_HEIGHT);
    lv_obj_set_pos(waveView, 0, GRAPH_TOP);
    
    // 波形区域样式
    static lv_style_t style_bg;
    lv_style_copy(&style_bg, &lv_style_plain);
    style_bg.body.main_color = LV_COLOR_BLACK;
    style_bg.body.grad_color = LV_COLOR_BLACK;
    style_bg.body.radius = 0;
    style_bg.body.border.width = 0;
    lv_obj_set_style(waveView, &style_bg);
    
    // 重要：设置波形显示区域的z索引较低，确保其他UI元素不会被覆盖
    lv_obj_set_top(waveView, false);
    
    // 4. 创建底部信息区域
    lv_obj_t *bottomInfoPanel = lv_cont_create(mainContainer, NULL);
    lv_obj_set_size(bottomInfoPanel, WIDTH, BOTTOM_INFO_HEIGHT);
    lv_obj_set_pos(bottomInfoPanel, 0, HEIGHT - BOTTOM_INFO_HEIGHT);
    
    // 重要：确保底部信息面板在波形显示区域之上
    lv_obj_set_top(bottomInfoPanel, true);
    
    // 设置底部信息面板样式
    static lv_style_t style_bottom_panel;
    lv_style_copy(&style_bottom_panel, &style_right_panel);
    style_bottom_panel.body.main_color = lv_color_hex(PANEL_BG_COLOR);
    style_bottom_panel.body.grad_color = lv_color_hex(PANEL_BG_COLOR);
    lv_obj_set_style(bottomInfoPanel, &style_bottom_panel);
    
    // 创建标题
    lv_obj_t *title = lv_label_create(controlPanel, NULL);
    lv_label_set_text(title, "ESP32 Osc");
    lv_obj_align(title, controlPanel, LV_ALIGN_IN_LEFT_MID, 10, 0);
    
    // 控制区样式 - 应用于所有按钮和控制元素
    static lv_style_t style_control;
    lv_style_copy(&style_control, &lv_style_pretty);
    style_control.body.main_color = lv_color_hex(CONTROL_BG_COLOR);
    style_control.body.grad_color = lv_color_hex(CONTROL_BG_COLOR);
    style_control.body.radius = 4;
    style_control.body.border.color = lv_color_hex(ACCENT_COLOR);
    style_control.body.border.width = 1;
    style_control.text.color = lv_color_hex(TEXT_PRIMARY_COLOR);
    
    // 创建模式标签和开关 - 放在顶部面板
    lv_obj_t *modeLabel = lv_label_create(controlPanel, NULL);
    lv_label_set_text(modeLabel, "DS/AS");
    lv_obj_align(modeLabel, title, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
    
    // 创建模式开关
    #if LV_VERSION_CHECK(7, 0, 0)
        modeSwitch = lv_switch_create(controlPanel, NULL);
        lv_switch_off(modeSwitch, LV_ANIM_OFF);
    #else
        modeSwitch = lv_sw_create(controlPanel, NULL);
        lv_sw_off(modeSwitch, LV_ANIM_OFF);
    #endif
    
    lv_obj_set_event_cb(modeSwitch, mode_switch_event_cb);
    lv_obj_align(modeSwitch, modeLabel, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    
    // 创建暂停按钮 - 放在顶部面板右侧
    pauseBtn = lv_btn_create(controlPanel, NULL);
    lv_obj_set_size(pauseBtn, 60, 25);
    lv_obj_set_style(pauseBtn, &style_control);
    lv_obj_set_event_cb(pauseBtn, pause_btn_event_cb);
    lv_obj_align(pauseBtn, controlPanel, LV_ALIGN_IN_RIGHT_MID, -10, 0);
    
    #if LV_VERSION_CHECK(7, 0, 0)
        lv_btn_set_checkable(pauseBtn, true);
    #else
        lv_btn_set_toggle(pauseBtn, true);
    #endif
    
    lv_obj_t *pauseLabel = lv_label_create(pauseBtn, NULL);
    lv_label_set_text(pauseLabel, "Pause");
    
    // 在右侧面板创建标签页 - 用于不同的控制组
    lv_obj_t *tabview = lv_tabview_create(rightPanel, NULL);
    lv_obj_set_size(tabview, RIGHT_PANEL_WIDTH, HEIGHT - TOP_PANEL_HEIGHT - 5);
    lv_obj_align(tabview, rightPanel, LV_ALIGN_IN_TOP_MID, 0, 2);
    
    // 确保标签页在波形区之上
    lv_obj_set_top(tabview, true);
    
    // 创建三个标签页：时基、幅度、光标
    lv_obj_t *timeTab = lv_tabview_add_tab(tabview, "T");
    lv_obj_t *ampTab = lv_tabview_add_tab(tabview, "A");
    lv_obj_t *cursorTab = lv_tabview_add_tab(tabview, "C");
    
    // 标签页内容样式
    static lv_style_t style_tab_panel;
    lv_style_copy(&style_tab_panel, &lv_style_plain);
    style_tab_panel.body.main_color = lv_color_hex(PANEL_BG_COLOR);
    style_tab_panel.body.grad_color = lv_color_hex(PANEL_BG_COLOR);
    style_tab_panel.body.padding.left = 5;
    style_tab_panel.body.padding.right = 5;
    style_tab_panel.body.padding.top = 5;
    style_tab_panel.body.padding.bottom = 5;
    
    // 为时基标签页添加控制元素
    lv_obj_t *timebaseLabel = lv_label_create(timeTab, NULL);
    lv_label_set_text(timebaseLabel, "TimeSet");
    lv_obj_align(timebaseLabel, timeTab, LV_ALIGN_IN_TOP_MID, 0, 10);
    
    // 创建时基选择器滚轮
    lv_obj_t *timebaseRoller = lv_roller_create(timeTab, NULL);
    // 准备时基选项
    char timebase_opts[128] = {0};
    for (int i = 0; i < sizeof(timebases) / sizeof(timebases[0]); i++) {
        char buf[16];
        sprintf(buf, "%.1fms/div", timebases[i]);
        if (i > 0) strcat(timebase_opts, "\n");
        strcat(timebase_opts, buf);
    }
    lv_roller_set_options(timebaseRoller, timebase_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(timebaseRoller, timebaseIndex, LV_ANIM_OFF);
    lv_obj_set_event_cb(timebaseRoller, timebase_roller_event_cb);
    lv_obj_align(timebaseRoller, timebaseLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_set_width(timebaseRoller, RIGHT_PANEL_WIDTH - 20);
    
    // 为幅度标签页添加控制元素
    lv_obj_t *ampLabel = lv_label_create(ampTab, NULL);
    lv_label_set_text(ampLabel, "AmpSet");
    lv_obj_align(ampLabel, ampTab, LV_ALIGN_IN_TOP_MID, 0, 10);
    
    // 创建幅度选择器滚轮
    lv_obj_t *ampRoller = lv_roller_create(ampTab, NULL);
    // 准备幅度选项
    char amp_opts[128] = {0};
    for (int i = 0; i < sizeof(scaleSteps) / sizeof(scaleSteps[0]); i++) {
        char buf[16];
        sprintf(buf, "%.2fx", scaleSteps[i]);
        if (i > 0) strcat(amp_opts, "\n");
        strcat(amp_opts, buf);
    }
    lv_roller_set_options(ampRoller, amp_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(ampRoller, scaleIndex, LV_ANIM_OFF);
    lv_obj_set_event_cb(ampRoller, amp_roller_event_cb);
    lv_obj_align(ampRoller, ampLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_set_width(ampRoller, RIGHT_PANEL_WIDTH - 20);
    
    // 为光标标签页添加控制元素
    lv_obj_t *cursorModeLabel = lv_label_create(cursorTab, NULL);
    lv_label_set_text(cursorModeLabel, "Cursor");
    lv_obj_align(cursorModeLabel, cursorTab, LV_ALIGN_IN_TOP_MID, 0, 5);
    
    // 创建光标开关并上移
    #if LV_VERSION_CHECK(7, 0, 0)
        cursorSwitch = lv_switch_create(cursorTab, NULL);
        lv_switch_off(cursorSwitch, LV_ANIM_OFF);
    #else
        cursorSwitch = lv_sw_create(cursorTab, NULL);
        lv_sw_off(cursorSwitch, LV_ANIM_OFF);
    #endif
    
    lv_obj_set_event_cb(cursorSwitch, cursor_switch_event_cb);
    lv_obj_align(cursorSwitch, cursorModeLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    
    // 创建光标选择下拉菜单并上移
    #if LV_VERSION_CHECK(7, 0, 0)
        cursorSelect = lv_dropdown_create(cursorTab, NULL);
        lv_dropdown_set_options(cursorSelect, "Cur 1\nCur 2");
    #else
        cursorSelect = lv_ddlist_create(cursorTab, NULL);
        lv_ddlist_set_options(cursorSelect, "Cur 1\nCur 2");
    #endif
    
    lv_obj_set_width(cursorSelect, RIGHT_PANEL_WIDTH - 20);
    lv_obj_align(cursorSelect, cursorSwitch, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_obj_set_event_cb(cursorSelect, cursor_select_event_cb);
    
    // 将光标位置滑块大幅上移，避免被任何元素遮挡
    cursorSlider = lv_slider_create(cursorTab, NULL);
    lv_obj_set_size(cursorSlider, RIGHT_PANEL_WIDTH - 20, 20);
    // 此处大幅调整位置，上移到标签页的上部位置
    lv_obj_align(cursorSlider, cursorSelect, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_obj_set_event_cb(cursorSlider, cursor_slider_event_cb);
    lv_slider_set_range(cursorSlider, 0, GRAPH_WIDTH - 1);
    lv_slider_set_value(cursorSlider, cursor1Pos, LV_ANIM_OFF);
    
    // 添加光标值显示标签
    lv_obj_t *cursorValLabel = lv_label_create(cursorTab, NULL);
    lv_label_set_text(cursorValLabel, "Pos: -");
    lv_obj_align(cursorValLabel, cursorSlider, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    
    // 隐藏光标控制，直到光标模式打开
    lv_obj_set_hidden(cursorSelect, true);
    lv_obj_set_hidden(cursorSlider, true);
    lv_obj_set_hidden(cursorValLabel, true);
    
    // 创建滚动按钮，隐藏起来直到暂停模式激活
    scrollBtns = lv_cont_create(bottomInfoPanel, NULL);
    lv_obj_set_size(scrollBtns, 120, 25);
    lv_obj_align(scrollBtns, bottomInfoPanel, LV_ALIGN_IN_RIGHT_MID, -10, 0);
    lv_obj_set_style(scrollBtns, &style_transparent);
    lv_obj_set_hidden(scrollBtns, true);
    
    // 确保滚动按钮在波形区之上
    lv_obj_set_top(scrollBtns, true);
    
    // 左滚动按钮
    lv_obj_t *leftBtn = lv_btn_create(scrollBtns, NULL);
    lv_obj_set_size(leftBtn, 50, 20);
    lv_obj_set_style(leftBtn, &style_control);
    lv_obj_set_event_cb(leftBtn, scroll_btn_event_cb);
    lv_obj_t *leftLabel = lv_label_create(leftBtn, NULL);
    lv_label_set_text(leftLabel, "L");
    lv_obj_align(leftBtn, scrollBtns, LV_ALIGN_IN_LEFT_MID, 0, 0);
    
    // 右滚动按钮
    lv_obj_t *rightBtn = lv_btn_create(scrollBtns, NULL);
    lv_obj_set_size(rightBtn, 50, 20);
    lv_obj_set_style(rightBtn, &style_control);
    lv_obj_set_event_cb(rightBtn, scroll_btn_event_cb);
    lv_obj_t *rightLabel = lv_label_create(rightBtn, NULL);
    lv_label_set_text(rightLabel, "R");
    lv_obj_align(rightBtn, scrollBtns, LV_ALIGN_IN_RIGHT_MID, 0, 0);
    
    // 创建信息显示标签
    static lv_obj_t *infoLabel = lv_label_create(bottomInfoPanel, NULL);
    lv_obj_align(infoLabel, bottomInfoPanel, LV_ALIGN_IN_LEFT_MID, 10, 0);
    lv_label_set_text(infoLabel, ".");
    
    // 确保信息标签在波形区之上
    lv_obj_set_top(infoLabel, true);
    
    // 创建欢迎消息框
    lv_obj_t *welcome_mbox = lv_mbox_create(lv_scr_act(), NULL);
    lv_mbox_set_text(welcome_mbox, "ESP32 Oscilloscope\n\nControls:\n- T tab: Time base setting\n- A tab: Amplitude setting\n- C tab: Cursor measurement\n\nTap OK to start");
    
    // 添加按钮
    static const char * mbox_btns[] = {"OK", ""};
    lv_mbox_add_btns(welcome_mbox, mbox_btns);
    lv_obj_set_event_cb(welcome_mbox, welcome_mbox_event_cb);
    
    // 居中显示
    lv_obj_set_width(welcome_mbox, WIDTH - 40);
    lv_obj_align(welcome_mbox, NULL, LV_ALIGN_CENTER, 0, 0);
    
    // 欢迎框必须在最顶层
    lv_obj_set_top(welcome_mbox, true);
    
    // 隐藏波形区域，直到欢迎消息框关闭
    lv_obj_set_hidden(waveView, true);
}

// 计算采样频率
void calculateSamplingFreq() {
    static uint32_t lastCalcTime = 0;
    static uint16_t lastSampleCount = 0;
    
    if(millis() - lastCalcTime >= 500) {
        uint16_t newSamples = (sampleIndex - lastSampleCount) % MAX_SAMPLES;
        samplingFreq = (newSamples * 1000.0) / (millis() - lastCalcTime);
        lastCalcTime = millis();
        lastSampleCount = sampleIndex;
    }
}

// 采样函数
void takeSample() {
    const uint8_t SMOOTHING = 4;
    static uint16_t analogBuffer[4] = {0};
    static uint8_t bufIndex = 0;
    
    if(!digitalMode) {
        // 模拟信号采样
        uint16_t rawSample = analogRead(ADC_PIN);
        
        // 平滑滤波
        analogBuffer[bufIndex] = rawSample;
        bufIndex = (bufIndex + 1) % SMOOTHING;
        
        uint32_t sum = 0;
        for(uint8_t i=0; i<SMOOTHING; i++) {
            sum += analogBuffer[i];
        }
        samplesBuffer[sampleIndex] = sum / SMOOTHING;
    } else {
        // 数字信号采样
        static bool prevState = false;
        static uint8_t stableCount = 0;
        static uint8_t transitionCount = 0;
        
        bool currentState = digitalRead(DIGITAL_PIN);
        
        // 检测状态变化
        if(currentState != prevState) {
            transitionCount++;
            if(transitionCount >= 2) {
                // 设置过渡值
                samplesBuffer[sampleIndex] = currentState ? (2048 + 200) : (2048 - 200);
                
                prevState = currentState;
                transitionCount = 0;
                stableCount = 0;
            }
        } else {
            transitionCount = 0;
            stableCount++;
            
            if(stableCount >= 2) {
                samplesBuffer[sampleIndex] = currentState ? 4095 : 0;
            }
        }
    }
    
    // 更新采样索引
    sampleIndex = (sampleIndex + 1) % MAX_SAMPLES;
}

// 频率检测
void detectFrequency() {
    static uint32_t periodSum = 0;
    static uint8_t periodCount = 0;
    static uint16_t lastValue = 2048;
    static bool risingTrigger = false;
    const uint8_t NUM_PERIODS = 4;
    
    uint16_t currentValue = samplesBuffer[(sampleIndex - 1) % MAX_SAMPLES];
    uint16_t triggerLevel = 2048;
    
    // 检测上升沿过零
    if(!risingTrigger && lastValue < triggerLevel && currentValue >= triggerLevel) {
        static uint32_t lastCrossTime = 0;
        uint32_t currentTime = micros();
        
        if(lastCrossTime > 0) {
            uint32_t period = currentTime - lastCrossTime;
            
            if(period > 500 && period < 1000000) {
                periodSum += period;
                periodCount++;
            }
        }
        
        lastCrossTime = currentTime;
        risingTrigger = true;
    } 
    else if(risingTrigger && currentValue < triggerLevel - 100) {
        risingTrigger = false;
    }
    
    // 计算频率
    if(periodCount >= NUM_PERIODS) {
        detectedFrequency = 1000000.0 / (periodSum / periodCount);
        periodSum = 0;
        periodCount = 0;
    }
    
    lastValue = currentValue;
}

// 计算光标测量频率
void calculateMeasuredFrequency() {
    if(cursorMode && cursor1Pos != cursor2Pos) {
        float timeDiff = abs(cursor1Pos - cursor2Pos) * (currentTimebase / 32.0);
        
        if(timeDiff > 0) {
            measuredFrequency = 1000.0 / timeDiff;
        } else {
            measuredFrequency = 0;
        }
    }
}

// 更新波形显示
void updateWaveform() {
    // 计算起始索引
    uint16_t startIndex;
    if(isPaused) {
        startIndex = (pausedSampleIndex >= visibleSamples + scrollOffset) ? 
                      pausedSampleIndex - visibleSamples - scrollOffset : 
                      MAX_SAMPLES - (visibleSamples + scrollOffset - pausedSampleIndex);
    } else {
        startIndex = (sampleIndex >= visibleSamples) ? 
                      sampleIndex - visibleSamples : 
                      MAX_SAMPLES - (visibleSamples - sampleIndex);
        scrollOffset = 0;
    }
    
    // 获取波形颜色
    uint16_t waveColor = digitalMode ? DIGITAL_COLOR : (isPaused ? PAUSE_COLOR : WAVE_COLOR);
    
    // 使用精灵缓冲区绘制，而不是直接在屏幕上绘制
    waveSprite.fillSprite(BG_COLOR);
    
    // 绘制网格
    for(uint16_t x=0; x<GRAPH_WIDTH; x+=GRAPH_WIDTH/10) {
        if(x % (GRAPH_WIDTH/5) == 0) {
            waveSprite.drawFastVLine(x, 0, GRAPH_HEIGHT, GRID_COLOR);
        } else {
            for(uint16_t y=0; y<GRAPH_HEIGHT; y+=6) {
                waveSprite.drawPixel(x, y, GRID_COLOR);
            }
        }
    }
    
    for(uint16_t y=0; y<GRAPH_HEIGHT; y+=GRAPH_HEIGHT/8) {
        if(y % (GRAPH_HEIGHT/4) == 0) {
            waveSprite.drawFastHLine(0, y, GRAPH_WIDTH, GRID_COLOR);
        } else {
            for(uint16_t x=0; x<GRAPH_WIDTH; x+=6) {
                waveSprite.drawPixel(x, y, GRID_COLOR);
            }
        }
    }
    
    // 波形绘制
    if (digitalMode) {
        // 数字信号绘制
        bool firstPoint = true;
        uint16_t prevX = 0, prevY = 0, prevValue = 0;
        
        uint16_t pointsToShow = min(visibleSamples, (uint16_t)(GRAPH_WIDTH / (currentTimebase * 0.32)));
        float xStep = (float)GRAPH_WIDTH / pointsToShow;
        
        for(uint16_t i=0; i<pointsToShow; i++) {
            uint16_t x = i * xStep;
            if(x >= GRAPH_WIDTH) break;
            
            uint16_t bufferIndex = (startIndex + i) % MAX_SAMPLES;
            uint16_t value = samplesBuffer[bufferIndex];
            bool isHigh = value > 2048;
            
            uint16_t y = isHigh ? GRAPH_HEIGHT * 0.2 : GRAPH_HEIGHT * 0.8;
            
            if(!firstPoint) {
                bool prevIsHigh = prevValue > 2048;
                
                if(prevIsHigh != isHigh) {
                    uint16_t transitionX = prevX + (x - prevX) / 2;
                    
                    waveSprite.drawLine(prevX, prevY, transitionX, prevY, waveColor);
                    
                    if(isHigh) {
                        waveSprite.drawLine(transitionX, prevY, transitionX + 1, prevY - 2, waveColor);
                        waveSprite.drawLine(transitionX + 1, prevY - 2, transitionX + 2, y, waveColor);
                    } else {
                        waveSprite.drawLine(transitionX, prevY, transitionX + 1, prevY + 2, waveColor);
                        waveSprite.drawLine(transitionX + 1, prevY + 2, transitionX + 2, y, waveColor);
                    }
                    
                    prevX = transitionX + 2;
                    prevY = y;
                } else {
                    waveSprite.drawLine(prevX, prevY, x, y, waveColor);
                    prevX = x;
                    prevY = y;
                }
            } else {
                firstPoint = false;
                prevX = x;
                prevY = y;
            }
            
            prevValue = value;
        }
    } else {
        // 模拟信号绘制
        bool firstPoint = true;
        uint16_t prevY = 0;
        
        uint16_t pointsToShow = min(visibleSamples, (uint16_t)(GRAPH_WIDTH / (currentTimebase * 0.32)));
        float xStep = (float)GRAPH_WIDTH / pointsToShow;
        
        for(uint16_t i=0; i<pointsToShow; i++) {
            uint16_t x = i * xStep;
            if(x >= GRAPH_WIDTH) break;
            
            uint16_t bufferIndex = (startIndex + i) % MAX_SAMPLES;
            
            // 应用幅度缩放
            uint16_t value = samplesBuffer[bufferIndex];
            int32_t centered = value - 2048;
            centered = centered * amplitudeScale;
            centered = constrain(centered, -2048, 2047);
            value = centered + 2048;
            
            uint16_t y = map(value, 0, 4095, GRAPH_HEIGHT-1, 0);
            y = constrain(y, 0, GRAPH_HEIGHT-1);
            
            if(!firstPoint) {
                waveSprite.drawLine(x-xStep, prevY, x, y, waveColor);
            } else {
                firstPoint = false;
            }
            
            prevY = y;
        }
    }
    
    // 绘制光标
    if(cursorMode) {
        // 光标1
        waveSprite.drawFastVLine(cursor1Pos, 0, GRAPH_HEIGHT, CURSOR1_COLOR);
        if(cursor1Selected) {
            waveSprite.fillTriangle(
                cursor1Pos-4, 2,
                cursor1Pos+4, 2,
                cursor1Pos, 10,
                CURSOR1_COLOR);
        }
        
        // 光标2
        waveSprite.drawFastVLine(cursor2Pos, 0, GRAPH_HEIGHT, CURSOR2_COLOR);
        if(!cursor1Selected) {
            waveSprite.fillTriangle(
                cursor2Pos-4, 2,
                cursor2Pos+4, 2,
                cursor2Pos, 10,
                CURSOR2_COLOR);
        }
        
        // 光标间区域半透明
        uint16_t startX = min(cursor1Pos, cursor2Pos);
        uint16_t endX = max(cursor1Pos, cursor2Pos);
        for(uint16_t x = startX; x <= endX; x++) {
            for(uint16_t y = 0; y < GRAPH_HEIGHT; y++) {
                if(y % 2 == 0 && x % 2 == 0) {
                    waveSprite.drawPixel(x, y, TFT_NAVY);
                }
            }
        }
    }
    
    // 一次性将精灵推送到屏幕上，减少闪烁
    waveSprite.pushSprite(0, GRAPH_TOP);
}

// 更新信息显示
void updateInfoDisplay() {
    static lv_obj_t *infoLabel = NULL;
    
    if (infoLabel == NULL) {
        infoLabel = lv_label_create(lv_scr_act(), NULL);
        lv_obj_align(infoLabel, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 10, -5);
    }
    
    // 准备信息字符串
    char info[100];
    
    // 获取电压/状态信息
    if (digitalMode) {
        String state = samplesBuffer[(sampleIndex-1)%MAX_SAMPLES] > 2047 ? "HIGH" : "LOW";
        sprintf(info, "DS: %s | ", state.c_str());
    } else {
        float voltage = samplesBuffer[(sampleIndex-1)%MAX_SAMPLES]*3.3/4095.0;
        sprintf(info, "Vol: %.2fV | ", voltage);
    }
    
    // 添加频率信息
    String freqUnit = "Hz";
    float displayFreq = cursorMode ? measuredFrequency : detectedFrequency;
    
    if (displayFreq > 1000) {
        displayFreq /= 1000.0;
        freqUnit = "kHz";
    }
    
    char freqInfo[30];
    sprintf(freqInfo, "Freq: %.1f%s", displayFreq, freqUnit.c_str());
    strcat(info, freqInfo);
    
    // 更新标签
    lv_label_set_text(infoLabel, info);
    
    // 如果在光标模式下更新光标信息
    if (cursorMode) {
        static lv_obj_t *cursorInfoLabel = NULL;
        
        if (cursorInfoLabel == NULL) {
            cursorInfoLabel = lv_label_create(lv_scr_act(), NULL);
            lv_obj_align(cursorInfoLabel, infoLabel, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
        }
        
        float timeDiff = abs(cursor1Pos - cursor2Pos) * (currentTimebase / 32.0);
        char cursorInfo[50];
        sprintf(cursorInfo, "| dT: %.2fms", timeDiff);
        
        lv_label_set_text(cursorInfoLabel, cursorInfo);
        lv_obj_set_hidden(cursorInfoLabel, false);
    } else {
        // 隐藏光标信息
        static lv_obj_t *cursorInfoLabel = NULL;
        if (cursorInfoLabel != NULL) {
            lv_obj_set_hidden(cursorInfoLabel, true);
        }
    }
}

void setup() {
    Serial.begin(115200);
    
    // 初始化引脚
    pinMode(ADC_PIN, INPUT);
    pinMode(DIGITAL_PIN, INPUT_PULLUP);
    
    Serial.println("初始化显示和触摸屏...");
    tft_touch_init();
    
    Serial.println("初始化LVGL...");
    lvgl_setup();
    
    Serial.println("Free RAM before sprite: " + String(ESP.getFreeHeap()) + " bytes");
    
    // 创建波形精灵 - 使用新尺寸并分配缓冲
    waveSprite.createSprite(GRAPH_WIDTH, GRAPH_HEIGHT);
    waveSprite.fillSprite(BG_COLOR);
    Serial.println("波形精灵初始化完成: " + String(GRAPH_WIDTH) + "x" + String(GRAPH_HEIGHT));
    Serial.println("Free RAM after sprite: " + String(ESP.getFreeHeap()) + " bytes");
    
    Serial.println("创建UI界面...");
    create_ui();
    
    Serial.println("内存使用情况: " + String((ESP.getHeapSize() - ESP.getFreeHeap()) / 1024.0, 1) + "KB / " + String(ESP.getHeapSize() / 1024.0, 1) + "KB");
    Serial.println("初始化完成");
}

void loop() {
    // 处理LVGL任务
    lv_task_handler();
    
    // 采样控制
    static uint32_t lastSampleTime = 0;
    uint32_t requiredInterval = currentTimebase * 1000 / 10; // 转换为微秒，每个时基单位显示10个采样点
    requiredInterval = max(requiredInterval, (uint32_t)(1000000 / 20000)); // 限制最高20kHz采样率
    
    if(!isPaused && micros() - lastSampleTime >= requiredInterval) {
        lastSampleTime = micros();
        takeSample();
        detectFrequency();
    }
    
    // 计算采样频率 - 每500ms更新一次
    static uint32_t lastFreqCalcTime = 0;
    if(millis() - lastFreqCalcTime >= 500) {
        calculateSamplingFreq();
        lastFreqCalcTime = millis();
    }
    
    // 波形显示刷新 - 降低刷新率为每50ms刷新一次，减少闪烁
    static uint32_t lastRefresh = 0;
    if(millis() - lastRefresh >= 50) {
        lastRefresh = millis();
        updateWaveform();
        updateInfoDisplay();
    }
    
    // 延迟以避免CPU过载，增加延迟时间
    delay(10);
} 