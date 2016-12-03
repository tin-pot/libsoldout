/* main.c - main function for markdown module testing */


#include "markdown.h"
#include "renderers.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define READ_UNIT 1024
#define OUTPUT_UNIT 64


/* buffer statistics, to track some memleaks */
#ifdef BUFFER_STATS
extern long buffer_stat_nb;
extern size_t buffer_stat_alloc_bytes;
#endif

#define BUFBOL(ob) ((ob)->size == 0 || (ob)->data[(ob)->size-1] == '\n')
#define BUFNEL(ob) do { if (!BUFBOL(ob)) bufputc(ob, '\n'); } while (0) 

/* ESIS representation in `nsgmls` output format */

static void attribn(struct buf *ob, const char *attr, const char *val,
                                                      size_t len)
{
    BUFNEL(ob);
    bufputc(ob, 'A');
    bufputs(ob, attr);
    BUFPUTSL(ob, " CDATA ");
    bufput(ob, val, len);
    BUFNEL(ob);
}

static void attrib(struct buf *ob, const char *attr, const char *val)
{
    BUFNEL(ob);
    bufputc(ob, 'A');
    bufputs(ob, attr);
    BUFPUTSL(ob, " CDATA ");
    bufputs(ob, val);
    BUFNEL(ob);
}

static void stag(struct buf *ob, const char *gi)
{
    BUFNEL(ob);
    bufputc(ob, '(');
    bufputs(ob, gi);
    BUFNEL(ob);
}

static void etag(struct buf *ob, const char *gi)
{
    BUFNEL(ob);
    bufputc(ob, ')');
    bufputs(ob, gi);
    BUFNEL(ob);
}

static void cdata(struct buf *ob, char *text, size_t len)
{
    size_t k;
    
    if (len == 0) return;
    
    if (BUFBOL(ob)) {
        bufputc(ob, '-');
    }
    for (k = 0; k < len; ++k) {
	unsigned ch = 0xFF & text[k];
	if (ch == '\\') {
	    bufputc(ob, '\\');
	    bufputc(ob, '\\');
	} else if (ch == '\n') {
	    bufputc(ob, '\\');
	    bufputc(ob, 'n');
	    bufputc(ob, '\\');
	    bufputc(ob, '0');
	    bufputc(ob, '1');
	    bufputc(ob, '2');
	} else if (ch < 32) {
	    char buf[4];

	    sprintf(buf, "%03o", ch);
	    bufputc(ob, '\\');
	    bufputc(ob, buf[0]);
	    bufputc(ob, buf[1]);
	    bufputc(ob, buf[2]);
	} else {
	    bufputc(ob, ch);
	}
    }
    /* bufputc(ob, '\n'); */
}

static void entref(struct buf *ob, char *text, size_t len)
{
    if (len == 0) return;

    BUFNEL(ob);
    bufputc(ob, '&');
    bufput(ob, text, len);
    BUFNEL(ob);
}

/* Renderers */

static void
esis_blockcode(struct buf *ob, struct buf *text, char *info, size_t infosz, void *opaque)
{
    stag(ob, "pre");
    if (infosz > 0) attribn(ob, "title", info, infosz);
    stag(ob, "code");
    cdata(ob, text->data, text->size);
    etag(ob, "code");
    etag(ob, "pre");
}


static void
esis_blockquote(struct buf *ob, struct buf *text, void *opaque)
{
    stag(ob, "blockquote");
    if (text) bufput(ob, text->data, text->size);
    etag(ob, "blockquote");
}


static void
esis_raw_block(struct buf *ob, struct buf *text, void *opaque)
{
    attrib(ob, "mode", "vert");
    attrib(ob, "notation", "SGML");
    stag(ob, "mark-up");
    cdata(ob, text->data, text->size);
    etag(ob, "mark-up");
    
    return;
}


static void
esis_header(struct buf *ob, struct buf *text, int level, void *opaque)
{
    char tag[8];
    sprintf(tag, "h%d", level);
    stag(ob, tag);
    if (text) bufput(ob, text->data, text->size);
    etag(ob, tag);
}


static void
esis_hrule(struct buf *ob, void *opaque)
{
    const char *tag = "hr";
    stag(ob, "hr");
    etag(ob, "hr");
}


static void
esis_list(struct buf *ob, struct buf *text, int flags, void *opaque)
{
    const char *tag = (flags & MKD_LIST_ORDERED) ? "ol" : "ul";
    stag(ob, tag);
    bufput(ob, text->data, text->size);
    etag(ob, tag);
}


