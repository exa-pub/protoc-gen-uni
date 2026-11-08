/* Copyright (c) 2011-2019 Canonical Ltd
 * Licensed under the Apache License, Version 2.0 (see LICENSE of go.yaml.in/yaml/v3). */

/* Port of go.yaml.in/yaml/v3 v3.0.4 resolve.go: the implicit tag of a plain scalar (resolve with
 * an empty tag, the only use the Node encoder and parser make of it), shortTag and longTag. The
 * value conversions are not needed: only whether strconv.ParseInt, ParseUint, ParseFloat and
 * time.Parse (Go 1.25) accept the text. */
#include "internal.h"

#include "go_std.h"

#include <math.h>
#include <stdlib.h>

#define LONG_TAG_PREFIX "tag:yaml.org,2002:"

Str yaml_short_tag(Str tag)
{
	Str prefix = STR_LIT(LONG_TAG_PREFIX);
	if (str_has_prefix(tag, prefix)) {
		/* shortTags[tag] or "!!" + the rest: the same text */
		Buf b = {0};
		buf_adds(&b, "!!");
		buf_add(&b, tag.p + prefix.n, tag.n - prefix.n);
		return buf_view(&b);
	}
	return tag;
}

Str yaml_long_tag(Str tag)
{
	if (str_has_prefix(tag, STR_LIT("!!"))) {
		Buf b = {0};
		buf_adds(&b, LONG_TAG_PREFIX);
		buf_add(&b, tag.p + 2, tag.n - 2);
		return buf_view(&b);
	}
	return tag;
}

/* strconv.ParseFloat(s, 64) accepts s: 0, or 1 (ErrSyntax), 2 (ErrRange: overflow to ±Inf). */
static int parse_float(Str s)
{
	/* special(): infinity and NaN */
	{
		size_t i = 0;
		if (i < s.n && (s.p[i] == '+' || s.p[i] == '-'))
			i++;
		Str r = {s.p + i, s.n - i};
		static const char *const specials[] = {"infinity", "inf", "nan"};
		for (size_t k = 0; k < 3; k++) {
			size_t n = strlen(specials[k]);
			if (r.n < n)
				continue;
			bool eq = true;
			for (size_t j = 0; j < n && eq; j++)
				eq = (r.p[j] | 0x20) == specials[k][j];
			if (!eq)
				continue;
			if (k == 2 && i > 0)
				break; /* "nan" takes no sign */
			return i + n == s.n ? 0 : 1;
		}
	}
	/* readFloat */
	size_t i = 0;
	bool underscores = false, hex = false;
	if (i >= s.n)
		return 1;
	if (s.p[i] == '+' || s.p[i] == '-')
		i++;
	char exp_char = 'e';
	if (i + 2 < s.n && s.p[i] == '0' && (s.p[i + 1] | 0x20) == 'x') {
		i += 2;
		exp_char = 'p';
		hex = true;
	}
	bool sawdot = false, sawdigits = false;
	for (; i < s.n; i++) {
		char c = s.p[i];
		if (c == '_') {
			underscores = true;
			continue;
		}
		if (c == '.') {
			if (sawdot)
				break;
			sawdot = true;
			continue;
		}
		if ('0' <= c && c <= '9') {
			sawdigits = true;
			continue;
		}
		if (hex && 'a' <= (c | 0x20) && (c | 0x20) <= 'f') {
			sawdigits = true;
			continue;
		}
		break;
	}
	if (!sawdigits)
		return 1;
	if (i < s.n && (s.p[i] | 0x20) == exp_char) {
		i++;
		if (i >= s.n)
			return 1;
		if (s.p[i] == '+' || s.p[i] == '-')
			i++;
		if (i >= s.n || s.p[i] < '0' || s.p[i] > '9')
			return 1;
		for (; i < s.n && (('0' <= s.p[i] && s.p[i] <= '9') || s.p[i] == '_'); i++)
			if (s.p[i] == '_')
				underscores = true;
	} else if (hex) {
		return 1; /* Must have exponent. */
	}
	if (underscores) {
		/* underscoreOK(s[:i]) */
		char saw = '^';
		size_t k = 0;
		const char *u = s.p;
		size_t un = i;
		if (un >= 1 && (u[0] == '-' || u[0] == '+'))
			u++, un--;
		bool uhex = false;
		if (un >= 2 && u[0] == '0' && ((u[1] | 0x20) == 'b' || (u[1] | 0x20) == 'o' || (u[1] | 0x20) == 'x')) {
			k = 2;
			saw = '0';
			uhex = (u[1] | 0x20) == 'x';
		}
		for (; k < un; k++) {
			if (('0' <= u[k] && u[k] <= '9') || (uhex && 'a' <= (u[k] | 0x20) && (u[k] | 0x20) <= 'f')) {
				saw = '0';
				continue;
			}
			if (u[k] == '_') {
				if (saw != '0')
					return 1;
				saw = '_';
				continue;
			}
			if (saw == '_')
				return 1;
			saw = '!';
		}
		if (saw == '_')
			return 1;
	}
	if (i != s.n)
		return 1;
	/* The value: correctly rounded like Go; overflow is the only error. */
	Buf b = {0};
	for (size_t k = 0; k < s.n; k++)
		if (s.p[k] != '_')
			buf_addc(&b, s.p[k]);
	double f = strtod(buf_str(&b), NULL);
	return isinf(f) ? 2 : 0;
}

