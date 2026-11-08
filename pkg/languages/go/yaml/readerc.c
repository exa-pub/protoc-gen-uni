/* Copyright (c) 2011-2019 Canonical Ltd
 * Copyright (c) 2006-2010 Kirill Simonov
 * Licensed under the Apache License, Version 2.0 and the MIT License (see LICENSE of
 * go.yaml.in/yaml/v3). */

/* Port of go.yaml.in/yaml/v3 v3.0.4 readerc.go (with the string read handler of apic.go): the
 * input is read in raw chunks of 512 bytes and decoded (UTF-8 or UTF-16 by BOM) into the working
 * buffer, which grows here instead of having a fixed capacity. */
#include "internal.h"

/* Set the reader error and return 0. */
static bool set_reader_error(yaml_parser_t *parser, const char *problem, int offset, int value)
{
	parser->error = yaml_READER_ERROR;
	parser->problem = problem;
	parser->problem_offset = offset;
	parser->problem_value = value;
	return false;
}

/* Update the raw buffer (yaml_string_read_handler reads the input). */
static bool update_raw_buffer(yaml_parser_t *parser)
{
	/* Return if the raw buffer is full. */
	if (parser->raw_pos == 0 && parser->raw_len == YAML_INPUT_RAW_BUFFER_SIZE)
		return true;
	/* Return on EOF. */
	if (parser->eof)
		return true;
	/* Move the remaining bytes in the raw buffer to the beginning. */
	if (parser->raw_pos > 0 && parser->raw_pos < parser->raw_len)
		memmove(parser->raw_buffer, parser->raw_buffer + parser->raw_pos, parser->raw_len - parser->raw_pos);
	parser->raw_len -= parser->raw_pos;
	parser->raw_pos = 0;
	/* Call the read handler to fill the buffer. */
	if (parser->input_pos == parser->input.n) {
		parser->eof = true; /* io.EOF */
		return true;
	}
	size_t room = YAML_INPUT_RAW_BUFFER_SIZE - parser->raw_len, n = parser->input.n - parser->input_pos;
	if (n > room)
		n = room;
	memcpy(parser->raw_buffer + parser->raw_len, parser->input.p + parser->input_pos, n);
	parser->input_pos += n;
	parser->raw_len += n;
	return true;
}

/* Determine the input stream encoding by checking the BOM symbol. If no BOM is found, the UTF-8
 * encoding is assumed. Return 1 on success, 0 on failure. */
static bool determine_encoding(yaml_parser_t *parser)
{
	/* Ensure that we had enough bytes in the raw buffer. */
	while (!parser->eof && parser->raw_len - parser->raw_pos < 3)
		if (!update_raw_buffer(parser))
			return false;
	const uint8_t *buf = parser->raw_buffer;
	size_t pos = parser->raw_pos, avail = parser->raw_len - pos;
	if (avail >= 2 && buf[pos] == 0xff && buf[pos + 1] == 0xfe) {
		parser->encoding = yaml_UTF16LE_ENCODING;
		parser->raw_pos += 2;
		parser->offset += 2;
	} else if (avail >= 2 && buf[pos] == 0xfe && buf[pos + 1] == 0xff) {
		parser->encoding = yaml_UTF16BE_ENCODING;
		parser->raw_pos += 2;
		parser->offset += 2;
	} else if (avail >= 3 && buf[pos] == 0xef && buf[pos + 1] == 0xbb && buf[pos + 2] == 0xbf) {
		parser->encoding = yaml_UTF8_ENCODING;
		parser->raw_pos += 3;
		parser->offset += 3;
	} else {
		parser->encoding = yaml_UTF8_ENCODING;
	}
	return true;
}

static void buffer_reserve(yaml_parser_t *parser, size_t need)
{
	if (need <= parser->buffer_cap)
		return;
	size_t cap = parser->buffer_cap ? parser->buffer_cap : 1536;
	while (cap < need)
		cap *= 2;
	parser->buffer = xrealloc(parser->buffer, parser->buffer_cap, cap);
	parser->buffer_cap = cap;
}

/* Ensure that the buffer contains at least `length` characters. Return true on success, false on
 * failure. The length is supposed to be significantly less that the buffer size. */