static void
esis_listitem(struct buf *ob, struct buf *text, int flags, void *opaque)
{
    stag(ob, "li");
    
    if (text) {
	if (text->size >= 1 && text->data[text->size-1] == '\n')
	    text->size -= 1;
	
	if (text->size >= 6 &&
	    memcmp(text->data+text->size-6, "\\n\\012", 6) == 0)
	    text->size -= 6;
	    
        if (text->size >= 2 && 
	    memcmp(text->data+text->size-2, "\n-", 2) == 0)
	    text->size -= 2;
	    
	bufput(ob, text->data, text->size);
	bufputc(ob, '\n');
    }
    
    etag(ob, "li");
}


static void
esis_paragraph(struct buf *ob, struct buf *text, void *opaque)
{
    stag(ob, "p");
    if (text != NULL) bufput(ob, text->data, text->size);
    etag(ob, "p");
}


static int
esis_autolink(struct buf *ob, struct buf *link, enum mkd_autolink type,
						void *opaque)
{
    if (!link || !link->size)
	return 0;
	
    BUFNEL(ob);
    BUFPUTSL(ob, "Ahref CDATA ");
    if (type == MKDA_IMPLICIT_EMAIL)
	BUFPUTSL(ob, "mailto:");
	
    bufput(ob, link->data, link->size);
    bufputc(ob, '\n');
    
    stag(ob, "a");
    
    if (type == MKDA_EXPLICIT_EMAIL && link->size > 7)
	bufput(ob, link->data + 7, link->size - 7);
    else
	bufput(ob, link->data, link->size);
	
    etag(ob, "a");
    
    return 1;
}


static int
esis_codespan(struct buf *ob, struct buf *text, void *opaque)
{
    stag(ob, "code");
    cdata(ob, text->data, text->size);
    etag(ob, "code");
    return 1;
}


static int
esis_double_emphasis(struct buf *ob, struct buf *text, char c, void *opaque)
{
    if (text == NULL || text->size == 0)
	return 0;

    stag(ob, "strong");
    bufput(ob, text->data, text->size);
    etag(ob, "strong");
    return 1;
}


static int
esis_emphasis(struct buf *ob, struct buf *text, char c, void *opaque)
{
    if (text == NULL || text->size == 0)
	return 0;

    stag(ob, "em");
    bufput(ob, text->data, text->size);
    etag(ob, "em");
    return 1;
}


static int
esis_image(struct buf *ob, struct buf *link, struct buf *title,
			struct buf *alt, void *opaque)
{
    if (!link || !link->size) return 0;
    
    attribn(ob, "src",   link->data,   link->size);
    
    if (alt && alt->size)
	attribn(ob, "alt",   alt->data,    alt->size);
    
    if (alt && alt->size)
	attribn(ob, "title", title->data,  title->size);
	
    stag(ob, "img");
    etag(ob, "img");
    return 1;
}


static int
esis_linebreak(struct buf *ob, void *opaque)
{
    stag(ob, "br");
    etag(ob, "br");
    return 1;
}


static int
esis_link(struct buf *ob, struct buf *link, struct buf *title,
			struct buf *content, void *opaque)
{
    if (link && link->size)
	attribn(ob, "href", link->data, link->size);
    
    if (title && title->size)
	attribn(ob, "title", title->data, title->size);

    stag(ob, "a");
    
    if (content && content->size)
	bufput(ob, content->data, content->size);

    etag(ob, "a");
    
    return 1;
}


static int
esis_raw_inline(struct buf *ob, struct buf *text, void *opaque)
{
	/*
	 * This gets called for tags, markup and comment declarations, and 
	 * processing instructions.
	 */
    attrib(ob, "mode", "horiz");
    attrib(ob, "notation", "SGML");
    stag(ob, "mark-up");
    cdata(ob, text->data, text->size);
    etag(ob, "mark-up");
    return 1;
}


static int
esis_triple_emphasis(struct buf *ob, struct buf *text, char c, void *opaque)
{
    if (text == NULL || text->size == 0)
	return 0;

    stag(ob, "strong");
    stag(ob, "em");
    bufput(ob, text->data, text->size);
    etag(ob, "em");
    etag(ob, "strong");
    return 1;
}


static void
esis_normal_text(struct buf *ob, struct buf *text, void *opaque)
{
    cdata(ob, text->data, text->size);
}

static void
esis_entity(struct buf *ob, struct buf *entity, void *opaque)
{
    assert(entity->size > 0);
    entref(ob, entity->data, entity->size);
}


