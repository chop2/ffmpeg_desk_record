#include "stdafx.h"
#include "ffmpeg_screen_record.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

using namespace std;

FFmpeg_screen_record::FFmpeg_screen_record() :
_pFormatCtx_Video(NULL), _pCodecCtx_Video(NULL),
_pCodec_Video(NULL), _fifo_video(NULL),
_img_convert_ctx(NULL), _pix_fmt(AV_PIX_FMT_BGR24),
_frame_size(0), _bRecordStop(false),
_buffersink_ctx(NULL), _buffersrc_ctx(NULL),
_filter_graph(NULL), _video_stream_index(0),
_bBusy(false)
{
}


FFmpeg_screen_record::~FFmpeg_screen_record()
{
	Release();
}

void FFmpeg_screen_record::Release()
{
	_bRecordStop = true;
	avformat_close_input(&_pFormatCtx_Video);
	//avformat_free_context(_pFormatCtx_Video);
	//avcodec_free_context(&_pCodecCtx_Video);
	avfilter_free(_buffersink_ctx);
	avfilter_free(_buffersrc_ctx);
	avfilter_graph_free(&_filter_graph);
	sws_freeContext(_img_convert_ctx);
	//is neccessary to free _pCodec_Video ??
}

int FFmpeg_screen_record::init_filters(const char *filters_descr)
{
	char args[512];
	int ret = 0;
	AVFilter *buffersrc = avfilter_get_by_name("buffer");     /* 输入buffer filter */
	AVFilter *buffersink = avfilter_get_by_name("buffersink"); /* 输出buffer filter */
	AVFilterInOut *outputs = avfilter_inout_alloc();
	AVFilterInOut *inputs = avfilter_inout_alloc();
	AVRational time_base = _pFormatCtx_Video->streams[_video_stream_index]->time_base;   /* 时间基数 */

	enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_BGRA, AV_PIX_FMT_NONE };

	_filter_graph = avfilter_graph_alloc();                     /* 创建graph  */
	if (!outputs || !inputs || !_filter_graph) {
		ret = AVERROR(ENOMEM);
		goto end;
	}

	/* buffer video source: the decoded frames from the decoder will be inserted here. */
	_snprintf(args, sizeof(args),
		"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
		_pCodecCtx_Video->width, _pCodecCtx_Video->height, _pCodecCtx_Video->pix_fmt,
		time_base.num, time_base.den,
		_pCodecCtx_Video->sample_aspect_ratio.num, _pCodecCtx_Video->sample_aspect_ratio.den);

	/* 创建并向FilterGraph中添加一个Filter */
	ret = avfilter_graph_create_filter(&_buffersrc_ctx, buffersrc, "in",
		args, NULL, _filter_graph);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
		goto end;
	}

	/* buffer video sink: to terminate the filter chain. */
	ret = avfilter_graph_create_filter(&_buffersink_ctx, buffersink, "out",
		NULL, NULL, _filter_graph);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
		goto end;
	}

	/* Set a binary option to an integer list. */
	ret = av_opt_set_int_list(_buffersink_ctx, "pix_fmts", pix_fmts,
		AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
		goto end;
	}

	/*
	* Set the endpoints for the filter graph. The filter_graph will
	* be linked to the graph described by filters_descr.
	*/

	/*
	* The buffer source output must be connected to the input pad of
	* the first filter described by filters_descr; since the first
	* filter input label is not specified, it is set to "in" by
	* default.
	*/
	outputs->name = av_strdup("in");
	outputs->filter_ctx = _buffersrc_ctx;
	outputs->pad_idx = 0;
	outputs->next = NULL;

	/*
	* The buffer sink input must be connected to the output pad of
	* the last filter described by filters_descr; since the last
	* filter output label is not specified, it is set to "out" by
	* default.
	*/
	inputs->name = av_strdup("out");
	inputs->filter_ctx = _buffersink_ctx;
	inputs->pad_idx = 0;
	inputs->next = NULL;

	/* Add a graph described by a string to a graph */
	if ((ret = avfilter_graph_parse_ptr(_filter_graph, filters_descr,
		&inputs, &outputs, NULL)) < 0)
		goto end;

	/* Check validity and configure all the links and formats in the graph */
	if ((ret = avfilter_graph_config(_filter_graph, NULL)) < 0)
		goto end;

end:
	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);

	return ret;
}

