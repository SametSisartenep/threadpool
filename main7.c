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
	ulong off;
	ulong len;
};
void
fillpix(void *arg)
{
	Targs *imgop;
	Point p;
	ulong *fb, *fbb, *fbe, pix;
	double α;

	imgop = arg;
	fb  = (ulong*)byteaddr(imgop->i, ZP);
	fbb = fb + imgop->off;
	fbe = fbb + imgop->len;

	while(fbb < fbe){
		p.x = (fbb-fb)%Dx(imgop->i->r);
		p.y = (fbb-fb)/Dx(imgop->i->r);
		α = atan2(p.y, p.x);
		pix = α*25523UL*25523UL/* + truerand()*/;
		*fbb++ = pix|0xFF<<24;
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
	int i, stride;
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
	t = malloc(nprocs*sizeof(*t));
	stride = W*H/nprocs;
	if(threaded){
		pool = mkthreadpool(nprocs);

		while(cnt--)
		for(i = 0; i < nprocs; i++){
			t[i] = (Targs){img, i*stride, i == nprocs-1? W*H-i*stride: stride};
			threadpoolexec(pool, fillpix, &t[i]);
		}

		while(pool->issued.ref != pool->complete.ref)
			recvp(pool->done);

		writememimage(1, img);

		threadexitsall(nil);
	}

	while(cnt--)
	for(i = 0; i < nprocs; i++){
		t[i] = (Targs){img, i*stride, i == nprocs-1? W*H-i*stride: stride};
		fillpix(&t[i]);
	}
	writememimage(1, img);
	exits(nil);
}
