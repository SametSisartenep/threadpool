#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>

typedef struct Ttask Ttask;
typedef struct Tpool Tpool;

struct Ttask
{
	void (*fn)(void*);
	void *arg;
};

struct Tpool
{
	ulong nprocs;
	Ref issued;
	Ref complete;

	Channel *subq;	/* task submission queue */
	Channel *done;	/* task completion signal */
};

void
threadloop(void *arg)
{
	Tpool *pool;
	Ttask *task;

	pool = arg;

	while((task = recvp(pool->subq)) != nil){
		task->fn(task->arg);
		incref(&pool->complete);
		nbsend(pool->done, nil);
	}
}

Tpool *
mkthreadpool(ulong nprocs)
{
	Tpool *tp;

	tp = malloc(sizeof *tp);
	memset(tp, 0, sizeof *tp);
	tp->nprocs = nprocs;
	tp->subq = chancreate(sizeof(void*), nprocs);
	tp->done = chancreate(sizeof(void*), 0);
	while(nprocs--)
		proccreate(threadloop, tp, mainstacksize);
	return tp;
}

void
threadpoolexec(Tpool *tp, void (*fn)(void*), void *arg)
{
	Ttask *t;

	t = malloc(sizeof *t);
	t->fn = fn;
	t->arg = arg;

	sendp(tp->subq, t);
	incref(&tp->issued);
}

typedef struct Targs Targs;
struct Targs
{
	Memimage *i;
	int y;
};
void
fillpix(void *arg)
{
	Targs *imgop;
	Point p;
	ulong *fb, pix;
	double α;

	imgop = arg;

	for(p = Pt(0, imgop->y); p.x < Dx(imgop->i->r); p.x++){
		fb = (ulong*)byteaddr(imgop->i, p);
		α = atan2(p.y, p.x);
		pix = α*25523UL*25523UL/* + truerand()*/;
		*fb = pix|0xFF<<24;
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-t] [-n nprocs] [-c count]\n", argv0);
	exits(nil);
}

void
threadmain(int argc, char *argv[])
{
	static int W = 1000, H = 1000;
	Tpool *pool;
	Targs *t;
	Memimage *img;
	int i;
	int threaded;
	int nprocs;
	int cnt;

	threaded = 0;
	nprocs = 8;
	cnt = 100;
	ARGBEGIN{
	case 't': threaded++; break;
	case 'n': nprocs = strtoul(EARGF(usage()), nil, 0); break;
	case 'c': cnt = strtoul(EARGF(usage()), nil, 0); break;
	default: usage();
	}ARGEND;
	if(argc != 0)
		usage();

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");

	img = allocmemimage(Rect(0,0,W,H), XRGB32);
	t = malloc(H*sizeof(*t));
	if(threaded){
		pool = mkthreadpool(nprocs);

		while(cnt--)
		for(i = 0; i < H; i++){
			t[i] = (Targs){img, i};
			threadpoolexec(pool, fillpix, &t[i]);
		}

		while(pool->issued.ref != pool->complete.ref)
			recvp(pool->done);

		writememimage(1, img);

		threadexitsall(nil);
	}

	while(cnt--)
	for(i = 0; i < H; i++){
		t[i] = (Targs){img, i};
		fillpix(&t[i]);
	}
	writememimage(1, img);
	exits(nil);
}
