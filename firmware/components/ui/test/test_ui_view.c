#include <stdio.h>
#include <string.h>

#include "ui_view.h"

static int fails, passes;
#define CHECK(c) do { if (c) passes++; else { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
    ui_model_t m; ui_view_t v;
    ui_model_init(&m, 30000);
    ui_contact_t c[1] = { { 0xB0B0, "Bravo" } };
    ui_model_set_contacts(&m, c, 1);
    m.battery_pct = 87; m.rssi = -100; m.unread = 2;
    /* every screen renders without overflow and with a title */
    for (int s = 0; s < SCR__COUNT; s++) {
        m.screen = (ui_screen_t)s;
        ui_view_build(&m, &v);
        CHECK(strlen(v.title) > 0 || s == SCR_VIEWER);
        CHECK(v.n_rows <= UI_VIEW_ROWS);
    }
    m.screen = SCR_HOME; ui_view_build(&m, &v);
    CHECK(strstr(v.status, "87%") && strstr(v.status, "2 new") && strstr(v.status, "|||."));
    CHECK(v.n_rows == 4 && strstr(v.rows[1], "2 new") && v.highlight == 0);
    m.screen = SCR_PHOTO_PREVIEW; m.photo_bytes = 11500; m.photo_eta_ms = 76500; ui_view_build(&m, &v);
    CHECK(strstr(v.title, "12 kB") && strstr(v.title, "77 s") && v.show_image);
    m.screen = SCR_SENDING; m.progress = 40; m.recipient = 0xB0B0; ui_view_build(&m, &v);
    CHECK(v.progress == 40 && strstr(v.title, "Bravo"));
    m.screen = SCR_PAIRING; m.pair_code = 7; ui_view_build(&m, &v); CHECK(strstr(v.body, "000007"));
    m.screen = SCR_TEXT_PREVIEW; m.low_confidence = true; m.text_len = 0; ui_view_build(&m, &v);
    CHECK(strstr(v.title, "unclear") && strstr(v.body, "nothing"));
    /* very long contact name / status do not overflow */
    memset(m.status, 'x', sizeof m.status - 1); m.status[sizeof m.status - 1] = 0;
    ui_view_build(&m, &v); CHECK(strlen(v.footer) <= UI_VIEW_COLS);
    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
