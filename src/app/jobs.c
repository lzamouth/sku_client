/* Background jobs: a queue of (addon, operation) served by a few threads.
 * Each addon operation parallelizes its own file work through the core's
 * pools, so a handful of concurrent addons is plenty. */
#include "app.h"
#include "log.h"
#include "report.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct job {
	app_item *it;
	job_kind kind;
};

struct jobs {
	app *ap;
	pthread_t *threads;
	int nthreads;
	pthread_mutex_t lock;
	pthread_cond_t  cond;
	struct job *q;
	int head, tail, cap;        /* ring buffer */
	int running;
	atomic_int done;
	bool stop;
};

static void run(app *ap, struct job *j)
{
	app_item *it = j->it;
	sku_check_opts co = { j->kind == JOB_CHECK_FULL, false, ap->prefs.crc_threads };
	sku_update_opts uo = { false, ap->prefs.downloads, 0 };
	switch (j->kind) {
	case JOB_RELOAD:
		sku_addon_reload(it->a);
		break;
	case JOB_CHECK:
	case JOB_CHECK_FULL:
		if (sku_addon_check(it->a, &co) == SKU_OK && it->a->checked) sku_report_save(it->a, "Check");
		break;
	case JOB_UPDATE:
		if (sku_addon_check(it->a, &co) == SKU_OK && it->a->checked) {
			sku_addon_update(it->a, &uo);
			sku_report_save(it->a, "Update");
		}
		break;
	}
}

static void *worker(void *arg)
{
	struct jobs *js = arg;
	for (;;) {
		struct job j;
		pthread_mutex_lock(&js->lock);
		while (js->head == js->tail && !js->stop) pthread_cond_wait(&js->cond, &js->lock);
		if (js->stop && js->head == js->tail) { pthread_mutex_unlock(&js->lock); return NULL; }
		j = js->q[js->head];
		js->head = (js->head + 1) % js->cap;
		js->running++;
		pthread_mutex_unlock(&js->lock);

		run(js->ap, &j);

		pthread_mutex_lock(&js->lock);
		js->running--;
		pthread_mutex_unlock(&js->lock);
		atomic_store(&j.it->busy, 0);
		atomic_fetch_add(&js->done, 1);
	}
}

struct jobs *jobs_create(app *ap, int nthreads)
{
	struct jobs *js = sku_xcalloc(1, sizeof *js);
	js->ap = ap;
	js->nthreads = nthreads < 1 ? 1 : nthreads;
	js->cap = 256;
	js->q = sku_xcalloc((size_t)js->cap, sizeof *js->q);
	pthread_mutex_init(&js->lock, NULL);
	pthread_cond_init(&js->cond, NULL);
	atomic_init(&js->done, 0);
	js->threads = sku_xcalloc((size_t)js->nthreads, sizeof *js->threads);
	for (int i = 0; i < js->nthreads; i++) pthread_create(&js->threads[i], NULL, worker, js);
	return js;
}

void jobs_destroy(struct jobs *js)
{
	if (!js) return;
	pthread_mutex_lock(&js->lock);
	js->stop = true;
	js->head = js->tail;   /* drop what is queued */
	pthread_cond_broadcast(&js->cond);
	pthread_mutex_unlock(&js->lock);
	for (int i = 0; i < js->nthreads; i++) pthread_join(js->threads[i], NULL);
	pthread_mutex_destroy(&js->lock);
	pthread_cond_destroy(&js->cond);
	free(js->threads);
	free(js->q);
	free(js);
}

void jobs_submit(struct jobs *js, app_item *it, job_kind kind)
{
	if (atomic_load(&it->busy)) return;
	atomic_store(&it->busy, 1);
	pthread_mutex_lock(&js->lock);
	if ((js->tail + 1) % js->cap == js->head) {
		/* full: grow the ring */
		int ncap = js->cap * 2, n = 0;
		struct job *nq = sku_xcalloc((size_t)ncap, sizeof *nq);
		for (int i = js->head; i != js->tail; i = (i + 1) % js->cap) nq[n++] = js->q[i];
		free(js->q);
		js->q = nq; js->cap = ncap; js->head = 0; js->tail = n;
	}
	js->q[js->tail] = (struct job){ it, kind };
	js->tail = (js->tail + 1) % js->cap;
	pthread_cond_signal(&js->cond);
	pthread_mutex_unlock(&js->lock);
}

int jobs_running(struct jobs *js)
{
	int n;
	pthread_mutex_lock(&js->lock);
	n = js->running;
	pthread_mutex_unlock(&js->lock);
	return n;
}

int jobs_queued(struct jobs *js)
{
	int n;
	pthread_mutex_lock(&js->lock);
	n = (js->tail - js->head + js->cap) % js->cap;
	pthread_mutex_unlock(&js->lock);
	return n;
}

int jobs_done(struct jobs *js)
{
	return atomic_load(&js->done);
}

void jobs_cancel_all(app *ap)
{
	struct jobs *js = ap->jobs;
	pthread_mutex_lock(&js->lock);
	/* queued jobs are dropped: mark their items idle again */
	for (int i = js->head; i != js->tail; i = (i + 1) % js->cap) atomic_store(&js->q[i].it->busy, 0);
	js->head = js->tail;
	pthread_mutex_unlock(&js->lock);
	/* running ones are asked to stop */
	for (int i = 0; i < ap->nitems; i++)
		if (atomic_load(&ap->items[i].busy)) sku_addon_cancel(ap->items[i].a);
}