int FFmpeg_screen_record::OpenVideoCapture()
{
	AVInputFormat* ifmt = av_find_input_format("gdigrab");
	//这里可以加参数打开，例如可以指定采集帧率
	AVDictionary* options = NULL;
	av_dict_set(&options, "framerate", "15", NULL);
	//Video frame size. The default is to capture the full screen
	//av_dict_set(&options, "video_size", "320x240", 0);
	if (avformat_open_input(&_pFormatCtx_Video, "desktop", ifmt, &options) != 0)
	{
		printf("Couldn't open input stream.（无法打开视频输入流）\n");
		return -1;
	}

	if (avformat_find_stream_info(_pFormatCtx_Video, NULL) < 0)
	{
		printf("Couldn't find stream information.（无法获取视频流信息）\n");
		return -1;
	}

	if (_pFormatCtx_Video->streams[0]->codec->codec_type != AVMEDIA_TYPE_VIDEO)
	{
		printf("Couldn't find video stream information.（无法获取视频流信息）\n");
		return -1;
	}

	_pCodecCtx_Video = _pFormatCtx_Video->streams[0]->codec;
	_pCodec_Video = avcodec_find_decoder(_pCodecCtx_Video->codec_id);
	if (_pCodec_Video == NULL)
	{
		printf("Codec not found.（没有找到解码器）\n");
		return -1;
	}
	if (avcodec_open2(_pCodecCtx_Video, _pCodec_Video, NULL) < 0)
	{
		printf("Could not open codec.（无法打开解码器）\n");
		return -1;
	}
	_img_convert_ctx = sws_getContext(_pCodecCtx_Video->width, _pCodecCtx_Video->height, _pCodecCtx_Video->pix_fmt,
		_pCodecCtx_Video->width, _pCodecCtx_Video->height, _pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);
	_frame_size = avpicture_get_size(_pCodecCtx_Video->pix_fmt, _pCodecCtx_Video->width, _pCodecCtx_Video->height);
	//申请30帧缓存
	//_fifo_video = av_fifo_alloc(30 * avpicture_get_size(_pix_fmt, _pCodecCtx_Video->width, _pCodecCtx_Video->height));
	
	av_dict_free(&options);
	return 0;
}

unsigned long __stdcall FFmpeg_screen_record::ScreenCapThreadProc(LPVOID lpParam)
{
	FFmpeg_screen_record* pParent = (FFmpeg_screen_record*)lpParam;
	if (pParent == NULL)
		return -1;
	AVPacket packet;
	int got_picture;
	AVFrame* pFrame, *filt_frame;
	pFrame = av_frame_alloc(); 
	filt_frame = av_frame_alloc();
	av_init_packet(&packet);
	AVFrame* picture = NULL;
	uint8_t* picture_buf = NULL;
	int size = 0;
	while (true)
	{
		if (pParent->_pFormatCtx_Video == NULL)
			break;
		packet.data = NULL;
		packet.size = 0;
		if (av_read_frame(pParent->_pFormatCtx_Video, &packet) < 0)
			continue;
		if (packet.stream_index == 0)
		{
			if (avcodec_decode_video2(pParent->_pCodecCtx_Video, pFrame, &got_picture, &packet) < 0)
			{
				printf("Decode Error.（解码错误）\n");
				continue;
			}

			if (got_picture)
			{	
				/* push the decoded frame into the filtergraph */
				if (av_buffersrc_add_frame_flags(pParent->_buffersrc_ctx, pFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
					av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
					break;
				}
				int ret = av_buffersink_get_frame(pParent->_buffersink_ctx, filt_frame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
					continue;

				if (picture == NULL)
				{
					picture = av_frame_alloc();
					size = avpicture_get_size(pParent->_pix_fmt, filt_frame->width, filt_frame->height);
					if (picture_buf != NULL) 
						delete [] picture_buf;
					picture_buf = new uint8_t[size];
					avpicture_fill((AVPicture*)picture, picture_buf,
						pParent->_pix_fmt, filt_frame->width, filt_frame->height);
				}

				pParent->_img_convert_ctx = sws_getContext(filt_frame->width, filt_frame->height, pParent->_pCodecCtx_Video->pix_fmt,
					filt_frame->width, filt_frame->height, pParent->_pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);

				//YUV to BGR
				sws_scale(pParent->_img_convert_ctx, (const uint8_t* const*)filt_frame->data, filt_frame->linesize,
					0, filt_frame->height, picture->data, picture->linesize);

				if (pParent->_streamCallback != NULL)
				{
					pParent->_streamCallback(filt_frame->height, filt_frame->width, picture_buf, pParent->_pContext);
				}
				av_frame_unref(filt_frame);
			}
		}
		av_free_packet(&packet);
		if (pParent->_bRecordStop)
			break;
	}
	av_frame_free(&picture);
	av_frame_free(&pFrame);
	av_frame_free(&filt_frame);
	pParent->_bBusy = false;
	if (picture_buf != NULL) 
		delete [] picture_buf;
	return 0;
}

int FFmpeg_screen_record::Run(const std::string& filterStr)
{
	_bBusy = true;
	_bRecordStop = false;
	av_register_all();
	avdevice_register_all();
	avfilter_register_all();
	if (OpenVideoCapture() < 0)
		return -1;
	if (init_filters(filterStr.c_str()) < 0)
		return -1;
	//star cap screen thread
#ifdef _WIN32
	m_hThread = CreateThread(NULL, 0, ScreenCapThreadProc, this, 0, NULL);
#elif defined __linux__
	pthread_create(&m_threadID, NULL, (void *)ScreenCapThreadProc, this)
#endif
	return 0;
}

//void FFmpeg_screen_record::PauseRecord(bool bPause)
//{
//	if (bPause)
//	{
//		SuspendThread(m_hThread);
//	}
//	else
//	{
//		ResumeThread(m_hThread);
//	}
//}

void FFmpeg_screen_record::StopRecord()
{ 
	_bRecordStop = true;
#if defined _WIN32
	Sleep(500);
#elif defined __linux__
	sleep(0.5);
#endif
	Release();
}

std::string FFmpeg_screen_record::MakeCropFilterStr(int x, int y, int width, int height)
{
	char buff[128] = { 0 };
	sprintf(buff, "crop=%d:%d:%d:%d", width, height, x, y);
	return string(buff);
}

void FFmpeg_screen_record::RegisterStreamCallback(StreamCallback pCallback, void* pContext)
{
	this->_streamCallback = *pCallback;
	this->_pContext = pContext;
}