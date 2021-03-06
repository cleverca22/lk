#include <arch/ops.h>
#include <lk/console_cmd.h>
#include <lk/reg.h>
#include <platform/bcm28xx/hvs.h>
#include <stdio.h>
#include <stdlib.h>

// note, 4096 slots total
volatile uint32_t* dlist_memory = REG32(SCALER_LIST_MEMORY);
volatile struct hvs_channel *hvs_channels = (volatile struct hvs_channel*)REG32(SCALER_DISPCTRL0);
int display_slot = 0;

static int cmd_hvs_dump(int argc, const cmd_args *argv);

STATIC_COMMAND_START
STATIC_COMMAND("hvs_dump", "dump hvs state", &cmd_hvs_dump)
STATIC_COMMAND_END(hvs);

static uint32_t gfx_to_hvs_pixel_format(gfx_format fmt) {
  switch (fmt) {
  case GFX_FORMAT_RGB_565:
    return HVS_PIXEL_FORMAT_RGB565;
  case GFX_FORMAT_RGB_332:
    return HVS_PIXEL_FORMAT_RGB332;
  case GFX_FORMAT_ARGB_8888:
  case GFX_FORMAT_RGB_x888:
    return HVS_PIXEL_FORMAT_RGBA8888;
  default:
    printf("warning, unsupported pixel format: %d\n", fmt);
    return 0;
  }
}

void hvs_add_plane(gfx_surface *fb, int x, int y) {
  dlist_memory[display_slot++] = CONTROL_VALID
    | CONTROL_WORDS(7)
    | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_ABGR)
//    | CONTROL0_VFLIP
    | CONTROL_UNITY
    | CONTROL_FORMAT(gfx_to_hvs_pixel_format(fb->format));
  dlist_memory[display_slot++] = POS0_X(x) | POS0_Y(y) | POS0_ALPHA(0xff);
  dlist_memory[display_slot++] = POS2_H(fb->height) | POS2_W(fb->width);
  dlist_memory[display_slot++] = 0xDEADBEEF; // dummy for HVS state
  dlist_memory[display_slot++] = (uint32_t)fb->ptr | 0xc0000000;
  dlist_memory[display_slot++] = 0xDEADBEEF; // dummy for HVS state
  dlist_memory[display_slot++] = fb->stride * fb->pixelsize;
}

void hvs_terminate_list(void) {
  dlist_memory[display_slot++] = CONTROL_END;
}

void hvs_initialize() {
  *REG32(SCALER_DISPCTRL) &= ~SCALER_DISPCTRL_ENABLE; // disable HVS
  *REG32(SCALER_DISPCTRL) = SCALER_DISPCTRL_ENABLE | 0x9a0dddff; // re-enable HVS
  for (int i=0; i<3; i++) {
    hvs_channels[i].dispctrl = SCALER_DISPCTRLX_RESET;
    hvs_channels[i].dispctrl = 0;
    hvs_channels[i].dispbkgnd = 0x1020202; // bit 24
  }

  hvs_channels[2].dispbase = BASE_BASE(0)      | BASE_TOP(0xf00);
  hvs_channels[1].dispbase = BASE_BASE(0xf10)  | BASE_TOP(0x4b00);
  hvs_channels[0].dispbase = BASE_BASE(0x4b10) | BASE_TOP(0x7700);

  hvs_wipe_displaylist();

  *REG32(SCALER_DISPEOLN) = 0x40000000;
}

void hvs_configure_channel(int channel, int width, int height) {
  hvs_channels[0].dispctrl = SCALER_DISPCTRLX_RESET;
  hvs_channels[0].dispctrl = SCALER_DISPCTRLX_ENABLE | SCALER_DISPCTRL_W(width) | SCALER_DISPCTRL_H(height);

  hvs_channels[0].dispbkgnd = SCALER_DISPBKGND_AUTOHS | 0x020202;
}

void hvs_wipe_displaylist(void) {
  for (int i=0; i<1024; i++) {
    dlist_memory[i] = CONTROL_END;
  }
  display_slot = 0;
}

static int cmd_hvs_dump(int argc, const cmd_args *argv) {
  printf("SCALER_DISPCTRL: 0x%x\n", *REG32(SCALER_DISPCTRL));
  printf("SCALER_DISPEOLN: 0x%08x\n", *REG32(SCALER_DISPEOLN));
  printf("SCALER_DISPLIST0: 0x%x\n", *REG32(SCALER_DISPLIST0));
  uint32_t list1 = *REG32(SCALER_DISPLIST1);
  printf("SCALER_DISPLIST1: 0x%x\n", list1);
  printf("SCALER_DISPLIST2: 0x%x\n\n", *REG32(SCALER_DISPLIST2));
  for (int i=0; i<3; i++) {
    printf("SCALER_DISPCTRL%d: 0x%x\n", i, hvs_channels[i].dispctrl);
    printf("SCALER_DISPBKGND%d: 0x%x\n", i, hvs_channels[i].dispbkgnd);
    uint32_t stat = hvs_channels[i].dispstat;
    printf("SCALER_DISPSTAT%d: 0x%x\n", i, stat);
    printf("mode: %d\n", (stat >> 30) & 0x3);
    if (stat & (1<<29)) puts("full");
    if (stat & (1<<28)) puts("empty");
    printf("unknown: 0x%x\n", (stat >> 18) & 0x3ff);
    printf("frame count: %d\n", (stat >> 12) & 0x3f);
    printf("line: %d\n", stat & 0xfff);
    uint32_t base = hvs_channels[i].dispbase;
    printf("SCALER_DISPBASE%d: base 0x%x top 0x%x\n\n", i, base & 0xffff, base >> 16);
  }
  for (uint32_t i=list1; i<(list1+16); i++) {
    printf("dlist[%x]: 0x%x\n", i, dlist_memory[i]);
  }
  return 0;
}

__WEAK status_t display_get_framebuffer(struct display_framebuffer *fb) {
  // TODO, have a base layer exposed via this
  return -1;
}
