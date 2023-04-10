/*
Copyright (C) 2022 Axel Gneiting

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// tasks.c -- parallel task system

#include "tasks.h"
#include "atomics.h"
#include "quakedef.h"

#if defined(USE_HELGRIND)
#include "valgrind/helgrind.h"
#else
#define ANNOTATE_HAPPENS_BEFORE(x) \
	do                             \
	{                              \
	} while (false)
#define ANNOTATE_HAPPENS_AFTER(x) \
	do                            \
	{                             \
	} while (false)
#define ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(x) \
	do                                        \
	{                                         \
	} while (false)
#endif

#define NUM_INDEX_BITS		 8
#define MAX_PENDING_TASKS	 (1u << NUM_INDEX_BITS)
#define MAX_EXECUTABLE_TASKS 256
#define MAX_DEPENDENT_TASKS	 16
#define MAX_PAYLOAD_SIZE	 128
#define WORKER_HUNK_SIZE	 (1 * 1024 * 1024)
#define WAIT_SPIN_COUNT		 100

COMPILE_TIME_ASSERT (tasks, MAX_EXECUTABLE_TASKS >= 256);
COMPILE_TIME_ASSERT (tasks, MAX_PENDING_TASKS >= MAX_EXECUTABLE_TASKS);

typedef enum
{
	TASK_TYPE_NONE,
	TASK_TYPE_SCALAR,
	TASK_TYPE_INDEXED,
} task_type_t;

typedef struct
{
	task_type_t		task_type;
	int				num_dependents;
	int				indexed_limit;
	atomic_uint32_t remaining_workers;
	atomic_uint32_t remaining_dependencies;
	uint64_t		epoch;
	void		   *func;
	SDL_mutex	   *epoch_mutex;
	SDL_cond	   *epoch_condition;
	uint8_t			payload[MAX_PAYLOAD_SIZE];
	task_handle_t	dependent_task_handles[MAX_DEPENDENT_TASKS];
} task_t;

typedef struct
{
	atomic_uint32_t head;
	uint32_t		head_padding[15]; // Pad to 64 byte cache line size
	atomic_uint32_t tail;
	uint32_t		tail_padding[15];
	uint32_t		capacity_mask;
	SDL_sem		   *push_semaphore;
	SDL_sem		   *pop_semaphore;
	atomic_uint32_t task_indices[1];
} task_queue_t;

typedef struct
{
	atomic_uint32_t index;
	uint32_t		limit;
} task_counter_t;

static int					 num_workers = 0;
static SDL_Thread		   **worker_threads;
static task_t				 tasks[MAX_PENDING_TASKS];
static task_queue_t			*free_task_queue;
static task_queue_t			*executable_task_queue;
static task_counter_t		*indexed_task_counters;
static uint8_t				 steal_worker_indices[TASKS_MAX_WORKERS * 2];
static THREAD_LOCAL qboolean is_worker = false;
static THREAD_LOCAL int		 tl_worker_index;

/*
====================
IndexedTaskCounterIndex
====================
*/
static inline int IndexedTaskCounterIndex (int task_index, int worker_index)
{
	return (MAX_PENDING_TASKS * worker_index) + task_index;
}

/*
====================
IndexFromTaskHandle
====================
*/
static inline uint32_t IndexFromTaskHandle (task_handle_t handle)
{
	return handle & (MAX_PENDING_TASKS - 1);
}

/*
====================
EpochFromTaskHandle
====================
*/
static inline uint64_t EpochFromTaskHandle (task_handle_t handle)
{
	return handle >> NUM_INDEX_BITS;
}

/*
====================
CreateTaskHandle
====================
*/
static inline task_handle_t CreateTaskHandle (uint32_t index, int epoch)
{
	return (task_handle_t)index | ((task_handle_t)epoch << NUM_INDEX_BITS);
}

