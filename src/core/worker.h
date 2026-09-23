/* Bounded thread pool: runs njobs jobs on nthreads threads and waits. */
#ifndef SKU_WORKER_H
#define SKU_WORKER_H

#include "sku.h"
#include <stdatomic.h>

/* job: 0..njobs-1, thread: 0..nthreads-1 (lets a job use per-thread resources). */
typedef void (*sku_job_fn)(int job, int thread, void *user);

/* Blocks until every job ran or *cancel became non-zero (remaining jobs are
 * skipped). Returns SKU_ERR_CANCELLED in that case. cancel may be NULL. */
int sku_pool_run(int nthreads, int njobs, sku_job_fn fn, void *user, atomic_int *cancel);

#endif