bool yaml_parser_update_buffer(yaml_parser_t *parser, int length)
{
	/* If the buffer has enough characters, return. */
	if (parser->unread >= length)
		return true;

	/* Determine the input encoding if it is not known yet. */
	if (parser->encoding == yaml_ANY_ENCODING) {
		if (!determine_encoding(parser))
			return false;
	}

	/* Move the unread characters to the beginning of the buffer. */
	size_t buffer_len = parser->buffer_len;
	if (parser->buffer_pos > 0 && parser->buffer_pos < buffer_len) {
		memmove(parser->buffer, parser->buffer + parser->buffer_pos, buffer_len - parser->buffer_pos);
		buffer_len -= parser->buffer_pos;
		parser->buffer_pos = 0;
	} else if (parser->buffer_pos == buffer_len) {
		buffer_len = 0;
		parser->buffer_pos = 0;
	}

	/* Fill the buffer until it has enough characters. */
	bool first = true;
	while (parser->unread < length) {
		/* Fill the raw buffer if necessary. */
		if (!first || parser->raw_pos == parser->raw_len) {
			if (!update_raw_buffer(parser)) {
				parser->buffer_len = buffer_len;
				return false;
			}
		}
		first = false;

		/* Decode the raw buffer. */
		while (parser->raw_pos != parser->raw_len) {
			uint32_t value = 0;
			int width = 0;
			size_t raw_unread = parser->raw_len - parser->raw_pos;
			const uint8_t *raw = parser->raw_buffer + parser->raw_pos;

			/* Decode the next character. */
			bool incomplete = false;
			if (parser->encoding == yaml_UTF8_ENCODING) {
				/* Decode a UTF-8 character. Check RFC 3629
				 * (http://www.ietf.org/rfc/rfc3629.txt) for more details. */
				uint8_t octet = raw[0];
				if ((octet & 0x80) == 0x00)
					width = 1;
				else if ((octet & 0xE0) == 0xC0)
					width = 2;
				else if ((octet & 0xF0) == 0xE0)
					width = 3;
				else if ((octet & 0xF8) == 0xF0)
					width = 4;
				else
					return (parser->buffer_len = buffer_len, set_reader_error)(parser, "invalid leading UTF-8 octet", parser->offset, octet);

				/* Check if the raw buffer contains an incomplete character. */
				if ((size_t)width > raw_unread) {
					if (parser->eof)
						return (parser->buffer_len = buffer_len, set_reader_error)(parser, "incomplete UTF-8 octet sequence", parser->offset, -1);
					incomplete = true;
				} else {
					/* Decode the leading octet. */
					if ((octet & 0x80) == 0x00)
						value = octet & 0x7F;
					else if ((octet & 0xE0) == 0xC0)
						value = octet & 0x1F;
					else if ((octet & 0xF0) == 0xE0)
						value = octet & 0x0F;
					else
						value = octet & 0x07;
					/* Check and decode the trailing octets. */
					for (int k = 1; k < width; k++) {
						octet = raw[k];
						/* Check if the octet is valid. */
						if ((octet & 0xC0) != 0x80)
							return (parser->buffer_len = buffer_len, set_reader_error)(parser, "invalid trailing UTF-8 octet", parser->offset + k, octet);
						/* Decode the octet. */
						value = (value << 6) + (octet & 0x3F);
					}
					/* Check the length of the sequence against the value. */
					if (!(width == 1 || (width == 2 && value >= 0x80) || (width == 3 && value >= 0x800) ||
					      (width == 4 && value >= 0x10000)))
						return (parser->buffer_len = buffer_len, set_reader_error)(parser, "invalid length of a UTF-8 sequence", parser->offset, -1);
					/* Check the range of the value. */
					if ((value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF)
						return (parser->buffer_len = buffer_len, set_reader_error)(parser, "invalid Unicode character", parser->offset, (int)value);
				}
			} else {
				int low, high;
				if (parser->encoding == yaml_UTF16LE_ENCODING)
					low = 0, high = 1;
				else
					low = 1, high = 0;
				/* Check for incomplete UTF-16 character. */
				if (raw_unread < 2) {
					if (parser->eof)
						return (parser->buffer_len = buffer_len, set_reader_error)(parser, "incomplete UTF-16 character", parser->offset, -1);
					incomplete = true;
				} else {
					/* Get the character. */
					value = (uint32_t)raw[low] + ((uint32_t)raw[high] << 8);
					/* Check for unexpected low surrogate area. */
					if ((value & 0xFC00) == 0xDC00)
						return (parser->buffer_len = buffer_len, set_reader_error)(parser, "unexpected low surrogate area", parser->offset, (int)value);
					/* Check for a high surrogate area. */
					if ((value & 0xFC00) == 0xD800) {
						width = 4;
						/* Check for incomplete surrogate pair. */
						if (raw_unread < 4) {
							if (parser->eof)
								return (parser->buffer_len = buffer_len, set_reader_error)(parser, "incomplete UTF-16 surrogate pair", parser->offset, -1);
							incomplete = true;
						} else {
							/* Get the next character. */
							uint32_t value2 = (uint32_t)raw[low + 2] + ((uint32_t)raw[high + 2] << 8);
							/* Check for a low surrogate area. */
							if ((value2 & 0xFC00) != 0xDC00)
								return (parser->buffer_len = buffer_len, set_reader_error)(parser, "expected low surrogate area", parser->offset + 2, (int)value2);
							/* Generate the value of the surrogate pair. */
							value = 0x10000 + ((value & 0x3FF) << 10) + (value2 & 0x3FF);
						}
					} else {
						width = 2;
					}
				}
			}
			if (incomplete)
				break;

			/* Check if the character is in the allowed range:
			 *      #x9 | #xA | #xD | [#x20-#x7E]               (8 bit)
			 *      | #x85 | [#xA0-#xD7FF] | [#xE000-#xFFFD]    (16 bit)
			 *      | [#x10000-#x10FFFF]                        (32 bit) */
			if (!(value == 0x09 || value == 0x0A || value == 0x0D || (value >= 0x20 && value <= 0x7E) || value == 0x85 ||
			      (value >= 0xA0 && value <= 0xD7FF) || (value >= 0xE000 && value <= 0xFFFD) ||
			      (value >= 0x10000 && value <= 0x10FFFF)))
				return (parser->buffer_len = buffer_len, set_reader_error)(parser, "control characters are not allowed", parser->offset, (int)value);

			/* Move the raw pointers. */
			parser->raw_pos += (size_t)width;
			parser->offset += width;

			/* Finally put the character into the buffer. */
			buffer_reserve(parser, buffer_len + 4);
			uint8_t *b = parser->buffer + buffer_len;
			if (value <= 0x7F) {
				b[0] = (uint8_t)value;
				buffer_len += 1;
			} else if (value <= 0x7FF) {
				b[0] = (uint8_t)(0xC0 + (value >> 6));
				b[1] = (uint8_t)(0x80 + (value & 0x3F));
				buffer_len += 2;
			} else if (value <= 0xFFFF) {
				b[0] = (uint8_t)(0xE0 + (value >> 12));
				b[1] = (uint8_t)(0x80 + ((value >> 6) & 0x3F));
				b[2] = (uint8_t)(0x80 + (value & 0x3F));
				buffer_len += 3;
			} else {
				b[0] = (uint8_t)(0xF0 + (value >> 18));
				b[1] = (uint8_t)(0x80 + ((value >> 12) & 0x3F));
				b[2] = (uint8_t)(0x80 + ((value >> 6) & 0x3F));
				b[3] = (uint8_t)(0x80 + (value & 0x3F));
				buffer_len += 4;
			}
			parser->unread++;
		}

		/* On EOF, put NUL into the buffer and return. */
		if (parser->eof) {
			buffer_reserve(parser, buffer_len + 1);
			parser->buffer[buffer_len] = 0;
			buffer_len++;
			parser->unread++;
			break;
		}
	}
	/* [Go] Read the documentation of this function above. To return true, we need to have the
	 * given length in the buffer. Not doing that means every single check that calls this
	 * function to make sure the buffer has a given length is Go) panicking; or C) accessing
	 * invalid memory. This happens here due to the EOF above breaking early. */
	buffer_reserve(parser, (size_t)length > buffer_len ? (size_t)length : buffer_len);
	while (buffer_len < (size_t)length) {
		parser->buffer[buffer_len] = 0;
		buffer_len++;
	}
	parser->buffer_len = buffer_len;
	return true;
}
