/* Copyright 2010 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* time.Parse (format.go, format_rfc3339.go) for the layouts time.RFC3339
 * ("2006-01-02T15:04:05Z07:00") and "2006-01-02": whether the value parses. */

static bool time_isDigit(Str s, size_t i) { return i < s.n && s.p[i] >= '0' && s.p[i] <= '9'; }

static bool time_isLeap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

static int time_daysIn(int month, int year)
{
	static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (month == 2 && time_isLeap(year))
		return 29;
	return month >= 1 && month <= 12 ? days[month - 1] : 31;
}

/* getnum parses s[0:1] or s[0:2] (fixed forces s[0:2]) as a decimal integer; -1 on error */
static int time_getnum(Str *s, bool fixed)
{
	if (!time_isDigit(*s, 0))
		return -1;
	if (!time_isDigit(*s, 1)) {
		if (fixed)
			return -1;
		int v = s->p[0] - '0';
		s->p++, s->n--;
		return v;
	}
	int v = (s->p[0] - '0') * 10 + (s->p[1] - '0');
	s->p += 2, s->n -= 2;
	return v;
}

static bool time_literal(Str *s, char c)
{
	if (!s->n || s->p[0] != c)
		return false;
	s->p++, s->n--;
	return true;
}

/* stdLongYear: four characters, the first a digit, all of them an integer (atoi) */
static bool time_year(Str *s, int *year)
{
	if (s->n < 4 || !time_isDigit(*s, 0))
		return false;
	int y = 0;
	for (int i = 0; i < 4; i++) {
		if (!time_isDigit(*s, (size_t)i))
			return false;
		y = y * 10 + (s->p[i] - '0');
	}
	*year = y;
	s->p += 4, s->n -= 4;
	return true;
}

/* the date part of both layouts (the general parser), with the range checks */
static bool time_date(Str *s, int *year, int *month, int *day)
{
	if (!time_year(s, year) || !time_literal(s, '-'))
		return false;
	*month = time_getnum(s, true);
	if (*month < 1 || *month > 12 || !time_literal(s, '-'))
		return false;
	*day = time_getnum(s, true);
	return *day >= 0;
}

/* parseRFC3339: the fast path */
static bool time_parseRFC3339(Str s)
{
	bool ok = true;
#define PARSE_UINT(off, len, min, max, out)                                                                            \
	do {                                                                                                           \
		int x = 0;                                                                                             \
		for (int i_ = 0; i_ < (len); i_++) {                                                                   \
			char c = s.p[(off) + i_];                                                                       \
			if (c < '0' || c > '9') {                                                                       \
				ok = false;                                                                             \
				x = (min);                                                                              \
				goto done_##out;                                                                        \
			}                                                                                              \
			x = x * 10 + (c - '0');                                                                        \
		}                                                                                                      \
		if (x < (min) || (max) < x) {                                                                          \
			ok = false;                                                                                     \
			x = (min);                                                                                      \
		}                                                                                                      \
	done_##out:                                                                                                    \
		out = x;                                                                                               \
	} while (0)
	if (s.n < 19)
		return false;
	int year, month, day, hour, min, sec;
	PARSE_UINT(0, 4, 0, 9999, year);
	PARSE_UINT(5, 2, 1, 12, month);
	PARSE_UINT(8, 2, 1, time_daysIn(month, year), day);
	PARSE_UINT(11, 2, 0, 23, hour);
	PARSE_UINT(14, 2, 0, 59, min);
	PARSE_UINT(17, 2, 0, 59, sec);
	(void)day, (void)hour, (void)min, (void)sec;
	if (!ok || !(s.p[4] == '-' && s.p[7] == '-' && s.p[10] == 'T' && s.p[13] == ':' && s.p[16] == ':'))
		return false;
	s.p += 19, s.n -= 19;
	/* Parse the fractional second. */
	if (s.n >= 2 && s.p[0] == '.' && time_isDigit(s, 1)) {
		size_t n = 2;
		while (n < s.n && time_isDigit(s, n))
			n++;
		s.p += n, s.n -= n;
	}
	/* Parse the time zone. */
	if (s.n != 1 || s.p[0] != 'Z') {
		if (s.n != 6)
			return false;
		int hr, mm;
		PARSE_UINT(1, 2, 0, 23, hr);
		PARSE_UINT(4, 2, 0, 59, mm);
		(void)hr, (void)mm;
		if (!ok || !((s.p[0] == '-' || s.p[0] == '+') && s.p[3] == ':'))
			return false;
	}
	return true;
#undef PARSE_UINT
}

bool go_std_time_parse(const char *layout, Str value)
{
	Str s = value;
	int year, month, day;
	if (strcmp(layout, "2006-01-02") == 0) {
		if (!time_date(&s, &year, &month, &day) || s.n)
			return false;
		return day >= 1 && day <= time_daysIn(month, year);
	}
	/* time.RFC3339 */
	if (time_parseRFC3339(value))
		return true;
	if (!time_date(&s, &year, &month, &day) || !time_literal(&s, 'T'))
		return false;
	int hour = time_getnum(&s, false);
	if (hour < 0 || hour >= 24 || !time_literal(&s, ':'))
		return false;
	int min = time_getnum(&s, true);
	if (min < 0 || min >= 60 || !time_literal(&s, ':'))
		return false;
	int sec = time_getnum(&s, true);
	if (sec < 0 || sec >= 60)
		return false;
	/* a fractional second that the layout does not have */
	if (s.n >= 2 && (s.p[0] == '.' || s.p[0] == ',') && time_isDigit(s, 1)) {
		size_t n = 2;
		while (n < s.n && time_isDigit(s, n))
			n++;
		s.p += n, s.n -= n;
	}
	/* stdISO8601ColonTZ */
	if (s.n >= 1 && s.p[0] == 'Z') {
		s.p++, s.n--;
	} else {
		if (s.n < 6 || s.p[3] != ':')
			return false;
		Str h = {s.p + 1, 2}, m = {s.p + 4, 2};
		char sign = s.p[0];
		int hr = time_getnum(&h, true), mm = hr >= 0 ? time_getnum(&m, true) : -1;
		if (hr < 0 || mm < 0 || hr > 24 || mm > 60 || (sign != '+' && sign != '-'))
			return false;
		s.p += 6, s.n -= 6;
	}
	if (s.n)
		return false; /* extra text */
	return day >= 1 && day <= time_daysIn(month, year);
}
