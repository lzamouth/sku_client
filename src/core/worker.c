#include "worker.h"
#include <stdlib.h>
#include <pthread.h>

struct pool {
	sku_job_fn fn;
	void *user;
	int njobs;
	atomic_int next;
	atomic_int *cancel;
};

struct thread_arg {
	struct pool *p;
	int index;
};

static void *thread_main(void *arg)
{
	struct thread_arg *t = arg;
	struct pool *p = t->p;
	for (;;) {
		int job;
		if (p->cancel && atomic_load(p->cancel)) break;
		job = atomic_fetch_add(&p->next, 1);
		if (job >= p->njobs) break;
		p->fn(job, t->index, p->user);
	}
	return NULL;
}

int sku_pool_run(int nthreads, int njobs, sku_job_fn fn, void *user, atomic_int *cancel)
{
	struct pool p;
	pthread_t *threads;
	struct thread_arg *args;
	int started = 0;

	if (njobs <= 0) return SKU_OK;
	if (nthreads < 1) nthreads = 1;
	if (nthreads > njobs) nthreads = njobs;
	p.fn = fn; p.user = user; p.njobs = njobs; p.cancel = cancel;
	atomic_init(&p.next, 0);

	threads = sku_xmalloc((size_t)nthreads * sizeof *threads);
	args = sku_xmalloc((size_t)nthreads * sizeof *args);
	for (int i = 0; i < nthreads; i++) {
		args[i].p = &p; args[i].index = i;
		if (pthread_create(&threads[i], NULL, thread_main, &args[i]) != 0) break;
		started++;
	}
	if (started == 0) thread_main(&args[0]);   /* no threads at all: run inline */
	for (int i = 0; i < started; i++) pthread_join(threads[i], NULL);
	free(threads);
	free(args);
	return (cancel && atomic_load(cancel)) ? SKU_ERR_CANCELLED : SKU_OK;
}
