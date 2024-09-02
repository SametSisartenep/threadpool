#include <u.h>
#include <libc.h>
#include <thread.h>

typedef struct Ttask Ttask;
typedef struct Taskq Taskq;
typedef struct Tpool Tpool;

struct Ttask
{
	void (*fn)(void*);
	void *arg;
	Ttask *next;
};

struct Taskq
{
	Ttask *hd;
	Ttask *tl;
};

struct Tpool
{
	QLock;
	ulong nprocs;
	Ref nworking;

	Taskq subq;	/* task submission queue */
	Channel *done;	/* task completion signal */
};

void
taskqput(Tpool *tp, Ttask *t)
{
	qlock(tp);
	if(tp->subq.tl == nil){
		tp->subq.hd = tp->subq.tl = t;
		qunlock(tp);
		return;
	}

	tp->subq.tl->next = t;
	tp->subq.tl = t;
	qunlock(tp);
}

Ttask *
taskqget(Tpool *tp)
{
	Ttask *t;

	qlock(tp);
	if(tp->subq.hd == nil){
		qunlock(tp);
		return nil;
	}

	t = tp->subq.hd;
	tp->subq.hd = t->next;
	t->next = nil;
	if(tp->subq.hd == nil)
		tp->subq.tl = nil;
	qunlock(tp);
	return t;
}

void
threadloop(void *arg)
{
	Tpool *pool;
	Ttask *task;

	pool = arg;

	for(;;){
		task = taskqget(pool);
		if(task == nil)
			continue;
		incref(&pool->nworking);
		task->fn(task->arg);
		decref(&pool->nworking);
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
	memset(t, 0, sizeof *t);
	t->fn = fn;
	t->arg = arg;
	taskqput(tp, t);
}

typedef struct Tsum Tsum;
struct Tsum
{
	int a;
	int b;
};
void
sum(void *arg)
{
	Tsum *sum;
	int cnt;

	sum = arg;
	cnt = 100;
	while(cnt--) sum->a = sum->a+sum->b;
}

void
usage(void)
{
	fprint(2, "usage: %s [-t] [-n nprocs]\n", argv0);
	exits(nil);
}

void
threadmain(int argc, char *argv[])
{
	static int W = 10, H = 10;
	Tpool *pool;
	Tsum *t;
	int i, j;
	int threaded;
	int nprocs;

	threaded = 0;
	nprocs = 8;
	ARGBEGIN{
	case 't': threaded++; break;
	case 'n': nprocs = strtoul(EARGF(usage()), nil, 0); break;
	default: usage();
	}ARGEND;
	if(argc != 0)
		usage();

	t = malloc(W*H*sizeof(*t));
	if(threaded){
		pool = mkthreadpool(nprocs);

		for(i = 0; i < H; i++)
		for(j = 0; j < W; j++){
			t[i*W+j] = (Tsum){i, j};
			threadpoolexec(pool, sum, &t[i*W+j]);
		}

		while(pool->nworking.ref > 0)
			recvp(pool->done);

		threadexitsall(nil);
	}

	for(i = 0; i < H; i++)
	for(j = 0; j < W; j++){
		t[i*W+j] = (Tsum){i, j};
		sum(&t[i*W+j]);
	}
	exits(nil);
}
