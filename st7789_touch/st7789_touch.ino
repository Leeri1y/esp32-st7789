	#include <lvgl.h>
	#include <Ticker.h>
	#include <TFT_eSPI.h>
	#include <XPT2046_Touchscreen.h>
	 
	#include "lv_test_theme.h"
	#include "lv_test_theme_1.h"
	 
	#define LVGL_TICK_PERIOD 20
	 
	#define CS_PIN  17 // Chip Select pin
	#define irq_pin 16 //IRQ pin
	 
	Ticker tick; /* timer for interrupt handler */
	TFT_eSPI tft = TFT_eSPI(); /* TFT instance */
	XPT2046_Touchscreen ts(CS_PIN, irq_pin);

  // 消抖相关变量
  uint32_t last_touch_time = 0;
  const uint16_t CLICK_DEBOUNCE_MS = 30;  // 单击消抖时间
  bool is_sliding = false;                // 标记是否在滑动

  // Calibration variables
  int16_t cal_x_min = 200, cal_x_max = 3900; // Raw X min/max from touchscreen
  int16_t cal_y_min = 200, cal_y_max = 3800; // Raw Y min/max from touchscreen
	 
	////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////
	//// LVGL /////////////////////////////////////////////
	static lv_disp_buf_t disp_buf;
	static lv_color_t buf[LV_HOR_RES_MAX * 10];
	 
	/* 与LCD驱动关联 */
	void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
	{
	     
	    uint16_t c;
	     
	    tft.startWrite(); /* Start new TFT transaction */
	    tft.setAddrWindow(area->x1, area->y1, (area->x2 - area->x1 + 1), (area->y2 - area->y1 + 1)); /* set the working window */
	    for (int y = area->y1; y <= area->y2; y++) {
	        for (int x = area->x1; x <= area->x2; x++) {
	            c = color_p->full;
	            tft.writeColor(c, 1);
	            color_p++;
	        }
	    }
	    tft.endWrite(); /* terminate TFT transaction */
	    lv_disp_flush_ready(disp); /* tell lvgl that flushing is done */
	}
	 
	/* 中断 ms */
	static void lv_tick_handler(void)
	{
	     
	    lv_tick_inc(LVGL_TICK_PERIOD);
	}
	 
	/* 输入设备：touchpad */
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
	 
	void tft_touch_init()
	{
	    ts.begin(); //Begin TouchScreen.
	    tft.begin(); /* TFT init */
	    tft.setRotation(1); /* Landscape orientation */
	}
	 
	void lvgl_setup()
	{
	    lv_init();
	     
	    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * 10);
	     
	    /*Initialize the display*/
	    lv_disp_drv_t disp_drv;
	    lv_disp_drv_init(&disp_drv);
	    disp_drv.hor_res = LV_HOR_RES_MAX;
	    disp_drv.ver_res = LV_VER_RES_MAX;
	    disp_drv.flush_cb = my_disp_flush;
	    disp_drv.buffer = &disp_buf;
	    lv_disp_drv_register(&disp_drv);
	     
	     
	    /*Initialize the touch pad*/
	    lv_indev_drv_t indev_drv;
	    lv_indev_drv_init(&indev_drv);
	    indev_drv.type = LV_INDEV_TYPE_POINTER;
	    indev_drv.read_cb = my_touchpad_read;
	    lv_indev_drv_register(&indev_drv);
	     
	    /*Initialize the graphics library's tick*/
	    tick.attach_ms(LVGL_TICK_PERIOD, lv_tick_handler);
	}
	 
	void setup() {
	     
	    Serial.begin(9600); /* prepare for possible serial debug */
	     
	    //tft和touch 设置
	    tft_touch_init();
	     
	    //lvgl 相关设置和初始化
	    lvgl_setup();
	     
	    //lvgl demo
	    lv_test_theme();
	     
	    //    lv_test_theme_1(lv_theme_night_init(210, NULL));
	    //  lv_test_theme_1(lv_theme_night_init(100, NULL));
	    //  lv_test_theme_1(lv_theme_material_init(210, NULL));
	    //  lv_test_theme_1(lv_theme_alien_init(210, NULL));
	//  lv_test_theme_1(lv_theme_zen_init(210, NULL));
	    //  lv_test_theme_1(lv_theme_nemo_init(210, NULL));
	    //  lv_test_theme_1(lv_theme_mono_init(210, NULL));
	    //  lv_test_theme_1(lv_theme_default_init(210, NULL));
	     
	}
	 
	void loop() {
	    // put your main code here, to run repeatedly:
	     
	    lv_task_handler();
	    /*If 'lv_tick_inc(5)' is not called in a Timer then call it here*/
	    delay(5);             /*Wait a little*/
	}
						