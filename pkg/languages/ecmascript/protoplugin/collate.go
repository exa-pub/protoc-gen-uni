package protoplugin

// LocaleCompare compares two strings like String.prototype.localeCompare in Node.js with the
// default (root) ICU collation, for the characters of ECMAScript identifiers generated from
// Protobuf names: ASCII letters, digits, '_' and '$'. Upstream sorts imported names with it.
//
// The root collation compares in levels: first the base characters ignoring case (punctuation
// '_' sorts before the currency symbol '$', which sorts before digits, which sort before
// letters), then the case (lowercase before uppercase). Other characters are compared by code
// point after letters, which is not exact but does not occur in generated identifiers.
func LocaleCompare(a, b string) int {
	if c := compareKeys(a, b, primaryWeight); c != 0 {
		return c
	}
	if c := compareKeys(a, b, tertiaryWeight); c != 0 {
		return c
	}
	switch {
	case a < b:
		return -1
	case a > b:
		return 1
	}
	return 0
}

func compareKeys(a, b string, weight func(rune) int) int {
	ra, rb := []rune(a), []rune(b)
	for i := 0; i < len(ra) && i < len(rb); i++ {
		wa, wb := weight(ra[i]), weight(rb[i])
		if wa != wb {
			if wa < wb {
				return -1
			}
			return 1
		}
	}
	switch {
	case len(ra) < len(rb):
		return -1
	case len(ra) > len(rb):
		return 1
	}
	return 0
}

func primaryWeight(r rune) int {
	switch {
	case r == '_':
		return 1
	case r == '$':
		return 2
	case r >= '0' && r <= '9':
		return 10 + int(r-'0')
	case r >= 'a' && r <= 'z':
		return 100 + int(r-'a')
	case r >= 'A' && r <= 'Z':
		return 100 + int(r-'A')
	}
	return 1000 + int(r)
}

func tertiaryWeight(r rune) int {
	if r >= 'A' && r <= 'Z' {
		return 1
	}
	return 0
}
