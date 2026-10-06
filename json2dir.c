/*
 * json2dir - create the directory tree a JSON document describes,
 * in the current directory (RFC J2D-1).
 *
 * Written in K&R C, as in "The C Programming Language", 1st edition
 * (1978): old-style function definitions, no prototypes, no void,
 * no const, no enum, no unsigned char.  The only headers are the
 * two that K&R itself uses for stat() in chapter 8.  Everything
 * else is declared by hand; the open() flags are the Linux values.
 */

#include <sys/types.h>
#include <sys/stat.h>

#define O_RDONLY	0
#define O_WRONLY	01
#define O_CREAT		0100
#define O_EXCL		0200
#define O_NOFOLLOW	0400000

#define MAXDEPTH	1000

#define T_STR		1
#define T_LINK		2
#define T_SCRIPT	3
#define T_OBJ		4

int read();
int write();
int open();
int close();
int unlink();
int symlink();
char *malloc();
char *realloc();
int exit();

struct node {
	int type;
	char *s;		/* string value, NUL-terminated */
	long len;		/* its length in bytes */
	char *name;		/* member name, NUL-terminated */
	long nlen;
	struct node *kids;	/* members of an object */
	struct node *next;	/* next member of the parent */
};

char *buf;		/* the whole input */
long blen;		/* its length */
long pos;		/* parse position */
char *sval;		/* last string parsed */
long slen;

long
length(s)
char *s;
{
	long n;

	n = 0;
	while (s[n] != '\0')
		n++;
	return n;
}

puts2(s)
char *s;
{
	write(2, s, length(s));
}

die(msg)
char *msg;
{
	puts2("json2dir: ");
	puts2(msg);
	puts2("\n");
	exit(1);
}

die2(msg, path)
char *msg, *path;
{
	puts2("json2dir: ");
	puts2(path);
	puts2(": ");
	puts2(msg);
	puts2("\n");
	exit(1);
}

char *
xalloc(n)
long n;
{
	char *p;

	if ((p = malloc(n)) == 0)
		die("out of memory");
	return p;
}

readall()
{
	long cap, n;

	cap = 65536;
	buf = xalloc(cap);
	blen = 0;
	for (;;) {
		if (blen == cap) {
			cap = cap * 2;
			if ((buf = realloc(buf, cap)) == 0)
				die("out of memory");
		}
		n = read(0, buf + blen, cap - blen);
		if (n < 0)
			die("cannot read standard input");
		if (n == 0)
			break;
		blen = blen + n;
	}
}

/* strict UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF */
utf8ok()
{
	long i, cp;
	int c, d, n, k;

	i = 0;
	while (i < blen) {
		c = buf[i] & 0377;
		if (c < 0200) {
			i++;
			continue;
		}
		if (c >= 0302 && c <= 0337) {
			n = 1;
			cp = c & 037;
		} else if (c >= 0340 && c <= 0357) {
			n = 2;
			cp = c & 017;
		} else if (c >= 0360 && c <= 0364) {
			n = 3;
			cp = c & 07;
		} else
			return 0;
		if (i + n >= blen)
			return 0;
		for (k = 1; k <= n; k++) {
			d = buf[i + k] & 0377;
			if ((d & 0300) != 0200)
				return 0;
			cp = (cp << 6) | (d & 077);
		}
		if (n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)))
			return 0;
		if (n == 3 && (cp < 0x10000 || cp > 0x10FFFF))
			return 0;
		i = i + n + 1;
	}
	return 1;
}

skipws()
{
	while (pos < blen && (buf[pos] == ' ' || buf[pos] == '\t' ||
	    buf[pos] == '\n' || buf[pos] == '\r'))
		pos++;
}

expect(c)
int c;
{
	skipws();
	if (pos >= blen || buf[pos] != c)
		die("input is not valid JSON");
	pos++;
	skipws();
}

long
hex4()
{
	long v;
	int i, c;

	v = 0;
	for (i = 0; i < 4; i++) {
		if (pos >= blen)
			die("input is not valid JSON: bad \\u escape");
		c = buf[pos++];
		if (c >= '0' && c <= '9')
			c = c - '0';
		else if (c >= 'a' && c <= 'f')
			c = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			c = c - 'A' + 10;
		else
			die("input is not valid JSON: bad \\u escape");
		v = v * 16 + c;
	}
	return v;
}

