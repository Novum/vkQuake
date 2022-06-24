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

#define MAX_PENDING_TASKS    1024
#define MAX_EXECUTABLE_TASKS 256
#define MAX_DEPENDENT_TASKS  16
#define MAX_PAYLOAD_SIZE     32
#define INDEX_BITS           20
#define GUTTER_BITS          2
#define MAX_WORKERS          32
#define WORKER_HUNK_SIZE     (1 * 1024 * 1024)
#define WAIT_SPIN_COUNT      1000
#define WAIT_SLEEP_COUNT     3

COMPILE_TIME_ASSERT (tasks, MAX_PENDING_TASKS >= MAX_EXECUTABLE_TASKS);

typedef enum
{
	TASK_TYPE_NONE,
	TASK_TYPE_SCALAR,
	TASK_TYPE_INDEXED,
} task_type_t;

typedef struct
{
	task_type_t     task_type;
	uint32_t        id;
	int             num_dependents;
	int             indexed_limit;
	atomic_uint32_t remaining_workers;
	atomic_uint32_t remaining_dependencies;
	void           *func;
	SDL_mutex      *id_mutex;
	SDL_cond       *id_condition;
	uint8_t         payload[MAX_PAYLOAD_SIZE];
	task_handle_t   dependent_task_handles[MAX_DEPENDENT_TASKS];
} task_t;

typedef struct
{
	uint32_t        capacity_mask;
	atomic_uint64_t state;
	SDL_sem        *push_semaphore;
	SDL_sem        *pop_semaphore;
	atomic_uint32_t task_indices[1];
} task_queue_t;

typedef struct
{
	atomic_uint32_t index;
	uint32_t        limit;
} task_counter_t;

static int                   num_workers = 0;
static SDL_Thread          **worker_threads;
static task_t                tasks[MAX_PENDING_TASKS];
static task_queue_t         *free_task_queue;
static task_queue_t         *executable_task_queue;
static atomic_uint32_t       current_task_id;
static task_counter_t       *indexed_task_counters;
static uint8_t               steal_worker_indices[MAX_WORKERS * 2];
static THREAD_LOCAL qboolean is_worker = false;

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
	return handle & 0xFFFFFFFFull;
}

/*
====================
IdFromTaskHandle
====================
*/
static inline int IdFromTaskHandle (task_handle_t handle)
{
	return handle >> 32;
}

/*
====================
CreateTaskHandle
====================
*/
static inline task_handle_t CreateTaskHandle (uint32_t index, int id)
{
	return (task_handle_t)index | ((task_handle_t)id << 32);
}

