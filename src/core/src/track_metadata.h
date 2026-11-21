#pragma once

// Cabeçalho interno: deixa `decoder_ffmpeg.cpp` e `metadata.cpp` compartilharem a mesma
// extração de tags, para a playlist e o player nunca discordarem do que é uma faixa.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "wamp/types.h"

namespace wamp::detail {

/// Preenche `info` a partir do container e do stream de áudio. `codec` pode ser nulo —
/// a leitura de playlist não abre codec; nesse caso sample rate e canais vêm do codecpar.
void fillTrackInfo(AVFormatContext* format, AVStream* stream, AVCodecContext* codec,
                   TrackInfo* info);

}  // namespace wamp::detail
