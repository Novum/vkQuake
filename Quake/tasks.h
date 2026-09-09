/*
 * tasks.h -- parallel task system
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __TASKS_H
#define __TASKS_H

#include "q_stdinc.h"

#include <stdint.h>
#include <stddef.h>

#define INVALID_TASK_HANDLE UINT64_MAX
#define TASKS_MAX_WORKERS	32

// Identifies one task, even after it finishes and its storage is reused.
// Finished tasks can still be joined or used as dependencies, but not resubmitted.
typedef uint64_t task_handle_t;
typedef void (*task_func_t) (void *);
typedef void (*task_indexed_func_t) (int, void *);

void		  Tasks_Init (void);
int			  Tasks_NumWorkers (void);
qboolean	  Tasks_IsWorker (void);
// The current worker's index. Only valid on a worker thread.
int			  Tasks_GetWorkerIndex (void);
// Reserves a task, waiting if none are free. Without a function, the task simply
// waits for its dependencies and then finishes.
task_handle_t Task_Allocate (void);
// Sets the function to run and copies its input data (up to 128 bytes).
// The function receives the copy. Anything it points to must stay alive until
// the function finishes. This does not start the task.
void		  Task_AssignFunc (task_handle_t handle, task_func_t func, void *payload, size_t payload_size);
// Sets up a parallel loop: func(index, payload) runs once for each index from
// 0 to limit - 1. Workers split the items, so calls can run at the same time
// and in any order. All calls share one copy of the input data.
// The whole loop counts as one task and finishes when every call is done.
void		  Task_AssignIndexedFunc (task_handle_t handle, task_indexed_func_t func, uint32_t limit, void *payload, size_t payload_size);
// Makes the task ready to run once its dependencies finish. Called once by the
// caller, after setting the function and adding dependencies.
void		  Task_Submit (task_handle_t handle);
void		  Tasks_Submit (int num_handles, task_handle_t *handles);
// Makes 'after' wait for 'before'. Added before submitting 'after'; 'before'
// can already be running or finished.
void		  Task_AddDependency (task_handle_t before, task_handle_t after);
// Waits for the task to finish. Returns true when done, false if the wait ends
// early. timeout is milliseconds per wait, or TASK_TIMEOUT_INFINITE to keep
// waiting. The calling thread waits rather than helping execute tasks.
qboolean	  Task_Join (task_handle_t handle, uint32_t timeout);

static inline task_handle_t Task_AllocateAndAssignFunc (task_func_t func, void *payload, size_t payload_size)
{
	task_handle_t handle = Task_Allocate ();
	Task_AssignFunc (handle, func, payload, payload_size);
	return handle;
}

static inline task_handle_t Task_AllocateAndAssignIndexedFunc (task_indexed_func_t func, uint32_t limit, void *payload, size_t payload_size)
{
	task_handle_t handle = Task_Allocate ();
	Task_AssignIndexedFunc (handle, func, limit, payload, payload_size);
	return handle;
}

static inline task_handle_t Task_AllocateAssignFuncAndSubmit (task_func_t func, void *payload, size_t payload_size)
{
	task_handle_t handle = Task_Allocate ();
	Task_AssignFunc (handle, func, payload, payload_size);
	Task_Submit (handle);
	return handle;
}

static inline task_handle_t Task_AllocateAssignIndexedFuncAndSubmit (task_indexed_func_t func, uint32_t limit, void *payload, size_t payload_size)
{
	task_handle_t handle = Task_Allocate ();
	Task_AssignIndexedFunc (handle, func, limit, payload, payload_size);
	Task_Submit (handle);
	return handle;
}

#ifdef _DEBUG
void TestTasks_f (void);
#endif

#ifdef USE_SDL3
// passed through to SDL3's Sint32 millisecond timeouts, where -1 means infinite
#define TASK_TIMEOUT_INFINITE ((uint32_t) - 1)
#else
#define TASK_TIMEOUT_INFINITE SDL_MUTEX_MAXWAIT
#endif

#endif