static const struct mkd_renderer mkd_esis = {
    NULL,
    NULL,

    esis_blockcode,
    esis_blockquote,
    esis_raw_block,
    esis_header,
    esis_hrule,
    esis_list,
    esis_listitem,
    esis_paragraph,
    NULL,
    NULL,
    NULL,

    esis_autolink,
    esis_codespan,
    esis_double_emphasis,
    esis_emphasis,
    esis_image,
    esis_linebreak,
    esis_link,
    esis_raw_inline,
    esis_triple_emphasis,

    esis_entity,
    esis_normal_text,

    64,
    "*_",
    NULL
};

/* usage • print the option list */
void
usage(FILE *out, const char *name) {
	fprintf(out, "Usage: %s [-H | -x] [-c | -d | -m | -n]"
	    " [input-file]\n\n",
	    name);
	fprintf(out, "\t-c, --commonmark\n"
	    "\t\tEnable CommonMark rendering\n"
	    "\t-d, --discount\n"
	    "\t\tEnable some Discount extensions (image size specficiation,\n"
	    "\t\tclass blocks and 'abbr:', 'class:', 'id:' and 'raw:'\n"
	    "\t\tpseudo-protocols)\n"
	    "\t-H, --html\n"
	    "\t\tOutput HTML-style self-closing tags (e.g. <br>)\n"
	    "\t-h, --help\n"
	    "\t\tDisplay this help text and exit without further processing\n"
	    "\t-m, --markdown\n"
	    "\t\tDisable all extensions and use strict markdown syntax\n"
	    "\t-n, --natext\n"
	    "\t\tEnable support Discount extensions and Natasha's own\n"
	    "\t\textensions (id header attribute, class paragraph attribute,\n"
	    "\t\t'ins' and 'del' elements, and plain span elements)\n"
	    "\t-x, --xhtml\n"
	    "\t\tOutput XHTML-style self-closing tags (e.g. <br />)\n"); }



/* main • main function, interfacing STDIO with the parser */
int
main(int argc, char **argv) {
	struct buf *ib, *ob;
	size_t ret;
	FILE *in = stdin;
	const struct mkd_renderer *erndr;
	int ch, argerr, help;
	struct option longopts[] = {
	    { "commonmark",	no_argument,	0,	'c' },
	    { "discount",	no_argument,	0,	'd' },
	    { "html",		no_argument,	0,	'H' },
	    { "help",		no_argument,	0,	'h' },
	    { "markdown",	no_argument,	0,	'm' },
	    { "natext",		no_argument,	0,	'n' },
	    { "xhtml",		no_argument,	0,	'x' },
	    { 0,		0,		0,	0 } };

	/* default options: strict markdown input, HTML output */
	erndr = &mkd_esis;

	/* argument parsing */
	argerr = help = 0;
	while (!argerr &&
	    (ch = getopt_long(argc, argv, "h", longopts, 0)) != -1)
		switch (ch) {
		    case 'h': /* display help */
			argerr = help = 1;
			break;
		    default:
			argerr = 1; }
	if (argerr) {
		usage(help ? stdout : stderr, argv[0]);
		return help ? EXIT_SUCCESS : EXIT_FAILURE; }
	argc -= optind;
	argv += optind;

	/* opening the file if given from the command line */
	if (argc > 0) {
		in = fopen(argv[0], "r");
		if (!in) {
			fprintf(stderr,"Unable to open input file \"%s\": %s\n",
				argv[0], strerror(errno));
			return 1; } }

	/* reading everything */
	ib = bufnew(READ_UNIT);
	bufgrow(ib, READ_UNIT);
	while ((ret = fread(ib->data + ib->size, 1,
			ib->asize - ib->size, in)) > 0) {
		ib->size += ret;
		bufgrow(ib, ib->size + READ_UNIT); }
	if (in != stdin) fclose(in);

	/* performing markdown parsing */
	ob = bufnew(OUTPUT_UNIT);
	markdown(ob, ib, erndr);

	/* writing the result to stdout */
	ret = fwrite(ob->data, 1, ob->size, stdout);
	if (ret < ob->size)
		fprintf(stderr, "Warning: only %zu output byte written, "
				"out of %zu\n",
				ret,
				ob->size);

	/* cleanup */
	bufrelease(ib);
	bufrelease(ob);

	/* memory checks */
#ifdef BUFFER_STATS
	if (buffer_stat_nb)
		fprintf(stderr, "Warning: %ld buffers still active\n",
				buffer_stat_nb);
	if (buffer_stat_alloc_bytes)
		fprintf(stderr, "Warning: %zu bytes still allocated\n",
				buffer_stat_alloc_bytes);
#endif
	return 0; }

/* vim: set filetype=c: */