/* parse a string at buf[pos] (a '"') into sval/slen */
pstring()
{
	long end, n, cp, lo;
	int c;
	char *out;

	end = pos + 1;
	while (end < blen && buf[end] != '"') {
		if (buf[end] == '\\')
			end++;
		end++;
	}
	if (end >= blen)
		die("input is not valid JSON: unterminated string");
	out = xalloc(end - pos + 1);
	n = 0;
	pos++;
	while (buf[pos] != '"') {
		c = buf[pos] & 0377;
		if (c < 040)
			die("input is not valid JSON: control character in string");
		if (c != '\\') {
			out[n++] = buf[pos++];
			continue;
		}
		pos++;
		c = buf[pos++];
		if (c == '"' || c == '\\' || c == '/')
			out[n++] = c;
		else if (c == 'b')
			out[n++] = '\b';
		else if (c == 'f')
			out[n++] = '\f';
		else if (c == 'n')
			out[n++] = '\n';
		else if (c == 'r')
			out[n++] = '\r';
		else if (c == 't')
			out[n++] = '\t';
		else if (c == 'u') {
			cp = hex4();
			if (cp >= 0xDC00 && cp <= 0xDFFF)
				die("string contains an unpaired surrogate");
			if (cp >= 0xD800 && cp <= 0xDBFF) {
				if (pos + 1 >= blen || buf[pos] != '\\' ||
				    buf[pos + 1] != 'u')
					die("string contains an unpaired surrogate");
				pos = pos + 2;
				lo = hex4();
				if (lo < 0xDC00 || lo > 0xDFFF)
					die("string contains an unpaired surrogate");
				cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
			}
			if (cp < 0x80)
				out[n++] = cp;
			else if (cp < 0x800) {
				out[n++] = 0300 | (cp >> 6);
				out[n++] = 0200 | (cp & 077);
			} else if (cp < 0x10000) {
				out[n++] = 0340 | (cp >> 12);
				out[n++] = 0200 | ((cp >> 6) & 077);
				out[n++] = 0200 | (cp & 077);
			} else {
				out[n++] = 0360 | (cp >> 18);
				out[n++] = 0200 | ((cp >> 12) & 077);
				out[n++] = 0200 | ((cp >> 6) & 077);
				out[n++] = 0200 | (cp & 077);
			}
		} else
			die("input is not valid JSON: bad escape");
	}
	pos++;
	out[n] = '\0';
	sval = out;
	slen = n;
}

/* is there a NUL byte among the first n bytes of s? */
hasnul(s, n)
char *s;
long n;
{
	long i;

	for (i = 0; i < n; i++)
		if (s[i] == '\0')
			return 1;
	return 0;
}

