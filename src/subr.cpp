#include "StringUtil.h"
#include "subr.h"
#include <iconv.h>
#include <sys/endian.h>

// 雑多なサブルーチン

// 名前表示用に整形
std::string
formatname(const std::string& text)
{
	std::string rv = unescape(text);
	rv = string_replace(rv, "\r\n", " ");
	rv = string_replace(rv, "\r", " ");
	rv = string_replace(rv, "\n", " ");
	return rv;
}

// ID 表示用に整形
std::string
formatid(const std::string& text)
{
	return "@" + text;
}

// HTML のエスケープを元に戻す
std::string
unescape(const std::string& text)
{
	std::string rv = text;
	rv = string_replace(rv, "&lt;", "<");
	rv = string_replace(rv, "&gt;", ">");
	rv = string_replace(rv, "&amp;", "&");
	return rv;
}

// HTML タグを取り除いた文字列を返す
std::string
strip_tags(const std::string& text)
{
	std::string sb;
	bool intag = false;
	for (const auto& c : text) {
		if (intag) {
			if (c == '>') {
				intag = false;
			}
		} else {
			if (c == '<') {
				intag = true;
			} else {
				sb += c;
			}
		}
	}
	return sb;
}

// 現在時刻を返す
static time_t
Now()
{
#if defined(SELFTEST)
	// 固定の時刻を返す (2009/11/18 18:54:12)
	return 1258538052;
#else
	// 現在時刻を返す
	return time(NULL);
#endif
}

std::string
formattime(const Json& obj)
{
	char buf[64];

	// 現在時刻
	time_t now = Now();
	struct tm ntm;
	localtime_r(&now, &ntm);

	// obj の日時を取得
	time_t dt = get_datetime(obj);
	struct tm dtm;
	localtime_r(&dt, &dtm);

	const char *fmt;
	if (dtm.tm_year == ntm.tm_year && dtm.tm_yday == ntm.tm_yday) {
		// 今日なら時刻のみ
		fmt = "%T";
	} else if (dtm.tm_year == ntm.tm_year) {
		// 昨日以前で今年中なら年を省略 (mm/dd HH:MM:SS)
		// XXX 半年以内ならくらいのほうがいいのか?
		fmt = "%m/%d %T";
	} else {
		// 去年以前なら yyyy/mm/dd HH:MM (秒はもういいだろう…)
		fmt = "%Y/%m/%d %R";
	}
	strftime(buf, sizeof(buf), fmt, &dtm);
	return std::string(buf);
}

// status の日付時刻を返す。
// timestamp_ms があれば使い、なければ created_at を使う。
// 今のところ、timestamp_ms はたぶん新しめのツイート/イベント通知には
// 付いてるはずだが、リツイートされた側は created_at しかない模様。
time_t
get_datetime(const Json& status)
{
	time_t unixtime;

	if (status.contains("timestamp_ms")) {
		// 数値のようにみえる文字列で格納されている
		const std::string& timestamp_ms = status["timestamp_ms"];
		unixtime = (time_t)(std::stol(timestamp_ms) / 1000);
	} else {
		const std::string& created_at = status["created_at"];
		unixtime = conv_twtime_to_unixtime(created_at);
	}
	return unixtime;
}

// Twitter 書式の日付時刻から Unixtime を返す。
// "Wed Nov 18 18:54:12 +0000 2009"
time_t
conv_twtime_to_unixtime(const std::string& instr)
{
	auto w = Split(instr, " ");
	auto& monname = w[1];
	int mday = std::stoi(w[2]);
	auto timestr = w[3];
	int year = std::stoi(w[5]);

	static const std::string monnames = "JanFebMarAprMayJunJulAugSepOctNovDec";
	int mon0 = monnames.find(monname);
	mon0 = (mon0 / 3);

	auto t = Split(timestr, ":");
	int hour = std::stoi(t[0]);
	int min  = std::stoi(t[1]);
	int sec  = std::stoi(t[2]);

	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = year - 1900;
	tm.tm_mon  = mon0;
	tm.tm_mday = mday;
	tm.tm_hour = hour;
	tm.tm_min  = min;
	tm.tm_sec  = sec;
	// タイムゾーン欄 (+0000) は常に 0 っぽいので、他は対応してない
	return mktime_z(NULL, &tm);
}

