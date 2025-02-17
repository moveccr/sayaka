/*
 * Copyright (C) 2015 Y.Sugahara (moveccr)
 * Copyright (C) 2021 Tetsuya Isaki
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

#include "header.h"
#include "Diag.h"
#include "FdStream.h"
#include "FileStream.h"
#include "HttpClient.h"
#include "ImageReductor.h"
#include "StringUtil.h"
#include "SixelConverter.h"
#include "term.h"
#include <chrono>
#include <cstring>
#include <map>
#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <sys/utsname.h>

using namespace std::chrono;

enum OutputFormat {
	SIXEL,
	GVRAM,
	PALETTEPNG,
};

Diag diag;
Diag diagHttp;
int opt_debug_sixel = 0;
int opt_debug_mbedtls;
static ReductorColorMode opt_colormode = ReductorColorMode::Fixed256;
static int opt_graylevel = 256;
static int opt_width = 0;
static int opt_height = 0;
static ResizeAxisMode opt_resizeaxis = ResizeAxisMode::Both;
static ReductorReduceMode opt_reduce = ReductorReduceMode::HighQuality;
static bool opt_outputpalette = true;
static bool opt_ignore_error = false;
bool opt_ormode = false;
static bool opt_profile = false;
static SixelResizeMode opt_resizemode = SixelResizeMode::ByLoad;
static OutputFormat opt_outputformat = OutputFormat::SIXEL;
static int opt_output_x = 0;
static int opt_output_y = 0;
static float opt_color_factor = 1.0f;
static ReductorDiffuseMethod opt_highqualitydiffusemethod =
	ReductorDiffuseMethod::RDM_FS;
static ReductorFinderMode opt_findermode = ReductorFinderMode::RFM_Default;
static int opt_addnoise = 0;
static int opt_address_family = AF_UNSPEC;

enum {
	OPT_8 = 0x80,
	OPT_16,
	OPT_256,
	OPT_addnoise,
	OPT_axis,
	OPT_color_factor,
	OPT_debug,
	OPT_debug_http,
	OPT_debug_mbedtls,
	OPT_debug_sixel,
	OPT_debug_tls,
	OPT_finder,
	OPT_gray,
	OPT_height,
	OPT_help,
	OPT_help_all,
	OPT_ignore_error,
	OPT_ipv4,
	OPT_ipv6,
	OPT_x68k,
	OPT_ormode,
	OPT_output_format,
	OPT_output_x,
	OPT_output_y,
	OPT_palette,
	OPT_profile,
	OPT_resize,
};

static const struct option longopts[] = {
	{ "8",				no_argument,		NULL,	OPT_8 },
	{ "16",				no_argument,		NULL,	OPT_16 },
	{ "256",			no_argument,		NULL,	OPT_256 },
	{ "addnoise",		required_argument,	NULL,	OPT_addnoise },
	{ "axis",			required_argument,	NULL,	OPT_axis },
	{ "color",			required_argument,	NULL,	'c' },
	{ "colors",			required_argument,	NULL,	'c' },
	{ "color-factor",	required_argument,	NULL,	OPT_color_factor },
	{ "debug",			required_argument,	NULL,	OPT_debug },
	{ "debug-http",		required_argument,	NULL,	OPT_debug_http },
	{ "debug-mbedtls",	required_argument,	NULL,	OPT_debug_mbedtls },
	{ "debug-sixel",	required_argument,	NULL,	OPT_debug_sixel },
	{ "diffusion",		required_argument,	NULL,	'd' },
	{ "finder",			required_argument,	NULL,	OPT_finder },
	{ "gray",			required_argument,	NULL,	OPT_gray },
	{ "height",			required_argument,	NULL,	'h' },
	{ "ignore-error",	no_argument,		NULL,	OPT_ignore_error },
	{ "ipv4",			no_argument,		NULL,	OPT_ipv4 },
	{ "ipv6",			no_argument,		NULL,	OPT_ipv6 },
	{ "monochrome",		no_argument,		NULL,	'e' },
	{ "ormode",			required_argument,	NULL,	OPT_ormode },
	{ "output-format",	required_argument,	NULL,	OPT_output_format },
	{ "output-x",		required_argument,	NULL,	OPT_output_x },
	{ "output-y",		required_argument,	NULL,	OPT_output_y },
	{ "palette",		required_argument,	NULL,	OPT_palette },
	{ "profile",		no_argument,		NULL,	OPT_profile },
	{ "resize",			required_argument,	NULL,	OPT_resize },
	{ "width",			no_argument,		NULL,	'w' },
	{ "x68k",			no_argument,		NULL,	OPT_x68k },
	{ "help",			no_argument,		NULL,	OPT_help },
	{ "help-all",		no_argument,		NULL,	OPT_help_all },
	{ NULL },
};

static std::map<const std::string, ReductorColorMode> colormode_map = {
	{ "8",					ReductorColorMode::Fixed8 },
	{ "16",					ReductorColorMode::FixedANSI16 },
	{ "256",				ReductorColorMode::Fixed256 },
	{ "256rgbi",			ReductorColorMode::Fixed256RGBI },
	{ "mono",				ReductorColorMode::Mono },
	{ "gray",				ReductorColorMode::Gray },
	{ "graymean",			ReductorColorMode::GrayMean },
	{ "x68k",				ReductorColorMode::FixedX68k },
};

static std::map<const std::string, ResizeAxisMode> resizeaxis_map = {
	{ "both",				ResizeAxisMode::Both },
	{ "w",					ResizeAxisMode::Width },
	{ "width",				ResizeAxisMode::Width },
	{ "h",					ResizeAxisMode::Height },
	{ "height",				ResizeAxisMode::Height },
	{ "long",				ResizeAxisMode::Long },
	{ "short",				ResizeAxisMode::Short },
	{ "sdboth",				ResizeAxisMode::ScaleDownBoth },
	{ "scaledown-both",		ResizeAxisMode::ScaleDownBoth },
	{ "sdw",				ResizeAxisMode::ScaleDownWidth },
	{ "sdwidth",			ResizeAxisMode::ScaleDownWidth },
	{ "scaledown-width",	ResizeAxisMode::ScaleDownWidth },
	{ "sdh",				ResizeAxisMode::ScaleDownHeight },
	{ "sdheight",			ResizeAxisMode::ScaleDownHeight },
	{ "scaledown-height",	ResizeAxisMode::ScaleDownHeight },
	{ "sdlong",				ResizeAxisMode::ScaleDownLong },
	{ "scaledown-long",		ResizeAxisMode::ScaleDownLong },
	{ "sdshort",			ResizeAxisMode::ScaleDownShort },
	{ "scaledown-short",	ResizeAxisMode::ScaleDownShort },
};

static std::map<const std::string, SixelResizeMode> resizemode_map = {
	{ "load",				SixelResizeMode::ByLoad },
	{ "imagereductor",		SixelResizeMode::ByImageReductor },
};

static std::map<const std::string, OutputFormat> outputformat_map = {
	{ "sixel",				OutputFormat::SIXEL },
	{ "gvram",				OutputFormat::GVRAM },
	{ "palettepng",			OutputFormat::PALETTEPNG },
};

static std::map<const std::string, ReductorFinderMode> findermode_map = {
	{ "default",			ReductorFinderMode::RFM_Default },
	{ "rgb",				ReductorFinderMode::RFM_Default },
	{ "hsv",				ReductorFinderMode::RFM_HSV },
};

#define RRM ReductorReduceMode
#define RDM ReductorDiffuseMethod
static std::map<const std::string, std::pair<RRM, RDM>> reduce_map = {
	{ "auto",		{ RRM::HighQuality,	(RDM)-1 } },
	{ "none",		{ RRM::Simple,		(RDM)-1 } },
	{ "fast",		{ RRM::Fast,		(RDM)-1 } },
	{ "high",		{ RRM::HighQuality,	(RDM)-1 } },
	{ "fs",			{ RRM::HighQuality,	RDM::RDM_FS } },
	{ "atkinson",	{ RRM::HighQuality,	RDM::RDM_ATKINSON } },
	{ "jajuni",		{ RRM::HighQuality,	RDM::RDM_JAJUNI } },
	{ "stucki",		{ RRM::HighQuality,	RDM::RDM_STUCKI } },
	{ "burkes",		{ RRM::HighQuality,	RDM::RDM_BURKES } },
	{ "2",			{ RRM::HighQuality,	RDM::RDM_2 } },
	{ "3",			{ RRM::HighQuality,	RDM::RDM_3 } },
	{ "rgb",		{ RRM::HighQuality,	RDM::RDM_RGB } },
};
#undef RRM
#undef RDM

[[noreturn]] static void usage(bool all = false);
static bool optbool(const char *arg);
static void Convert(const std::string& filename);
static void ConvertFromStream(Stream *stream);
static void signal_handler(int signo);

// map から key を検索する。
// 見付かればその値を返し、result に true を格納する。
// 見付からなければ(何かを返し) result に false を返す。
template <typename T> T
select_opt(const std::map<const std::string, T>& map, const char *key,
	bool *result)
{
	if (map.find(key) == map.end()) {
		*result = false;
		return T();
	}
	*result = true;
	return map.at(key);
}

int main(int ac, char *av[])
{
	struct utsname ut;
	int c;
	int val;
	bool res;

	diag.SetClassname("sixelv");
	diagHttp.SetClassname("HttpClient");

	// X68k なら、デフォルトで --x68k 相当にする。
	uname(&ut);
	if (strcmp(ut.machine, "x68k") == 0) {
		opt_colormode = ReductorColorMode::FixedX68k;
		opt_ormode = true;
		opt_outputpalette = false;
	}

	while ((c = getopt_long(ac, av, "c:d:eh:w:", longopts, NULL)) != -1) {
		switch (c) {
		 case OPT_debug:
			val = stou32def(optarg, -1);
			if (val < 0 || val > 2) {
				errx(1, "--debug %s: debug level must be 0..2", optarg);
			}
			diag.SetLevel(val);
			break;

		 case OPT_debug_http:
			val = stou32def(optarg, -1);
			if (val < 0 || val > 2) {
				errx(1, "--debug-http %s: debug level must be 0..2", optarg);
			}
			diagHttp.SetLevel(val);
			break;

		 case OPT_debug_mbedtls:
			val = stou32def(optarg, -1);
			if (val < 0 || val > 2) {
				errx(1, "--debug-mbedtls %s: debug level must be 0..4", optarg);
			}
			opt_debug_mbedtls = val;
			break;

		 case OPT_debug_sixel:
			val = stou32def(optarg, -1);
			if (val < 0 || val > 2) {
				errx(1, "--debug-sixel %s: debug level must be 0..2", optarg);
			}
			opt_debug_sixel = val;
			break;

		 case OPT_debug_tls:
			val = stou32def(optarg, -1);
			if (val < 0 || val > 2) {
				errx(1, "--debug-tls %s: debug level must be 0..2", optarg);
			}
			TLSHandleBase::SetLevel(val);
			break;

		 case 'e':
			opt_colormode = ReductorColorMode::Mono;
			break;

		 case OPT_gray:
			opt_graylevel = stou32def(optarg, 0);
			if (opt_graylevel < 2 || opt_graylevel > 256) {
				errx(1, "--gray %s: grayscale must be 2..256", optarg);
			}
			opt_colormode = ReductorColorMode::Gray;
			break;

		 case OPT_profile:
			opt_profile = true;
			break;

		 case 'c':
			opt_colormode = select_opt(colormode_map, optarg, &res);
			if (res == false) {
				errx(1, "--color %s: invalid parameter", optarg);
			}
			break;

		 case OPT_8:
			opt_colormode = ReductorColorMode::Fixed8;
			break;
		 case OPT_16:
			opt_colormode = ReductorColorMode::FixedANSI16;
			break;
		 case OPT_256:
			opt_colormode = ReductorColorMode::Fixed256;
			break;

		 case 'w':
			opt_width = stou32def(optarg, -1);
			if (opt_width < 0) {
				errno = EINVAL;
				err(1, "--width %s", optarg);
			}
			break;
		 case 'h':
			opt_height = stou32def(optarg, -1);
			if (opt_height < 0) {
				errno = EINVAL;
				err(1, "--height %s", optarg);
			}
			break;

		 case OPT_axis:
			opt_resizeaxis = select_opt(resizeaxis_map, optarg, &res);
			if (res == false) {
				errx(1, "--axis %s: invalid parameter", optarg);
			}
			break;

		 case 'd':
		 {
			auto [ reduce, diffuse ] = select_opt(reduce_map, optarg, &res);
			if (res == false) {
				errx(1, "--diffusion %s: invalid parameter", optarg);
			}
			opt_reduce = reduce;
			if (diffuse != (ReductorDiffuseMethod)-1) {
				opt_highqualitydiffusemethod = diffuse;
			}
			break;
		 }

		 case OPT_x68k:
			opt_colormode = ReductorColorMode::FixedX68k;
			opt_ormode = true;
			opt_outputpalette = false;
			break;

		 case OPT_ignore_error:
			opt_ignore_error = true;
			break;

		 case OPT_ipv4:
			opt_address_family = AF_INET;
			break;

		 case OPT_ipv6:
			opt_address_family = AF_INET6;
			break;

		 case OPT_ormode:
			opt_ormode = optbool(optarg);
			break;

		 case OPT_palette:
			opt_outputpalette = optbool(optarg);
			break;

		 case OPT_resize:
			opt_resizemode = select_opt(resizemode_map, optarg, &res);
			if (res == false) {
				errx(1, "--resize %s: invalid parameter", optarg);
			}
			break;

		 case OPT_output_format:
			opt_outputformat = select_opt(outputformat_map, optarg, &res);
			if (res == false) {
				errx(1, "--output-format %s: must be either 'sixel' or 'gvram'",
					optarg);
			}
			break;

		 case OPT_output_x:
			opt_output_x = stou32def(optarg, -1);
			if (opt_output_x < 0) {
				errx(1, "--output-x %s: offset must be >= 0", optarg);
			}
			break;
		 case OPT_output_y:
			opt_output_y = stou32def(optarg, -1);
			if (opt_output_y < 0) {
				errx(1, "--output-y %s: offset must be >= 0", optarg);
			}
			break;

		 case OPT_color_factor:
		 {
			char *end;

			errno = 0;
			opt_color_factor = strtof(optarg, &end);
			if (end == optarg || *end != '\0') {
				errno = EINVAL;
			}
			if (opt_color_factor < 0) {
				errno = EINVAL;
			}
			if (errno) {
				err(1, "--color-factor %s", optarg);
			}
			break;
		 }

		 case OPT_finder:
			opt_findermode = select_opt(findermode_map, optarg, &res);
			if (res == false) {
				errx(1, "--finder %s: must be either 'rgb' or 'hsv'", optarg);
			}
			break;

		 case OPT_addnoise:
			opt_addnoise = stou32def(optarg, -1);
			if (opt_addnoise < 0) {
				errno = EINVAL;
				err(1, "--addnoise %s", optarg);
			}
			break;

		 case OPT_help_all:
			usage(true);
			break;

		 default:
		 case OPT_help:
			usage(false);
			break;
		}
	}
	ac -= optind;
	av += optind;

	int nfiles;
	for (nfiles = 0; nfiles < ac; nfiles++) {
		if (nfiles > 0)
			printf("\n");
		Convert(av[nfiles]);
	}
	if (nfiles == 0) {
		if (!opt_ignore_error) {
			errx(1, "No input files");
		}
	}
	return 0;
}

// 引数をブール値にして返す
static bool
optbool(const char *arg_)
{
	std::string arg(arg_);

	if (arg == "yes" || arg == "on" || arg == "true") {
		return true;
	}
	return false;
}

static const char short_help[] = R"**(
   -c <color>, --color[s]=<color> : Select color mode (default: 256)
    <color> := 8, 16, 256, 256rgbi, mono, gray, graymean, x68k
   -8, -16, -256      : Shortcut for -c 8, -c 16, -c 256
   -e, --monochrome   : Shortcut for -c mono
   --gray=<graylevel> : Specify grayscale tone from 2 to 256 (default: 256)
   -w <width>         : Resize width to <width> pixel.
   -h <height>        : Resize height to <height> pixel.
   -d <type>, --diffusion=<type>  : Select diffuse algorithm (default: high)
    <type> := none, fast, high(=fs), auto(=high)
              fs, atkinson, jajuni, stucki, burkes, 2, 3, rgb
   --axis={both, w, width, h, height, long, short}
   --ignore-error                   --debug       <0..2>
   --profile                        --debug-http  <0..2>
   --help-all                       --debug-sixel <0..2>
)**";

static const char long_help[] = R"**( 
 color options
   -c <color>, --color[s]=<color> : Select color mode (default: 256)
       8        : Fixed 8 colors
       16       : Fixed 16 colors
       256      : Fixed 256 colors (MSX SCREEN 8 compatible palette)
       256rgbi  : Fixed 256 colors (R2G2B2I2 palette)
       mono     : monochrome (1bit)
       gray     : grayscale with NTSC intensity
       graymean : grayscale with mean of RGB
       x68k     : Fixed x68k 16 color palette
   -8, -16, -256   : Shortcut for -c 8, -c 16, -c 256
   -e, --monochrome: Shortcut for -c mono
   --gray=<graylevel> : Specify grayscale tone from 2 to 256 (default: 256)

 size options
   -w <width>, --width=<width>:    Resize width to <width> pixel.
   -h <height>, --height=<height>: Resize height to <height> pixel.
   --resize={load, imagereductor}
                   : Select resize algorighm (for debug).

 algorithm options
   -d <type>, --diffusion=<type>: Select diffuse algorithm. (default: high)
       none : Simple (No diffuse)   fs       : Floyd Steinberg
       fast : Fast algorithm        atkinson : Atkinson
       high : one of 2D Diffusion   jajuni   : Jarvis, Judice, Ninke
              algorithm listed      stucki   : Stucki
              right column.         burkes   : Burkes
              (default: fs)         2        : 2pixels (right, down)
       auto : alias to high         3        : 3pixels (right, down, rightdown)
                                    rgb      : for debug

 misc options
   --x68k             : alias to "-c x68k --ormode=on --palette=off"
   --ormode={on|off}  : Output OR-mode SIXEL. (default: off)
   --palette={on|off} : Output palette definition (default: on)
   --output-format={sixel, gvram}: Select output format (default: sixel)
   --output-x=<xoffset>, --output-y=<yoffset>
                      : Specify X, Y offset for gvram format file.
   --ipv4, --ipv6
   --ignore-error
   --axis={both, w, width, h, height, long, short}
   --color-factor=<factor>
   --finder={rgb, hsv} (default: rgb)
   --addnoise=<noiselevel>
   --debug       <0..2>
   --debug-http  <0..2>
   --debug-sixel <0..2>
   --profile
   --help, --help-all
)**";

static void
usage(bool all)
{
	fprintf(stderr, "usage: sixelv [<options>] <file>...\n");
	// 定義文字列は見やすさのため先頭に改行を入れてあるのでこっちで取り除く
	if (all) {
		fprintf(stderr, "%s", long_help + 1);
	} else {
		fprintf(stderr, "%s", short_help + 1);
	}
	exit(1);
}

// プロファイルID
enum {
	Profile_Start,
	Profile_Create,
	Profile_Load,
	Profile_Convert,
	Profile_Output,
	Profile_Max,
};
static const char *profile_name[] = {
	"(Start)",
	"Create",
	"Load",
	"Convert",
	"Output",
};

static void
Convert(const std::string& filename)
{
	// ソース別にストリームを作成
	if (filename == "-") {
		Debug(diag, "Loading stdin");
		FdStream stream(STDIN_FILENO, false);
		ConvertFromStream(&stream);

	} else if (filename.find("://") != std::string::npos) {
		Debug(diag, "Downloading %s", filename.c_str());
		HttpClient http(diagHttp);
		http.user_agent = "sixelv";
		if (http.Open(filename) == false) {
			warn("File error: %s", filename.c_str());
			goto error;
		}
		http.family = opt_address_family;
		Stream *stream = http.GET();
		if (stream == NULL) {
			// mbedTLS だと errno に入っていない。OpenSSL は未確認。
			warnx("Fetch error: %s", filename.c_str());
			goto error;
		}
		ConvertFromStream(stream);

	} else {
		Debug(diag, "Loading %s", filename.c_str());
		int fd = open(filename.c_str(), O_RDONLY);
		if (fd < 0) {
			warn("File load error: %s", filename.c_str());
			goto error;
		}
		FdStream stream(fd, true);
		ConvertFromStream(&stream);
	}
	return;

 error:
	if (opt_ignore_error == false) {
		exit(1);
	}
}

static void
ConvertFromStream(Stream *istream)
{
	// プロファイル時間
	time_point<system_clock> prof[Profile_Max];

	if (opt_profile) {
		prof[Profile_Start] = system_clock::now();
	}

	SixelConverter sx(opt_debug_sixel);
	ImageReductor& ir = sx.GetImageReductor();

	// SixelConverter モード設定
	sx.ColorMode = opt_colormode;
	sx.ReduceMode = opt_reduce;
	sx.ResizeMode = opt_resizemode;
	sx.OutputPalette = opt_outputpalette;
	sx.GrayCount = opt_graylevel;
	sx.FinderMode = opt_findermode;
	sx.AddNoiseLevel = opt_addnoise;
	sx.ResizeWidth = opt_width;
	sx.ResizeHeight = opt_height;
	sx.ResizeAxis = opt_resizeaxis;

	ir.HighQualityDiffuseMethod = opt_highqualitydiffusemethod;

	if (opt_ormode) {
		sx.OutputMode = SixelOutputMode::Or;
	} else {
		sx.OutputMode = SixelOutputMode::Normal;
	}

	if (opt_profile) {
		prof[Profile_Create] = system_clock::now();
	}

	if (sx.LoadFromStream(istream) == false) {
		warnx("Load error");
		if (opt_ignore_error) {
			return;
		}
		exit(1);
	}

	if (opt_profile) {
		prof[Profile_Load] = system_clock::now();
	}

	if (diag >= 1) {
		// これ以上詳しいパラメータは --debug-sixel 1 で SixelConverter 側で
		// 表示できているので、ここでは不要。
		std::string s = string_format("Converting axis=%s",
			ImageReductor::RAX2str(opt_resizeaxis));
		if (opt_width != 0 || opt_height != 0)
			s += string_format(" width=%d height=%d", opt_width, opt_height);
		diag.Print("%s", s.c_str());
	}
	sx.ConvertToIndexed();

	if (opt_profile) {
		prof[Profile_Convert] = system_clock::now();
	}

	if (opt_color_factor != 1.0) {
		ir.ColorFactor(opt_color_factor);
	}

	switch (opt_outputformat) {
	 case OutputFormat::SIXEL:
	 {
		signal(SIGINT, signal_handler);
		FileStream stream(stdout, false);
		sx.SixelToStream(&stream);
		stream.Flush();
		break;
	 }
	 case OutputFormat::GVRAM:
	 {
		if (opt_output_y + sx.GetHeight() > 512) {
			warnx("Image height %d is larger than GVRAM",
				opt_output_y + sx.GetHeight());
			return;
		}
		if (ir.GetPaletteCount() <= 16) {
			if (opt_output_x + sx.GetWidth() > 1024) {
				warnx("Image width %d is larger than 16-color mode GVRAM",
					opt_output_x + sx.GetWidth());
				return;
			}
		} else {
			if (opt_output_x + sx.GetWidth() > 512) {
				warnx("Image width %d is larger than 256-color mode GVRAM",
					opt_output_x + sx.GetWidth());
				return;
			}
		}

		std::vector<uint8> buf;
		union {
			uint16 w;
			uint8 b[2];
		} data;
		// バージョン番号 0x0001 in BE
		data.w = htobe16(0x0001);
		buf.emplace_back(data.b[0]);
		buf.emplace_back(data.b[1]);

		// パレット数
		data.w = htobe16(ir.GetPaletteCount());
		buf.emplace_back(data.b[0]);
		buf.emplace_back(data.b[1]);

		// X68k パレットを作る
		for (int i = 0; i < ir.GetPaletteCount(); i++) {
			auto col = ir.GetPalette(i);
			uint16 r = col.r >> 3;
			uint16 g = col.g >> 3;
			uint16 b = col.b >> 3;
			uint I = (col.r & 0x7) + (col.g & 0x7) + (col.b & 0x7);

			uint8 h = g << 3 | r >> 2;
			uint8 l = (r << 6 | b << 1) | (I > (21 / 2) ? 1 : 0);
			buf.emplace_back(h);
			buf.emplace_back(l);
		}
		// x, y, w, h を BE data で出す
		data.w = opt_output_x;
		buf.emplace_back(data.b[0]);
		buf.emplace_back(data.b[1]);
		data.w = opt_output_y;
		buf.emplace_back(data.b[0]);
		buf.emplace_back(data.b[1]);
		data.w = sx.GetWidth();
		buf.emplace_back(data.b[0]);
		buf.emplace_back(data.b[1]);
		data.w = sx.GetHeight();
		buf.emplace_back(data.b[0]);
		buf.emplace_back(data.b[1]);

		fwrite(buf.data(), 1, buf.size(), stdout);

		// GVRAM データを作る
		fwrite(sx.Indexed.data(), 1, sx.Indexed.size(), stdout);
		break;
	 }

	 case OutputFormat::PALETTEPNG:
	 {
#if 0
		// 11 x 11 はどうなのかとか。img2sixel 合わせだが、
		// img2sixel 側の問題でうまくいかない。
		const int width = ir.GetPaletteCount() * 11;
		const int height = 11;
		const int channels = 3;
		const int stride = width * channels;	// ?
		Image img(width, height, stride, channels);
		uint8 *p = img.GetBuf();
		for (int y = 0; y < height; y++) {
			for (int i = 0; i < ir.GetPaletteCount(); i++) {
				auto col = ir.GetPalette(i);
				for (int x = 0; x < width; x++) {
					p[0] = col.r;
					p[1] = col.g;
					p[2] = col.b;
					p += 3;
				}
			}
		}
		// XXX PNG に出力
#else
		warn("output-format=palettepng is not supported");
#endif
		break;
	 }
	}

	if (opt_profile) {
		prof[Profile_Output] = system_clock::now();
	}

	if (opt_profile) {
		double usec;
		for (int i = 1; i < Profile_Max; i++) {
			usec = duration_cast<microseconds>(prof[i] - prof[i - 1]).count();
			fprintf(stderr, "%-7s %.3fms\n", profile_name[i], usec / 1000);
		}

		auto& start = prof[Profile_Start];
		auto& end   = prof[Profile_Output];
		usec = duration_cast<microseconds>(end - start).count();
		fprintf(stderr, "Total   %.3fms\n", usec / 1000);
	}
}

static void
signal_handler(int signo)
{
	switch (signo) {
	 case SIGINT:
		// SIXEL 出力を中断する (CAN + ST)
		printf(CAN ESC "\\");
		fflush(stdout);
		break;
	}
}