/*
====================
ShuffleIndex
====================
*/
static uint32_t ShuffleIndex (uint32_t i)
{
	// Swap bits 0-3 and 4-7 to avoid false sharing
	return (i & ~0xFF) | ((i & 0xF) << 4) | ((i >> 4) & 0xF);
}

/*
====================
CPUPause
====================
*/
static inline void CPUPause ()
{
#if defined(USE_SSE2)
	// Don't have to actually check for SSE2 support, the
	// instruction is backwards compatible and executes as a NOP
	_mm_pause ();
#elif defined(USE_NEON)
	// Always available on AArch64
	asm volatile ("isb" ::);
#endif
}

/*
====================
SpinWaitSemaphore
====================
*/
static inline void SpinWaitSemaphore (SDL_sem *semaphore)
{
	int remaining_spins = WAIT_SPIN_COUNT;
	int result = 0;
	while ((result = SDL_SemTryWait (semaphore)) != 0)
	{
		CPUPause ();
		if (--remaining_spins == 0)
			break;
	}
	if (result != 0)
		SDL_SemWait (semaphore);
}

/*
====================
CreateTaskQueue
====================
*/
static task_queue_t *CreateTaskQueue (int capacity)
{
	assert (capacity > 0);
	assert ((capacity & (capacity - 1)) == 0); // Needs to be power of 2
	task_queue_t *queue = Mem_Alloc (sizeof (task_queue_t) + (sizeof (atomic_uint32_t) * (capacity - 1)));
	queue->capacity_mask = capacity - 1;
	queue->push_semaphore = SDL_CreateSemaphore (capacity - 1);
	queue->pop_semaphore = SDL_CreateSemaphore (0);
	return queue;
}

/*
====================
TaskQueuePush
====================
*/
static inline void TaskQueuePush (task_queue_t *queue, uint32_t task_index)
{
	SpinWaitSemaphore (queue->push_semaphore);
	uint32_t head = Atomic_LoadUInt32 (&queue->head);
	qboolean cas_successful = false;
	do
	{
		const uint32_t next = (head + 1u) & queue->capacity_mask;
		cas_successful = Atomic_CompareExchangeUInt32 (&queue->head, &head, next);
	} while (!cas_successful);

	const uint32_t shuffled_index = ShuffleIndex (head);
	while (Atomic_LoadUInt32 (&queue->task_indices[shuffled_index]) != 0u)
		CPUPause ();

	ANNOTATE_HAPPENS_BEFORE (&queue->task_indices[shuffled_index]);
	Atomic_StoreUInt32 (&queue->task_indices[shuffled_index], task_index + 1);
	SDL_SemPost (queue->pop_semaphore);
}

/*
====================
TaskQueuePop
====================a
*/
static inline uint32_t TaskQueuePop (task_queue_t *queue)
{
	SpinWaitSemaphore (queue->pop_semaphore);
	uint32_t tail = Atomic_LoadUInt32 (&queue->tail);
	qboolean cas_successful = false;
	do
	{
		const uint32_t next = (tail + 1u) & queue->capacity_mask;
		cas_successful = Atomic_CompareExchangeUInt32 (&queue->tail, &tail, next);
	} while (!cas_successful);

	const uint32_t shuffled_index = ShuffleIndex (tail);
	while (Atomic_LoadUInt32 (&queue->task_indices[shuffled_index]) == 0u)
		CPUPause ();

	const uint32_t val = Atomic_LoadUInt32 (&queue->task_indices[shuffled_index]) - 1;
	Atomic_StoreUInt32 (&queue->task_indices[shuffled_index], 0u);
	SDL_SemPost (queue->push_semaphore);
	ANNOTATE_HAPPENS_AFTER (&queue->task_indices[shuffled_index]);

	return val;
}