// strptime() っぽい俺様版。
// "%a" と "%R" だけ対応し、戻り値は int。
int
my_strptime(const std::string& buf, const std::string& fmt)
{
	if (fmt == "%a") {
		static const std::array<std::string, 7> wdays = {
			"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
		};
		for (int i = 0; i < wdays.size(); i++) {
			if (strcasecmp(buf.c_str(), wdays[i].c_str()) == 0) {
				return i;
			}
		}
		return -1;
	}

	if (fmt == "%R") {
		auto hhmm = Split(buf, ":");
		if (hhmm.size() != 2) {
			return -1;
		}
		auto hh = hhmm[0];
		auto mm = hhmm[1];
		if (hh.size() < 1 || hh.size() > 2) {
			return -1;
		}
		if (mm.size() < 1 || mm.size() > 2) {
			return -1;
		}
		auto h = std::stoi(hh);
		auto m = std::stoi(mm);
		if (h < 0 || m < 0) {
			return -1;
		}
		return (h * 60) + m;
	}

	return -1;
}

// UTF-8 入力文字列 utf8str を Unicode コードポイントの配列に変換する。
// といいつつ UTF-32 なのだが (実際は別物)。
// 変換できなければ空配列を返す。
std::vector<unichar>
Utf8ToUnicode(const std::string& utf8str)
{
	iconv_t cd;
	std::vector<unichar> unistr;

#if _BYTE_ORDER == _LITTLE_ENDIAN
#define UTF32_HE "utf-32le"
#else
#define UTF32_HE "utf-32be"
#endif
	cd = iconv_open(UTF32_HE, "utf-8");
	if (cd == (iconv_t)-1) {
		return unistr;
	}

	size_t srcleft = utf8str.size();
	std::vector<char> srcbuf(srcleft + 1);
	std::vector<char> dstbuf(srcleft * 4 + 1);
	memcpy(srcbuf.data(), utf8str.c_str(), srcbuf.size());
	const char *src = srcbuf.data();
	char *dst = dstbuf.data();
	size_t dstlen = dstbuf.size();
	auto r = iconv(cd, &src, &srcleft, &dst, &dstlen);
	if (r < 0) {
		iconv_close(cd);
		return unistr;
	}
	if (r > 0) {
		// 戻り値は invalid conversion の数
		iconv_close(cd);
		// どうすべ
		errno = 0;
		return unistr;
	}

	// デバッグ用
	if (0) {
		printf("src=+%x srcleft=%d->%d dst=+%x dstlen=%d:",
			(int)(src-srcbuf.data()),
			(int)utf8str.size(),
			(int)srcleft,
			(int)(dst-dstbuf.data()),
			(int)dstlen);
		for (int i = 0; i < (dst - dstbuf.data()); i++) {
			printf(" %02x", (unsigned char)dstbuf[i]);
		}
		printf("\n");
	}

	const uint32_t *s = (const uint32_t *)dstbuf.data();
	const uint32_t *e = (const uint32_t *)dst;
	while (s < e) {
		unistr.emplace_back(*s++);
	}
	return unistr;
}

// Unicode コードポイント配列を UTF-8 文字列に変換する。
// Unicode コードポイントといいつつ実際は UTF-32 なのだが。
// 変換できなければ "" を返す。
std::string
UnicodeToUtf8(const std::vector<unichar>& ustr)
{
	iconv_t cd;
	std::string str;

	cd = iconv_open("utf-8", UTF32_HE);
	if (cd == (iconv_t)-1) {
		return str;
	}

	size_t srcleft = ustr.size() * 4;
	std::vector<char> srcbuf(srcleft);
	std::vector<char> dstbuf(srcleft);	// 足りるはず?
	memcpy(srcbuf.data(), ustr.data(), ustr.size() * 4);
	const char *src = srcbuf.data();
	char *dst = dstbuf.data();
	size_t dstlen = dstbuf.size();
	auto r = iconv(cd, &src, &srcleft, &dst, &dstlen);
	if (r < 0) {
		iconv_close(cd);
		return str;
	}
	if (r > 0) {
		// 戻り値は invalid conversion の数
		iconv_close(cd);
		// どうすべ
		errno = 0;
		return str;
	}

	const char *s = (const char *)dstbuf.data();
	const char *e = (const char *)dst;
	while (s < e) {
		str += *s++;
	}
	return str;
}

