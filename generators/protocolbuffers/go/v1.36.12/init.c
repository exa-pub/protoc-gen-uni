/* Copyright 2019 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Ported from cmd/protoc-gen-go/internal_gengo/init.go and init_opaque.go (protobuf-go v1.36.12). */

typedef struct {
	PgEnum *e; /* embedded *protogen.Enum */
	bool genJSONMethod, genRawDescMethod;
	int index; /* in allEnums */
} EnumInfo;

typedef struct {
	PgMessage *m; /* embedded *protogen.Message */
	bool genRawDescMethod, genExtRangeMethod, isTracked, noInterface;
	int index;    /* in allMessages */
	int sfCount;  /* structFields.count */
} MessageInfo;

typedef struct {
	PgFile *f; /* embedded *protogen.File */
	EnumInfo **allEnums;
	int nAllEnums;
	MessageInfo **allMessages;
	int nAllMessages;
	PgExtension **allExtensions;
	int nAllExtensions;
	/* needRawDesc specifies whether the generator should emit logic to provide the legacy raw
	 * descriptor in GZIP'd form. */
	bool needRawDesc;
} FileInfo;

static bool is_open(const MessageInfo *m) { return m->m->APILevel == PG_API_OPEN; }
static bool is_hybrid(const MessageInfo *m) { return m->m->APILevel == PG_API_HYBRID; }
static bool is_opaque(const MessageInfo *m) { return m->m->APILevel == PG_API_OPAQUE; }

static void sf_append(MessageInfo *m) { m->sfCount++; }

/* isTrackedMessage reports whether field tracking is enabled on the message: the option is
 * decoded from unknown fields to avoid a dependency on the annotation proto. */
static COLD bool is_tracked_message(const MessageInfo *m)
{
	enum { trackFieldUse_fieldNumber = 37383685 };
	bool tracked = false;
	PbReader r = pb_unknown_fields(m->m->Desc->d.options);
	PbRawField f;
	while (pb_next_field(&r, &f))
		if (f.num == trackFieldUse_fieldNumber && f.wt == WT_VARINT)
			tracked = f.v != 0;
	return tracked;
}

static COLD void add_enum_infos(FileInfo *f, PgEnum **enums, int n)
{
	for (int i = 0; i < n; i++) {
		EnumInfo *e = xalloc(sizeof *e);
		e->e = enums[i];
		e->genJSONMethod = true;
		e->genRawDescMethod = true;
		/* opaqueNewEnumInfoHook */
		if (f->f->APILevel != PG_API_OPEN) {
			e->genJSONMethod = false;
			e->genRawDescMethod = false;
		}
		e->index = f->nAllEnums;
		f->allEnums = xrealloc(f->allEnums, sizeof *f->allEnums * (size_t)f->nAllEnums, sizeof *f->allEnums * (size_t)(f->nAllEnums + 1));
		f->allEnums[f->nAllEnums++] = e;
	}
}

static COLD void add_message_infos(FileInfo *f, PgMessage **messages, int n)
{
	for (int i = 0; i < n; i++) {
		MessageInfo *m = xalloc(sizeof *m);
		m->m = messages[i];
		m->genRawDescMethod = true;
		m->genExtRangeMethod = true;
		m->isTracked = is_tracked_message(m);
		/* opaqueNewMessageInfoHook */
		if (!is_open(m)) {
			m->genRawDescMethod = false;
			m->genExtRangeMethod = false;
		}
		m->index = f->nAllMessages;
		f->allMessages = xrealloc(f->allMessages, sizeof *f->allMessages * (size_t)f->nAllMessages,
					  sizeof *f->allMessages * (size_t)(f->nAllMessages + 1));
		f->allMessages[f->nAllMessages++] = m;
	}
}

static COLD void add_extension_infos(FileInfo *f, PgExtension **xs, int n)
{
	for (int i = 0; i < n; i++) {
		f->allExtensions = xrealloc(f->allExtensions, sizeof *f->allExtensions * (size_t)f->nAllExtensions,
					    sizeof *f->allExtensions * (size_t)(f->nAllExtensions + 1));
		f->allExtensions[f->nAllExtensions++] = xs[i];
	}
}

static COLD void walk_messages(FileInfo *f, PgMessage **messages, int n)
{
	for (int i = 0; i < n; i++) {
		PgMessage *m = messages[i];
		add_enum_infos(f, m->Enums, m->nEnums);
		add_message_infos(f, m->Messages, m->nMessages);
		add_extension_infos(f, m->Extensions, m->nExtensions);
		walk_messages(f, m->Messages, m->nMessages);
	}
}

static COLD FileInfo *new_file_info(PgFile *file)
{
	FileInfo *f = xalloc(sizeof *f);
	f->f = file;
	/* Collect all enums, messages, and extensions in "flattened ordering". See
	 * filetype.TypeBuilder. */
	add_enum_infos(f, file->Enums, file->nEnums);
	add_message_infos(f, file->Messages, file->nMessages);
	add_extension_infos(f, file->Extensions, file->nExtensions);
	walk_messages(f, file->Messages, file->nMessages);
	return f;
}