/*
====================
Task_ExecuteIndexed
====================
*/
static inline void Task_ExecuteIndexed (int worker_index, task_t *task, uint32_t task_index)
{
	for (int i = 0; i < num_workers; ++i)
	{
		const int		steal_worker_index = steal_worker_indices[worker_index + i];
		int				counter_index = IndexedTaskCounterIndex (task_index, steal_worker_index);
		task_counter_t *counter = &indexed_task_counters[counter_index];
		uint32_t		index = 0;
		while ((index = Atomic_IncrementUInt32 (&counter->index)) < counter->limit)
		{
			((task_indexed_func_t)task->func) (index, task->payload);
		}
	}
}

/*
====================
Task_Worker
====================
*/
static int Task_Worker (void *data)
{
	is_worker = true;

	const int worker_index = (intptr_t)data;
	tl_worker_index = worker_index;
	while (true)
	{
		uint32_t task_index = TaskQueuePop (executable_task_queue);
		task_t	*task = &tasks[task_index];
		ANNOTATE_HAPPENS_AFTER (task);

		if (task->task_type == TASK_TYPE_SCALAR)
		{
			((task_func_t)task->func) (task->payload);
		}
		else if (task->task_type == TASK_TYPE_INDEXED)
		{
			Task_ExecuteIndexed (worker_index, task, task_index);
		}

#if defined(USE_HELGRIND)
		ANNOTATE_HAPPENS_BEFORE (task);
		qboolean indexed_task = task->task_type == TASK_TYPE_INDEXED;
		if (indexed_task)
		{
			// Helgrind needs to know about all threads
			// that participated in an indexed execution
			SDL_LockMutex (task->epoch_mutex);
			for (int i = 0; i < task->num_dependents; ++i)
			{
				const int task_index = IndexFromTaskHandle (task->dependent_task_handles[i]);
				task_t	 *dep_task = &tasks[task_index];
				ANNOTATE_HAPPENS_BEFORE (dep_task);
			}
		}
#endif

		if (Atomic_DecrementUInt32 (&task->remaining_workers) == 1)
		{
			SDL_LockMutex (task->epoch_mutex);
			for (int i = 0; i < task->num_dependents; ++i)
				Task_Submit (task->dependent_task_handles[i]);
			task->epoch += 1;
			SDL_CondBroadcast (task->epoch_condition);
			SDL_UnlockMutex (task->epoch_mutex);
			TaskQueuePush (free_task_queue, task_index);
		}

#if defined(USE_HELGRIND)
		if (indexed_task)
			SDL_UnlockMutex (task->epoch_mutex);
#endif
	}
	return 0;
}

/*
====================
Tasks_Init
====================
*/
void Tasks_Init (void)
{
	free_task_queue = CreateTaskQueue (MAX_PENDING_TASKS);
	executable_task_queue = CreateTaskQueue (MAX_EXECUTABLE_TASKS);

	for (uint32_t task_index = 0; task_index < (MAX_PENDING_TASKS - 1); ++task_index)
	{
		TaskQueuePush (free_task_queue, task_index);
	}

	for (uint32_t task_index = 0; task_index < MAX_PENDING_TASKS; ++task_index)
	{
		tasks[task_index].epoch_mutex = SDL_CreateMutex ();
		tasks[task_index].epoch_condition = SDL_CreateCond ();
	}

	num_workers = CLAMP (1, SDL_GetCPUCount (), TASKS_MAX_WORKERS);

	// Fill lookup table to avoid modulo in Task_ExecuteIndexed
	for (int i = 0; i < num_workers; ++i)
	{
		steal_worker_indices[i] = i;
		steal_worker_indices[i + num_workers] = i;
	}

	indexed_task_counters = Mem_Alloc (sizeof (task_counter_t) * num_workers * MAX_PENDING_TASKS);
	worker_threads = (SDL_Thread **)Mem_Alloc (sizeof (SDL_Thread *) * num_workers);
	for (int i = 0; i < num_workers; ++i)
	{
		worker_threads[i] = SDL_CreateThread (Task_Worker, "Task_Worker", (void *)(intptr_t)i);
	}
}

/*
====================
Tasks_NumWorkers
====================
*/
int Tasks_NumWorkers (void)
{
	return num_workers;
}