#if defined(SELFTEST)
#include "test.h"

void
test_formattime()
{
	printf("%s\n", __func__);

	// テスト中は Now() が固定時刻を返す
	std::vector<std::array<std::string, 2>> table = {
		// 入力時刻							期待値
		{ "Wed Nov 18 11:54:12 +0000 2009",	"20:54:12" },			// 同日
		{ "Tue Nov 17 09:54:12 +0000 2009", "11/17 18:54:12" },		// 年内
		{ "Tue Nov 18 11:54:12 +0000 2008", "2008/11/18 20:54" },	// それ以前
	};
	for (const auto& a : table) {
		const auto& inp = a[0];
		const auto& exp = a[1];

		Json json = Json::parse("{\"created_at\":\"" + inp + "\"}");
		auto actual = formattime(json);
		xp_eq(exp, actual, inp);
	}
}

void
test_get_datetime()
{
	printf("%s\n", __func__);

	std::vector<std::pair<std::string, time_t>> table = {
		{ R"( "timestamp_ms":"1234999" )",	1234 },
		{ R"( "created_at":"Wed Nov 18 09:54:12 +0000 2009" )", 1258538052 },
	};
	for (const auto& a : table) {
		auto& src = a.first;
		time_t exp = a.second;

		Json json = Json::parse("{" + src + "}");
		auto actual = get_datetime(json);
		xp_eq(exp, actual, src);
	}
}

void
test_my_strptime()
{
	printf("%s\n", __func__);

	std::vector<std::tuple<std::string, std::string, int>> table = {
		{ "%a",	"Sun",	0 },
		{ "%a", "mon",	1 },
		{ "%a", "tue",	2 },
		{ "%a", "WED",	3 },
		{ "%a", "THU",	4 },
		{ "%a", "fri",	5 },
		{ "%a", "sAT",	6 },
		{ "%a", "",		-1 },

		{ "%R", "00:00",	0 },
		{ "%R", "00:01",	1 },
		{ "%R", "01:02",	62 },
		{ "%R", "23:59",	1439 },
		{ "%R", "24:01",	1441 },
		{ "%R",	"00:01:02",	-1 },
		{ "%R",	"00",		-1 },
		{ "%R", "-1:-1",	-1 },
		{ "%R",	"02:",		-1 },
		{ "%R",	":",		-1 },
		{ "%R", "0:2",		2 },	// 悩ましいが弾くほどでもないか
	};
	for (const auto& a : table) {
		const auto& fmt = std::get<0>(a);
		const auto& buf = std::get<1>(a);
		int exp = std::get<2>(a);

		auto actual = my_strptime(buf, fmt);
		xp_eq(exp, actual, fmt + "," + buf);
	}
}

void
test_Utf8ToUnicode()
{
	printf("%s\n", __func__);

	std::vector<std::pair<std::string, std::vector<unichar>>> table = {
		// input				expected
		{ "AB\n",				{ 0x41, 0x42, 0x0a } },
		{ "亜",					{ 0x4e9c } },
		{ "￥",					{ 0xffe5 } },	// FULLWIDTH YEN SIGN
		{ "\xf0\x9f\x98\xad",	{ 0x1f62d } },	// LOUDLY CRYING FACE
	};

	// Utf8ToUnicode()
	for (const auto& a : table) {
		const auto& input = a.first;
		const auto& expected = a.second;

		auto actual = Utf8ToUnicode(input);
		if (expected.size() == actual.size()) {
			for (int i = 0; i < expected.size(); i++) {
				xp_eq(expected[i], actual[i], input);
			}
		} else {
			xp_eq(expected.size(), actual.size(), input);
		}
	}

	// UnicodeToUtf8()
	for (const auto& a : table) {
		const auto& expected = a.first;
		const auto& input = a.second;

		auto actual = UnicodeToUtf8(input);
		xp_eq(expected, actual, expected);
	}
}

void
test_subr()
{
	test_formattime();
	test_get_datetime();
	test_my_strptime();
	test_Utf8ToUnicode();
}
#endif
