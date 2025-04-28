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
bool my_touchpad_read(lv_indev_drv_t * indev, lv_indev_data_t * data)
{
	static lv_coord_t last_x = 0;
	static lv_coord_t last_y = 0;
	
	/*Save the state and save the pressed coordinate*/
	data->state = ts.touched() ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
	if(data->state == LV_INDEV_STATE_PR){
		TS_Point p = ts.getPoint();
		
		//convert to lcd position
		last_x = LV_HOR_RES-(p.x *LV_HOR_RES)/4095;       /*TODO save the current X coordinate*/
		last_y = LV_VER_RES-(p.y *LV_VER_RES)/4095;       /*TODO save the current Y coordinate*/
		
		Serial.print("touched:");
		Serial.print(last_x);Serial.print(",");Serial.println(last_y);
	}
	
	/*Set the coordinates (if released use the last pressed coordinates)*/
	data->point.x = last_x;
	data->point.y = last_y;
	
	return false; /*Return `false` because we are not buffering and no more data to read*/
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
	//	lv_test_theme_1(lv_theme_night_init(100, NULL));
	//	lv_test_theme_1(lv_theme_material_init(210, NULL));
	//	lv_test_theme_1(lv_theme_alien_init(210, NULL));
//	lv_test_theme_1(lv_theme_zen_init(210, NULL));
	//	lv_test_theme_1(lv_theme_nemo_init(210, NULL));
	//	lv_test_theme_1(lv_theme_mono_init(210, NULL));
	//	lv_test_theme_1(lv_theme_default_init(210, NULL));
	
}

void loop() {
	// put your main code here, to run repeatedly:
	
	lv_task_handler();
	/*If 'lv_tick_inc(5)' is not called in a Timer then call it here*/
	delay(5);             /*Wait a little*/
}
