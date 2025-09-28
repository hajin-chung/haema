#include <libavutil/error.h>
#include <libavutil/hwcontext.h>

#include "include/hm_context.h"

HMContext *hm_ctx_create() {
  HMContext *ctx = malloc(sizeof(HMContext));
  AVBufferRef *hw_device_ctx = NULL;
  int ret = 0;
  if ((ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_QSV, NULL,
                                    NULL, 0)) < 0) {
    fprintf(stderr, "Failed to create QSV device, error: %s\n",
            av_err2str(ret));
  }
  ctx->hw_device_ctx = hw_device_ctx;
  return ctx;
}


void hm_ctx_free(HMContext *ctx) {
    av_buffer_unref(&ctx->hw_device_ctx); 
    free(ctx);
}