/*
====================
SpinWaitSemaphore
====================
*/
static inline void SpinWaitSemaphore (SDL_sem *semaphore)
{
	int remaining_sleeps = WAIT_SLEEP_COUNT;
	int remaining_spins = WAIT_SPIN_COUNT;
	int result = 0;
	while ((result = SDL_SemTryWait (semaphore)) != 0)
	{
		if (--remaining_spins == 0)
		{
			if (--remaining_sleeps == 0)
				break;
			else
				SDL_Delay (0);
			remaining_spins = WAIT_SPIN_COUNT;
#ifdef USE_SSE2
			// Don't have to actually check for SSE2 support, the
			// instruction is backwards compatible and executes as a NOP
			_mm_pause ();
#endif
		}
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
	uint64_t state = Atomic_LoadUInt64 (&queue->state);
	uint64_t new_state;
	uint32_t head;
	qboolean cas_successful = false;
	do
	{
		head = (uint32_t)(state & queue->capacity_mask);
		const uint32_t tail = (uint32_t)(state >> 32) & queue->capacity_mask;
		const uint32_t next = (head + 1u) & queue->capacity_mask;
		if ((next == tail) || (Atomic_LoadUInt32 (&queue->task_indices[head]) != 0u))
		{
			state = Atomic_LoadUInt64 (&queue->state);
			continue;
		}
		// avoid overflow
		new_state = (state & (~0x80000000ull)) + 1;
		cas_successful = Atomic_CompareExchangeUInt64 (&queue->state, &state, new_state);
	} while (!cas_successful);

	Atomic_StoreUInt32 (&queue->task_indices[head], task_index + 1u);
	SDL_SemPost (queue->pop_semaphore);
}

/*
====================
TaskQueuePop
====================
*/
static inline uint32_t TaskQueuePop (task_queue_t *queue)
{
	SpinWaitSemaphore (queue->pop_semaphore);
	uint64_t state = Atomic_LoadUInt64 (&queue->state);
	uint64_t new_state;
	uint32_t tail;
	qboolean cas_successful = false;
	do
	{
		const uint32_t head = (uint32_t)(state & queue->capacity_mask);
		tail = (uint32_t)(state >> 32) & queue->capacity_mask;
		if ((head == tail) || (Atomic_LoadUInt32 (&queue->task_indices[tail]) == 0u))
		{
			state = Atomic_LoadUInt64 (&queue->state);
			continue;
		}
		// avoid overflow
		new_state = state + 0x100000000ull;
		cas_successful = Atomic_CompareExchangeUInt64 (&queue->state, &state, new_state);
	} while (!cas_successful);

	const uint32_t val = Atomic_LoadUInt32 (&queue->task_indices[tail]);
	Atomic_StoreUInt32 (&queue->task_indices[tail], 0u);
	SDL_SemPost (queue->push_semaphore);
	return val - 1;
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
		const int       steal_worker_index = steal_worker_indices[worker_index + i];
		int             counter_index = IndexedTaskCounterIndex (task_index, steal_worker_index);
		task_counter_t *counter = &indexed_task_counters[counter_index];
		uint32_t        index = 0;
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
	while (true)
	{
		uint32_t task_index = TaskQueuePop (executable_task_queue);
		task_t  *task = &tasks[task_index];
		if (task->task_type == TASK_TYPE_SCALAR)
		{
			((task_func_t)task->func) (task->payload);
		}
		else if (task->task_type == TASK_TYPE_INDEXED)
		{
			Task_ExecuteIndexed (worker_index, task, task_index);
		}
		if (Atomic_DecrementUInt32 (&task->remaining_workers) == 1)
		{
			SDL_LockMutex (task->id_mutex);
			for (int i = 0; i < task->num_dependents; ++i)
			{
				Task_Submit (task->dependent_task_handles[i]);
			}
			// Invalidate ID by making it older than the task handle
			task->id -= 1;
			SDL_CondBroadcast (task->id_condition);
			SDL_UnlockMutex (task->id_mutex);
			TaskQueuePush (free_task_queue, task_index);
		}
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
	Atomic_StoreUInt32 (&current_task_id, 0);

	free_task_queue = CreateTaskQueue (MAX_PENDING_TASKS);
	executable_task_queue = CreateTaskQueue (MAX_EXECUTABLE_TASKS);

	for (uint32_t task_index = 0; task_index < (MAX_PENDING_TASKS - 1); ++task_index)
	{
		TaskQueuePush (free_task_queue, task_index);
	}

	for (uint32_t task_index = 0; task_index < MAX_PENDING_TASKS; ++task_index)
	{
		tasks[task_index].id_mutex = SDL_CreateMutex ();
		tasks[task_index].id_condition = SDL_CreateCond ();
	}

	num_workers = CLAMP (1, SDL_GetCPUCount (), MAX_WORKERS);

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
Task_Allocate
====================
*/
task_handle_t Task_Allocate (void)
{
	uint32_t task_index = TaskQueuePop (free_task_queue);
	task_t  *task = &tasks[task_index];
	int      id = Atomic_IncrementUInt32 (&current_task_id);
	Atomic_StoreUInt32 (&task->remaining_dependencies, 1);
	task->task_type = TASK_TYPE_NONE;
	task->id = id;
	task->num_dependents = 0;
	task->indexed_limit = 0;
	task->func = NULL;
	return CreateTaskHandle (task_index, id);
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
	task_t  *task = &tasks[task_index];
	task->task_type = TASK_TYPE_INDEXED;
	task->func = (void *)func;
	task->indexed_limit = limit;
	uint32_t index = 0;
	uint32_t count_per_worker = (limit + num_workers - 1) / num_workers;
	for (int worker_index = 0; worker_index < num_workers; ++worker_index)
	{
		const int       task_counter_index = IndexedTaskCounterIndex (task_index, worker_index);
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
	task_t  *task = &tasks[task_index];
	assert (task->id == IdFromTaskHandle (handle));
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
	task_t   *before_task = &tasks[before_task_index];
	const int before_handle_task_id = IdFromTaskHandle (before);
	SDL_LockMutex (before_task->id_mutex);
	if (before_task->id != before_handle_task_id)
	{
		SDL_UnlockMutex (before_task->id_mutex);
		return;
	}
	uint32_t after_task_index = IndexFromTaskHandle (after);
	task_t  *after_task = &tasks[after_task_index];
	assert (before_task->num_dependents < MAX_DEPENDENT_TASKS);
	before_task->dependent_task_handles[before_task->num_dependents] = after;
	before_task->num_dependents += 1;
	SDL_UnlockMutex (before_task->id_mutex);
	Atomic_IncrementUInt32 (&after_task->remaining_dependencies);
}

/*
====================
Task_Join
====================
*/
qboolean Task_Join (task_handle_t handle, uint32_t timeout)
{
	task_t   *task = &tasks[IndexFromTaskHandle (handle)];
	const int handle_task_id = IdFromTaskHandle (handle);
	SDL_LockMutex (task->id_mutex);
	while (task->id == handle_task_id)
	{
		if (SDL_CondWaitTimeout (task->id_condition, task->id_mutex, timeout) == SDL_MUTEX_TIMEDOUT)
		{
			SDL_UnlockMutex (task->id_mutex);
			return false;
		}
	}
	SDL_UnlockMutex (task->id_mutex);
	return true;
}