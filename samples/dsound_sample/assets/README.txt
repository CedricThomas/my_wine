ringingsound.wav
Source: https://commons.wikimedia.org/wiki/File:Ringingsound.wav
Download URL used in development: https://commons.wikimedia.org/wiki/Special:Redirect/file/Ringingsound.wav
Author: Kaga Sound
License: CC0 1.0 Universal

ringingsound.pcm is the raw PCM payload derived from ringingsound.wav with:
ffmpeg -y -i ringingsound.wav -f s16le -acodec pcm_s16le -ac 1 -ar 8000 ringingsound.pcm

ringingsound_pcm.h is the xxd-generated C header produced from ringingsound.pcm.
