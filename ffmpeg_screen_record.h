#pragma once
#include <string>
#ifdef  __cplusplus
extern "C"
{
#endif
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/pixfmt.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/avfiltergraph.h"
#include "include/libavfilter/buffersrc.h"

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avdevice.lib")
#pragma comment(lib, "avfilter.lib")
#pragma comment(lib, "postproc.lib")
#pragma comment(lib, "swresample.lib")
#pragma comment(lib, "swscale.lib")
#ifdef __cplusplus
};
#endif
#pragma warning(disable: 4996)

#ifdef __linux__
#include <pthread.h>
#include <unistd.h>
#endif

class FFmpeg_screen_record
{
public:
	FFmpeg_screen_record();
	~FFmpeg_screen_record();
	int OpenVideoCapture();
	void StopRecord();
	//void PauseRecord(bool bPause);
	static unsigned long __stdcall ScreenCapThreadProc(LPVOID lpParam);
	int Run(const std::string& filterStr = "");
	bool IsBusy(){ return _bBusy; }
	typedef void(*StreamCallback)(int height, int width,
		const unsigned char* data, void* pContext);
	void RegisterStreamCallback(StreamCallback pCallback, void* pContext = NULL);
	static std::string MakeCropFilterStr(int x, int y, int width, int height);

	void Release();
protected:
	int init_filters(const char *filters_descr);
	StreamCallback _streamCallback;
	void* _pContext;
protected:
	AVFormatContext* _pFormatCtx_Video;
	AVCodecContext* _pCodecCtx_Video;
	AVCodec* _pCodec_Video;
	AVFifoBuffer* _fifo_video;
	SwsContext* _img_convert_ctx;
	AVPixelFormat _pix_fmt;

	//filter
	AVFilterContext* _buffersink_ctx;
	AVFilterContext* _buffersrc_ctx;
	AVFilterGraph* _filter_graph;
	int _video_stream_index;

	int _frame_size;
	bool _bRecordStop;
#ifdef _WIN32
	HANDLE m_hThread;
#elif defined __linux__
	pthread_t m_threadID;
#endif
	bool _bBusy;
};

