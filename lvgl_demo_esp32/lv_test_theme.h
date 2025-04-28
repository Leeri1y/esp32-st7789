/**
 * @file lv_test_theme.h
 *
 */

#ifndef LV_TEST_THEME_2_H
#define LV_TEST_THEME_2_H

#ifdef __cplusplus
extern "C" {
#endif

#define	LV_USE_TESTS	1

#include "lvgl.h"

#if LV_USE_TESTS

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Test run time theme change
 */
void lv_test_theme(void);

/**********************
 *      MACROS
 **********************/

#endif /*LV_USE_TESTS*/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*LV_TEST_THEME_2_H*/
