#if(USE_ARM_LIB==1)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <rockchip/rk_mpi.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

#include <string>
#include <vector>

#define MPP_ALIGN(x, a)         (((x)+(a)-1)&~((a)-1))


typedef struct {
        // global flow control flag
        RK_U32 frm_eos;
        RK_U32 pkt_eos;
        RK_U32 frame_count;
        RK_U64 stream_size;

        // base flow context
        MppCtx ctx;
        MppApi *mpi;
        MppEncPrepCfg prep_cfg;
        MppEncRcCfg rc_cfg;
        MppEncCodecCfg codec_cfg;

        // input / output
        MppBuffer frm_buf;
        MppEncSeiMode sei_mode;

        // paramter for resource malloc
        RK_U32 width;
        RK_U32 height;
        RK_U32 hor_stride;
        RK_U32 ver_stride;
        MppFrameFormat fmt;
        MppCodingType type;
        RK_U32 num_frames;

        // resources
        size_t frame_size;
        /* NOTE: packet buffer may overflow */
        size_t packet_size;

        // rate control runtime parameter
        RK_S32 gop;
        RK_S32 fps;
        RK_S32 bps;
        //FILE *fp_output;
        //FILE *fp_outputx;
} MppContext;

/**
 * Rockchip MPP JPEG 编码器封装。
 *
 * 在回传链路里，它把跟踪节点输出的 RGB 小图编码成 JPEG，
 * 以便远程显示或 Web 端消费。
 */
class MppEncode
{
public:
        MppEncode();
        ~MppEncode();

        /**
         * 初始化编码器。
         *
         * @param wid          输入图像宽度。
         * @param hei          输入图像高度。
         * @param jpeg_quality JPEG 量化质量参数。
         */
        void init(int wid,int hei,int jpeg_quality);

        /**
         * 编码一帧 YUV420P 图像。
         *
         * @param in_data   输入 YUV420P 数据首地址。
         * @param in_size   输入数据总字节数，通常是 width * height * 3 / 2。
         * @param jpeg_data 输出参数，编码后的 JPEG 字节流。
         * @return          0 表示成功；非 0 表示编码失败。
         */
        int encode(unsigned char *in_data, int in_size,std::vector<unsigned char> &jpeg_data);

private:
        // mpp_enc_data:
        // 汇总编码器配置、上下文和输入输出资源。
        MppContext mpp_enc_data;

        // buf_ptr:
        // 指向 MPP 输入 frame buffer 的用户态地址，编码前会先把 YUV 数据 memcpy 进来。
        void *buf_ptr;

        // frame:
        // 描述当前输入帧属性（宽高、stride、像素格式等）的 MPP frame。
        MppFrame frame;
};

#endif
