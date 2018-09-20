#ifndef CANSEND_H
#define CANSEND_H

#define CAN_MSG_SUP_HB "0000FFFF#01045555555555"
#define CAN_MSG_CM_WAKE "0000FFFF#00015555555555"
#define CAN_MSG_WAKEUP_BTN "0000FFFF#06015555555555"
#define CAN_MSG_SW_READY "0000FFFF#0A005555555555"

int cansend_main(const char *msg);

int start_hb_thread(void);
int stop_hb_thread(void);

#endif