checkname(s, n)
char *s;
long n;
{
	long i;

	if (n == 0)
		die("invalid name: empty");
	if (hasnul(s, n))
		die("invalid name: contains NUL");
	if ((n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.'))
		die2("invalid name", s);
	for (i = 0; i < n; i++)
		if (s[i] == '/')
			die2("invalid name", s);
}

same(a, b, n)
char *a, *b;
long n;
{
	long i;

	for (i = 0; i < n; i++)
		if (a[i] != b[i])
			return 0;
	return 1;
}

struct node *
newnode(type)
int type;
{
	struct node *p;

	p = (struct node *) xalloc((long) sizeof(struct node));
	p->type = type;
	p->s = 0;
	p->len = 0;
	p->name = 0;
	p->nlen = 0;
	p->kids = 0;
	p->next = 0;
	return p;
}

struct node *pobject();

/* ["link", target] or ["script", content] */
struct node *
parray()
{
	struct node *p;
	char *kind;
	long klen;

	pos++;
	skipws();
	if (pos >= blen || buf[pos] != '"')
		die("an array must be [\"link\", target] or [\"script\", content]");
	pstring();
	kind = sval;
	klen = slen;
	expect(',');
	if (pos >= blen || buf[pos] != '"')
		die("an array must be [\"link\", target] or [\"script\", content]");
	pstring();
	skipws();
	if (pos >= blen || buf[pos] != ']')
		die("an array must be [\"link\", target] or [\"script\", content]");
	pos++;
	if (klen == 4 && same(kind, "link", 4L)) {
		if (slen == 0 || hasnul(sval, slen))
			die("a link target cannot be empty or contain NUL");
		p = newnode(T_LINK);
	} else if (klen == 6 && same(kind, "script", 6L))
		p = newnode(T_SCRIPT);
	else
		die("unknown array kind");
	p->s = sval;
	p->len = slen;
	return p;
}

struct node *
pvalue(depth)
int depth;
{
	struct node *p;

	if (pos >= blen)
		die("input is not valid JSON");
	if (buf[pos] == '{')
		return pobject(depth + 1);
	if (buf[pos] == '[')
		return parray();
	if (buf[pos] == '"') {
		pstring();
		p = newnode(T_STR);
		p->s = sval;
		p->len = slen;
		return p;
	}
	die("only objects, strings and arrays are allowed");
	return 0;
}

struct node *
pobject(depth)
int depth;
{
	struct node *o, *k, *q, *last;
	char *name;
	long nlen;

	if (depth > MAXDEPTH)
		die("document is nested too deeply");
	o = newnode(T_OBJ);
	last = 0;
	pos++;
	skipws();
	if (pos < blen && buf[pos] == '}') {
		pos++;
		return o;
	}
	for (;;) {
		skipws();
		if (pos >= blen || buf[pos] != '"')
			die("input is not valid JSON: expected a member name");
		pstring();
		name = sval;
		nlen = slen;
		checkname(name, nlen);
		for (q = o->kids; q != 0; q = q->next)
			if (q->nlen == nlen && same(q->name, name, nlen))
				die2("duplicate member name", name);
		expect(':');
		k = pvalue(depth);
		k->name = name;
		k->nlen = nlen;
		if (last == 0)
			o->kids = k;
		else
			last->next = k;
		last = k;
		skipws();
		if (pos < blen && buf[pos] == ',') {
			pos++;
			continue;
		}
		if (pos < blen && buf[pos] == '}') {
			pos++;
			return o;
		}
		die("input is not valid JSON: expected , or }");
	}
}

char *
join(dir, name)
char *dir, *name;
{
	long a, b, i;
	char *p;

	a = length(dir);
	b = length(name);
	p = xalloc(a + b + 2);
	for (i = 0; i < a; i++)
		p[i] = dir[i];
	p[a] = '/';
	for (i = 0; i <= b; i++)
		p[a + 1 + i] = name[i];
	return p;
}

writefile(path, n)
char *path;
struct node *n;
{
	int fd;
	long off, w;
	struct stat st;

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0666);
	if (fd < 0)
		die2("cannot create file", path);
	off = 0;
	while (off < n->len) {
		w = write(fd, n->s + off, n->len - off);
		if (w <= 0)
			die2("cannot write file", path);
		off = off + w;
	}
	if (n->type == T_SCRIPT) {
		if (fstat(fd, &st) < 0 || fchmod(fd, (st.st_mode & 07777) | 0111) < 0)
			die2("cannot make file executable", path);
	}
	if (close(fd) < 0)
		die2("cannot write file", path);
}

apply(dir, o)
char *dir;
struct node *o;
{
	struct node *k;
	struct stat st;
	char *path;
	int exists, isdir;

	for (k = o->kids; k != 0; k = k->next) {
		path = join(dir, k->name);
		exists = lstat(path, &st) == 0;
		isdir = exists && (st.st_mode & S_IFMT) == S_IFDIR;
		if (k->type == T_OBJ) {
			if (!isdir) {
				if (exists && unlink(path) < 0)
					die2("cannot remove", path);
				if (mkdir(path, 0777) < 0)
					die2("cannot create directory", path);
			}
			apply(path, k);
		} else {
			if (isdir)
				die2("a directory is in the way", path);
			if (exists && unlink(path) < 0)
				die2("cannot remove", path);
			if (k->type == T_LINK) {
				if (symlink(k->s, path) < 0)
					die2("cannot create symlink", path);
			} else
				writefile(path, k);
		}
	}
}

main(argc, argv)
int argc;
char **argv;
{
	struct node *root;

	if (argc > 1) {
		puts2("usage: json2dir < document.json\n");
		exit(2);
	}
	readall();
	if (!utf8ok())
		die("input is not valid UTF-8");
	pos = 0;
	if (blen >= 3 && (buf[0] & 0377) == 0357 && (buf[1] & 0377) == 0273 &&
	    (buf[2] & 0377) == 0277)
		pos = 3;
	skipws();
	if (pos >= blen || buf[pos] != '{')
		die("the root of the document must be an object");
	root = pobject(0);
	skipws();
	if (pos != blen)
		die("input is not valid JSON: trailing data");
	apply(".", root);
	exit(0);
}
