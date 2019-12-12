/* MP3 decoding support using libmad or libmpg123. */

#if !defined(_SND_MP3_H_)
#define _SND_MP3_H_

#if defined(USE_CODEC_MP3)

extern snd_codec_t mp3_codec;
int mp3_skiptags(snd_stream_t *);

#endif	/* USE_CODEC_MP3 */

#endif	/* ! _SND_MP3_H_ */