/*
====================
Tasks_IsWorker
====================
*/
qboolean Tasks_IsWorker (void)
{
	return is_worker;
}
/*
====================
Tasks_GetWorkerIndex
====================
*/
int Tasks_GetWorkerIndex (void)
{
	return tl_worker_index;
}

/*
====================
Task_Allocate
====================
*/
task_handle_t Task_Allocate (void)
{
	uint32_t task_index = TaskQueuePop (free_task_queue);
	task_t	*task = &tasks[task_index];
	Atomic_StoreUInt32 (&task->remaining_dependencies, 1);
	task->task_type = TASK_TYPE_NONE;
	task->num_dependents = 0;
	task->indexed_limit = 0;
	task->func = NULL;
	return CreateTaskHandle (task_index, task->epoch);
}

/*
====================
Task_AssignFunc
====================
*/
void Task_AssignFunc (task_handle_t handle, task_func_t func, void *payload, size_t payload_size)
{
	assert (payload_size <= MAX_PAYLOAD_SIZE);
	task_t *task = &tasks[IndexFromTaskHandle (handle)];
	task->task_type = TASK_TYPE_SCALAR;
	task->func = (void *)func;
	if (payload)
		memcpy (&task->payload, payload, payload_size);
}

/*
====================
Task_AssignIndexedFunc
====================
*/
void Task_AssignIndexedFunc (task_handle_t handle, task_indexed_func_t func, uint32_t limit, void *payload, size_t payload_size)
{
	assert (payload_size <= MAX_PAYLOAD_SIZE);
	uint32_t task_index = IndexFromTaskHandle (handle);
	task_t	*task = &tasks[task_index];
	task->task_type = TASK_TYPE_INDEXED;
	task->func = (void *)func;
	task->indexed_limit = limit;
	uint32_t index = 0;
	uint32_t count_per_worker = (limit + num_workers - 1) / num_workers;
	for (int worker_index = 0; worker_index < num_workers; ++worker_index)
	{
		const int		task_counter_index = IndexedTaskCounterIndex (task_index, worker_index);
		task_counter_t *counter = &indexed_task_counters[task_counter_index];
		Atomic_StoreUInt32 (&counter->index, index);
		counter->limit = q_min (index + count_per_worker, limit);
		index += count_per_worker;
	}
	if (payload)
		memcpy (&task->payload, payload, payload_size);
}

/*
====================
Task_Submit
====================
*/
void Task_Submit (task_handle_t handle)
{
	uint32_t task_index = IndexFromTaskHandle (handle);
	task_t	*task = &tasks[task_index];
	assert (task->epoch == EpochFromTaskHandle (handle));
	ANNOTATE_HAPPENS_BEFORE (task);
	if (Atomic_DecrementUInt32 (&task->remaining_dependencies) == 1)
	{
		const int num_task_workers = (task->task_type == TASK_TYPE_INDEXED) ? q_min (task->indexed_limit, num_workers) : 1;
		Atomic_StoreUInt32 (&task->remaining_workers, num_task_workers);
		for (int i = 0; i < num_task_workers; ++i)
		{
			TaskQueuePush (executable_task_queue, task_index);
		}
	}
}
/*
====================
Tasks_Submit
====================
*/

void Tasks_Submit (int num_handles, task_handle_t *handles)
{
	for (int i = 0; i < num_handles; ++i)
	{
		Task_Submit (handles[i]);
	}
}

