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

typedef uint64_t task_handle_t;
typedef void (*task_func_t) (void *);
typedef void (*task_indexed_func_t) (int, void *);

void		  Tasks_Init (void);
int			  Tasks_NumWorkers (void);
qboolean	  Tasks_IsWorker (void);
int			  Tasks_GetWorkerIndex (void);
task_handle_t Task_Allocate (void);
void		  Task_AssignFunc (task_handle_t handle, task_func_t func, void *payload, size_t payload_size);
void		  Task_AssignIndexedFunc (task_handle_t handle, task_indexed_func_t func, uint32_t limit, void *payload, size_t payload_size);
void		  Task_Submit (task_handle_t handle);
void		  Tasks_Submit (int num_handles, task_handle_t *handles);
void		  Task_AddDependency (task_handle_t before, task_handle_t after);
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

#endif
