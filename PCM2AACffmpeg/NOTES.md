# NOTES

## FFMPEG LAME/AAC Encoder issues
AAC/LAME works only for 
- __S16 MONO__. 
- __S16P STEREO__
- __FLTP MONO/STEREO__
But, it does not work for __S16 STEREO__. 

## swr_convert()
- In the FFmpeg example `mux.c` there are two audio frames. the `S16_FRAME` and `FLTP_FRAME`. 
- The microphone writes to the `S16_FRAME`
- The AAC/LAME encoder reads from the `FLTP_FRAME`
- `S16_FRAME` is resampled into `FLTP_FRAME` for the `AAC/LAME` encoders. 
- `FLTP_FRAME` and `S16_FRAME` have same `sample_rate` and `ch_layout`
- `Qt` does not provide `PLANAR` audio buffers. only `INTERLEAVED`. 
- So i guess if i want to use `Qt's AudioInput` for `S16 interleaved`, I have to resample `S16_FRAME` into `FLTP_FRAME` before `AAC` encoding.
- Or i could use `Qt's FLT` format for audio but it's not `PLANAR` so I will have to resample `FLT_interleaved` into `FLT_planar` before `AAC/LAME` encoding

```
swr_convert(
    swr_ctx,                            // rescale_context
    FLTP_FRAME->data,                   // output frame buffer
    FLTP_nb_samples,                    // output number samples
    (const uint8_t **)S16_FRAME->data,  // ipnut frame buffer 
    S16_nb_samples                      // input number samples
);
```
The function returns the number of samples that were sucessfully converted or an `AVERROR`.

--------------------



## sws_scale()
- For the video, Qt gave me an array of `RGB` pixels. I converted it into `YUV444P` before writing to the `FLV` container. I used `sws_scale()` for pushing an array of bytes (RGB pixels) into a `AVFrame`'s buffer (YUV444P). 
- Qt _might_ also be capable of providing `YUV422P` or `YUV420P` buffers which can be directly encoded into `H264`.

`sws_getContext` is used for initializing the `SwsContext`
```
struct SwsContext *sws_getContext(
    int srcW,                       // 1920
    int srcH,                       // 1080
    enum AVPixelFormat srcFormat,   // RGB
    int dstW,                       // 1920
    int dstH,                       // 1080
    enum AVPixelFormat dstFormat,   // YUV444
    int flags,                      // SWS_BILINEAR for example
    SwsFilter *srcFilter,           // nullable
    SwsFilter *dstFilter,           // nullable
    const double *param             // nullable
);
```
`sws_scale` is used for the actual rescaling
```
int sws_scale(
    SwsContext *c,                      // re-scale context
    const uint8_t *const srcSlice[],    // pointer to array of input RGB bytes
    const int srcStride[],              // array. single element. 1920*BYTES_PER_PIXEL
    int srcSliceY,                      // 0 i.e start position of input buffer
    int srcSliceH,                      // 1080 i.e height of input buffer
    uint8_t *const dst[],               // pointer to array of output YUV444 bytes
    const int dstStride[]               // array. single element. 1920*BYTES_PER_PIXEL
    );
```