/*
====================
Task_AddDependency
====================
*/
void Task_AddDependency (task_handle_t before, task_handle_t after)
{
	uint32_t  before_task_index = IndexFromTaskHandle (before);
	task_t	 *before_task = &tasks[before_task_index];
	const int before_handle_task_epoch = EpochFromTaskHandle (before);
	SDL_LockMutex (before_task->epoch_mutex);
	if (before_task->epoch != before_handle_task_epoch)
	{
		ANNOTATE_HAPPENS_AFTER (before_task);
		SDL_UnlockMutex (before_task->epoch_mutex);
		return;
	}
	uint32_t after_task_index = IndexFromTaskHandle (after);
	task_t	*after_task = &tasks[after_task_index];
	assert (before_task->num_dependents < MAX_DEPENDENT_TASKS);
	before_task->dependent_task_handles[before_task->num_dependents] = after;
	before_task->num_dependents += 1;
	Atomic_IncrementUInt32 (&after_task->remaining_dependencies);
	SDL_UnlockMutex (before_task->epoch_mutex);
}

/*
====================
Task_Join
====================
*/
qboolean Task_Join (task_handle_t handle, uint32_t timeout)
{
	task_t	 *task = &tasks[IndexFromTaskHandle (handle)];
	const int handle_task_epoch = EpochFromTaskHandle (handle);
	SDL_LockMutex (task->epoch_mutex);
	while (task->epoch == handle_task_epoch)
	{
		if (SDL_CondWaitTimeout (task->epoch_condition, task->epoch_mutex, timeout) == SDL_MUTEX_TIMEDOUT)
		{
			SDL_UnlockMutex (task->epoch_mutex);
			return false;
		}
	}
	SDL_UnlockMutex (task->epoch_mutex);
	ANNOTATE_HAPPENS_AFTER (task);
	return true;
}

#ifdef _DEBUG
/*
=================
TASKS_TEST_ASSERT
=================
*/
#define TASKS_TEST_ASSERT(cond, what) \
	if (!(cond))                      \
	{                                 \
		Con_Printf ("%s\n", what);    \
		abort ();                     \
	}

/*
=================
LotsOfTasks
=================
*/
static void LotsOfTasksTestTask (void *counters_ptr)
{
	uint32_t *counters = *((uint32_t **)counters_ptr);
	++counters[Tasks_GetWorkerIndex ()];
}
static void LotsOfTasks (void)
{
	static const int NUM_TASKS = 100000;
	TEMP_ALLOC_ZEROED (uint32_t, counters, TASKS_MAX_WORKERS);
	TEMP_ALLOC (task_handle_t, handles, NUM_TASKS);
	for (int i = 0; i < NUM_TASKS; ++i)
		handles[i] = Task_AllocateAssignFuncAndSubmit (LotsOfTasksTestTask, (void *)&counters, sizeof (uint32_t *));
	for (int i = 0; i < NUM_TASKS; ++i)
		Task_Join (handles[i], SDL_MUTEX_MAXWAIT);
	uint32_t counters_sum = 0;
	for (int i = 0; i < TASKS_MAX_WORKERS; ++i)
		counters_sum += counters[i];
	TASKS_TEST_ASSERT (counters_sum == NUM_TASKS, "Wrong counters_sum");
	TEMP_FREE (handles);
	TEMP_FREE (counters);
}

/*
=================
IndexedTasks
=================
*/
static void IndexedTestTask (int index, void *counters_ptr)
{
	uint32_t *counters = *((uint32_t **)counters_ptr);
	++counters[Tasks_GetWorkerIndex ()];
}
static void IndexedTasks ()
{
	static const int LIMIT = 100000;
	TEMP_ALLOC_ZEROED (uint32_t, counters, TASKS_MAX_WORKERS);
	task_handle_t task = Task_AllocateAssignIndexedFuncAndSubmit (IndexedTestTask, LIMIT, (void *)&counters, sizeof (uint32_t *));
	Task_Join (task, SDL_MUTEX_MAXWAIT);
	uint32_t counters_sum = 0;
	for (int i = 0; i < TASKS_MAX_WORKERS; ++i)
		counters_sum += counters[i];
	TASKS_TEST_ASSERT (counters_sum == LIMIT, "Wrong counters_sum");
	TEMP_FREE (counters);
}

/*
=================
TestTasks_f
=================
*/
void TestTasks_f (void)
{
	LotsOfTasks ();
	IndexedTasks ();
}
#endif