/* `^[-+]?(\.[0-9]+|[0-9]+(\.[0-9]*)?)([eE][-+]?[0-9]+)?$` */
static bool yaml_style_float(Str s)
{
	size_t i = 0;
	if (i < s.n && (s.p[i] == '-' || s.p[i] == '+'))
		i++;
	if (i < s.n && s.p[i] == '.') {
		i++;
		size_t d = i;
		while (i < s.n && s.p[i] >= '0' && s.p[i] <= '9')
			i++;
		if (i == d)
			return false;
	} else {
		size_t d = i;
		while (i < s.n && s.p[i] >= '0' && s.p[i] <= '9')
			i++;
		if (i == d)
			return false;
		if (i < s.n && s.p[i] == '.') {
			i++;
			while (i < s.n && s.p[i] >= '0' && s.p[i] <= '9')
				i++;
		}
	}
	if (i < s.n && (s.p[i] == 'e' || s.p[i] == 'E')) {
		size_t save = i;
		i++;
		if (i < s.n && (s.p[i] == '-' || s.p[i] == '+'))
			i++;
		size_t d = i;
		while (i < s.n && s.p[i] >= '0' && s.p[i] <= '9')
			i++;
		if (i == d)
			i = save; /* the exponent group does not match: then $ must match at save */
	}
	return i == s.n;
}

/* ---- time.Parse (Go 1.25) for the layouts of allowedTimestampFormats ---- */

/* getnum parses s[0:1] or s[0:2] (fixed forces s[0:2]) as a decimal integer. */
static bool getnum(Str *s, bool fixed, int *out)
{
	if (!is_digit(*s, 0))
		return false;
	if (!is_digit(*s, 1)) {
		if (fixed)
			return false;
		*out = s->p[0] - '0';
		s->p++, s->n--;
		return true;
	}
	*out = (s->p[0] - '0') * 10 + (s->p[1] - '0');
	s->p += 2, s->n -= 2;
	return true;
}

static Str cutspace(Str s)
{
	while (s.n > 0 && s.p[0] == ' ')
		s.p++, s.n--;
	return s;
}

/* skip removes the given prefix from value, treating runs of space characters as equivalent. */
static bool skip(Str *value, const char *prefix)
{
	while (*prefix) {
		if (*prefix == ' ') {
			if (value->n > 0 && value->p[0] != ' ')
				return false;
			while (*prefix == ' ')
				prefix++;
			*value = cutspace(*value);
			continue;
		}
		if (value->n == 0 || value->p[0] != *prefix)
			return false;
		prefix++;
		value->p++, value->n--;
	}
	return true;
}

static bool is_leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

static int days_in(int month, int year)
{
	static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (month == 2 && is_leap(year))
		return 29;
	return days[month - 1];
}

/* time.Parse(layout, s) succeeds; layout 0..3: "2006-1-2T15:4:5.999999999Z07:00",
 * "2006-1-2t15:4:5.999999999Z07:00", "2006-1-2 15:4:5.999999999", "2006-1-2". */
static bool time_parse(int layout, Str value)
{
	int year = 0, month = -1, day = -1, hour = 0, min = 0, sec = 0;
	/* stdLongYear (parseTimestamp has checked that value starts with four digits) */
	if (value.n < 4)
		return false;
	for (int k = 0; k < 4; k++)
		year = year * 10 + (value.p[k] - '0');
	value.p += 4, value.n -= 4;
	if (!skip(&value, "-"))
		return false;
	/* stdNumMonth */
	if (!getnum(&value, false, &month))
		return false;
	if (month <= 0 || 12 < month)
		return false;
	if (!skip(&value, "-"))
		return false;
	/* stdDay */
	if (!getnum(&value, false, &day))
		return false;
	if (layout < 3) {
		if (!skip(&value, layout == 0 ? "T" : layout == 1 ? "t" : " "))
			return false;
		/* stdHour */
		if (!getnum(&value, false, &hour))
			return false;
		if (hour < 0 || 24 <= hour)
			return false;
		if (!skip(&value, ":"))
			return false;
		/* stdMinute */
		if (!getnum(&value, false, &min))
			return false;
		if (min < 0 || 60 <= min)
			return false;
		if (!skip(&value, ":"))
			return false;
		/* stdSecond */
		if (!getnum(&value, false, &sec))
			return false;
		if (sec < 0 || 60 <= sec)
			return false;
		/* stdFracSecond9: any number of digits after a period or comma */
		if (value.n >= 2 && (value.p[0] == '.' || value.p[0] == ',') && value.p[1] >= '0' && value.p[1] <= '9') {
			size_t i = 0;
			while (i + 1 < value.n && value.p[i + 1] >= '0' && value.p[i + 1] <= '9')
				i++;
			value.p += 1 + i, value.n -= 1 + i;
		}
		if (layout < 2) {
			/* stdISO8601ColonTZ */
			if (value.n >= 1 && value.p[0] == 'Z') {
				value.p++, value.n--;
			} else {
				if (value.n < 6 || value.p[3] != ':')
					return false;
				Str hr = {value.p + 1, 2}, mm = {value.p + 4, 2};
				char sign = value.p[0];
				int h, m;
				if (!getnum(&hr, true, &h) || !getnum(&mm, true, &m))
					return false;
				if (h > 24 || m > 60)
					return false;
				if (sign != '+' && sign != '-')
					return false;
				value.p += 6, value.n -= 6;
			}
		}
	}
	if (value.n > 0)
		return false; /* extra text */
	if (day < 1 || day > days_in(month, year))
		return false;
	return true;
}

