/*
 * Copyright (C) 2023 Tetsuya Isaki
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "ImageLoaderWebp.h"
#include <cstring>
#include <webp/decode.h>

// コンストラクタ
ImageLoaderWebp::ImageLoaderWebp(InputStream *stream_, const Diag& diag_)
	: inherited(stream_, diag_)
{
}

// デストラクタ
ImageLoaderWebp::~ImageLoaderWebp()
{
}

// stream が webp なら true を返す。
bool
ImageLoaderWebp::Check() const
{
	// API で出来るのか分からんのでとりあえず自前で。

	char magic[12];

	auto n = stream->Peek(&magic, sizeof(magic));
	if (n < sizeof(magic)) {
		Trace(diag, "%s: Read(magic) failed: %s", __method__, strerror(errno));
		return false;
	}

	// マジックを確認。
	// +0: 'R' 'I' 'F' 'F'
	// +4: filesize
	// +8: 'W' 'E' 'B' 'P'
	if (strncmp(&magic[0], "RIFF", 4) != 0) {
		Trace(diag, "%s: Bad magic(RIFF)", __method__);
		return false;
	}
	if (strncmp(&magic[8], "WEBP", 4) != 0) {
		Trace(diag, "%s: Bad magic(WEBP)", __method__);
		return false;
	}
	Trace(diag, "%s: OK", __method__);
	return true;
}

// stream から画像をロードする。
bool
ImageLoaderWebp::Load(Image& img)
{
	WebPDecoderConfig config;
	int width;
	int height;
	int stride;
	uint8 *s = NULL;
	uint8 *d;
	bool rv = false;

	WebPInitDecoderConfig(&config);
	config.options.bypass_filtering = 1;	// ?
	config.options.no_fancy_upsampling = 1;
	config.options.use_cropping = 0;
	config.options.use_scaling = 0;

	// デコード。
	WebPIDecoder *idec = WebPINewDecoder(NULL);
	uint8 buf[1024];
	int status = VP8_STATUS_NOT_ENOUGH_DATA;
	for (;;) {
		auto n = stream->Read(buf, sizeof(buf));
		if (n <= 0) {
			break;
		}
		status = WebPIAppend(idec, buf, n);
		if (status != VP8_STATUS_SUSPENDED) {
			break;
		}
	}
	if (status != VP8_STATUS_OK) {
		Trace(diag, "%s: Decode failed %d", __method__, (int)status);
		goto abort;
	}

	// RGB バッファを取得?
	s = WebPIDecGetRGB(idec, NULL, &width, &height, &stride);
	if (s == NULL) {
		Trace(diag, "%s: WebPIDecGetRGB failed", __method__);
		goto abort;
	}

	img.Create(width, height);
	d = img.GetBuf();
	for (int y = 0; y < height; y++) {
		memcpy(d, s, width * 3);
		s += stride;
		d += width * 3;
	}

	rv = true;
 abort:
	WebPIDelete(idec);
	return rv;
}
