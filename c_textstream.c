#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <locale.h>


/*
 * Various conditions hold for the output written into a 
 * ISO C library _text streams_ if reading the stored text
 * back in again is to *guarantee* reproducing the text:
 *
 * Data consist only of printing characters and the control characters
 * HT and LF (named EOF here). 
 *
 *     text stream = line , { line } ;
 *
 *     line = [ { data char } , non-space char ] , EOL ;
 *
 *     data char = printing char | HT ; (* No CR or LF! *)
 *
 *     non-space char = printing char - space char ;
 *
 *     printing char = graphic char | SP ;
 *
 *     space char = SP | HT ; (* Other "space" control chars omitted *)
 *
 * Line lengths of at least 254 characters (including EOL) are supported.
 * Buffer size BUFSIZ is at least 256.
 *
 *
 * The line format used here is:
 *
 *   - A two-digit hexadecimal length prefix,
 *   - followed by the given number of *data char*,
 *   - then followed by either "<" or "/",
 *   - followed by EOL.
 *
 * The "<" indicates that the packaged text has an EOL here, while
 * the line breaks after "/" are there just to keep the line length
 * under the supported limit - the packaged text continues in the
 * next line.
 */
/* C99 7.19.2: MUST support lines with at least 254 characters */

#define LINSIZ 254-4 /* 2 hex digits, the trailing '<'or '/', and EOL */

#define NUL    '\0'
#define EOL    '\n'
#define SP     ' '

char lbuf[LINSIZ];  
unsigned linsiz = LINSIZ;
unsigned llen = 0;

int append(int ch)
{
    int sp;
    unsigned c = ch;
    int nout = 0;

    sp = isspace(c) != 0;
    if (ch != EOL && llen + 1 + sp < linsiz)
	lbuf[llen++] = ch;
    else {
	int olen;
	if (ch != EOL) {
	    lbuf[llen++] = ch;
	}
	olen = llen;

	nout = printf("%02x%.*s%c\n", llen, (int)olen, lbuf,
		                               (ch == EOL ? '<' : '/'));

	llen = 0;
    }
    return nout;
}

int main(int argc, char *argv[])
{
    int ch;
    int k, nout = 0;
    int i = -1;

    if (argc > 1) {
	if ((i = atoi(argv[1])) && 0 < i && i <= LINSIZ) {
	    linsiz = i;
	    printf("L = %d\n", linsiz);
	}
    }

    do
	k = append(ch = getchar());
    while (k >= 0 && ch != EOF);

    return 0;
}