/* parseTimestamp */
static bool parse_timestamp(Str s)
{
	/* Quick check: all date formats start with YYYY-. */
	size_t i = 0;
	for (; i < s.n; i++) {
		char c = s.p[i];
		if (c < '0' || c > '9')
			break;
	}
	if (i != 4 || i == s.n || s.p[i] != '-')
		return false;
	for (int layout = 0; layout < 4; layout++)
		if (time_parse(layout, s))
			return true;
	return false;
}

static bool parse_int_ok(Str s, int base)
{
	int64_t v;
	return go_std_parse_int(s.p, s.n, base, 64, &v) == 0;
}

static bool parse_uint_ok(Str s, int base)
{
	uint64_t v;
	return go_std_parse_uint(s.p, s.n, base, 64, &v) == 0;
}

const char *yaml_resolve_tag(Str in)
{
	static const char *const map_bool[] = {"true", "True", "TRUE", "false", "False", "FALSE"};
	static const char *const map_null[] = {"", "~", "null", "Null", "NULL"};
	static const char *const map_float[] = {".nan", ".NaN", ".NAN", ".inf", ".Inf", ".INF", "+.inf", "+.Inf", "+.INF", "-.inf", "-.Inf", "-.INF"};

	char hint = 'N';
	if (in.n > 0) {
		uint8_t c = (uint8_t)in.p[0];
		if (c == '+' || c == '-')
			hint = 'S';
		else if (c >= '0' && c <= '9')
			hint = 'D';
		else if (c && strchr("yYnNtTfFoO~", c))
			hint = 'M';
		else if (c == '.')
			hint = '.';
		else
			hint = 0;
	}
	if (hint != 0) {
		for (size_t i = 0; i < sizeof map_bool / sizeof *map_bool; i++)
			if (str_eqc(in, map_bool[i]))
				return "!!bool";
		for (size_t i = 0; i < sizeof map_null / sizeof *map_null; i++)
			if (str_eqc(in, map_null[i]))
				return "!!null";
		for (size_t i = 0; i < sizeof map_float / sizeof *map_float; i++)
			if (str_eqc(in, map_float[i]))
				return "!!float";
		if (str_eqc(in, "<<"))
			return "!!merge";

		switch (hint) {
		case 'M':
			/* We've already checked the map above. */
			break;
		case '.':
			/* Not in the map, so maybe a normal float. */
			if (parse_float(in) == 0)
				return "!!float";
			break;
		case 'D':
		case 'S': {
			/* Int, float, or timestamp. Only try values as a timestamp if the value is
			 * unquoted or there's an explicit !!timestamp tag. */
			if (parse_timestamp(in))
				return "!!timestamp";
			Buf b = {0};
			for (size_t i = 0; i < in.n; i++)
				if (in.p[i] != '_')
					buf_addc(&b, in.p[i]);
			Str plain = buf_view(&b);
			if (parse_int_ok(plain, 0))
				return "!!int";
			if (parse_uint_ok(plain, 0))
				return "!!int";
			if (yaml_style_float(plain) && parse_float(plain) == 0)
				return "!!float";
			if (str_has_prefix(plain, STR_LIT("0b"))) {
				Str r = {plain.p + 2, plain.n - 2};
				if (parse_int_ok(r, 2) || parse_uint_ok(r, 2))
					return "!!int";
			} else if (str_has_prefix(plain, STR_LIT("-0b"))) {
				Buf m = {0};
				buf_addc(&m, '-');
				buf_add(&m, plain.p + 3, plain.n - 3);
				if (parse_int_ok(buf_view(&m), 2))
					return "!!int";
			}
			/* Octals as introduced in version 1.2 of the spec. Octals from the 1.1 spec,
			 * spelled as 0777, are still decoded by default in v3 as well for
			 * compatibility. May be dropped in v4 depending on how usage evolves. */
			if (str_has_prefix(plain, STR_LIT("0o"))) {
				Str r = {plain.p + 2, plain.n - 2};
				if (parse_int_ok(r, 8) || parse_uint_ok(r, 8))
					return "!!int";
			} else if (str_has_prefix(plain, STR_LIT("-0o"))) {
				Buf m = {0};
				buf_addc(&m, '-');
				buf_add(&m, plain.p + 3, plain.n - 3);
				if (parse_int_ok(buf_view(&m), 8))
					return "!!int";
			}
			break;
		}
		}
	}
	return "!!str";
}
