/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2010-2014 QuakeSpasm developers

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

#ifndef __PR_COMP_H
#define __PR_COMP_H

// this file is shared by quake and qcc

typedef unsigned int func_t;
typedef int			 string_t;

#if _MSC_VER >= 1300
#define Q_ALIGN(a) __declspec (align (a))
#elif defined(__clang__)
#define Q_ALIGN(a) __attribute__ ((aligned (a)))
#elif __GNUC__ >= 3
#define Q_ALIGN(a) __attribute__ ((aligned (a)))
#else
#define Q_ALIGN(a)
#endif
// 64bit types need alignment hints to ensure we don't get misalignment exceptions on other platforms.
typedef Q_ALIGN (4) int64_t qcsint64_t;
typedef Q_ALIGN (4) uint64_t qcuint64_t;
typedef Q_ALIGN (4) double qcdouble_t;

typedef enum
{
	ev_bad = -1,
	ev_void = 0,
	ev_string,
	ev_float,
	ev_vector,
	ev_entity,
	ev_field,
	ev_function,
	ev_pointer,

	ev_ext_integer,
	ev_ext_uint32,
	ev_ext_sint64,
	ev_ext_uint64,
	ev_ext_double,
} etype_t;

#define OFS_NULL	 0
#define OFS_RETURN	 1
#define OFS_PARM0	 4 // leave 3 ofs for each parm to hold vectors
#define OFS_PARM1	 7
#define OFS_PARM2	 10
#define OFS_PARM3	 13
#define OFS_PARM4	 16
#define OFS_PARM5	 19
#define OFS_PARM6	 22
#define OFS_PARM7	 25
#define RESERVED_OFS 28

enum
{
	OP_DONE,
	OP_MUL_F,
	OP_MUL_V,
	OP_MUL_FV,
	OP_MUL_VF,
	OP_DIV_F,
	OP_ADD_F,
	OP_ADD_V,
	OP_SUB_F,
	OP_SUB_V,

	OP_EQ_F,
	OP_EQ_V,
	OP_EQ_S,
	OP_EQ_E,
	OP_EQ_FNC,

	OP_NE_F,
	OP_NE_V,
	OP_NE_S,
	OP_NE_E,
	OP_NE_FNC,

	OP_LE,
	OP_GE,
	OP_LT,
	OP_GT,

	OP_LOAD_F,
	OP_LOAD_V,
	OP_LOAD_S,
	OP_LOAD_ENT,
	OP_LOAD_FLD,
	OP_LOAD_FNC,

	OP_ADDRESS,

	OP_STORE_F,
	OP_STORE_V,
	OP_STORE_S,
	OP_STORE_ENT,
	OP_STORE_FLD,
	OP_STORE_FNC,

	OP_STOREP_F,
	OP_STOREP_V,
	OP_STOREP_S,
	OP_STOREP_ENT,
	OP_STOREP_FLD,
	OP_STOREP_FNC,

	OP_RETURN,
	OP_NOT_F,
	OP_NOT_V,
	OP_NOT_S,
	OP_NOT_ENT,
	OP_NOT_FNC,
	OP_IF,
	OP_IFNOT,
	OP_CALL0,
	OP_CALL1,
	OP_CALL2,
	OP_CALL3,
	OP_CALL4,
	OP_CALL5,
	OP_CALL6,
	OP_CALL7,
	OP_CALL8,
	OP_STATE,
	OP_GOTO,
	OP_AND,
	OP_OR,

	OP_BITAND,
	OP_BITOR
};

typedef struct statement_s
{
	unsigned short op;
	short		   a, b, c;
} dstatement_t;

typedef struct
{
	unsigned short type; // if DEF_SAVEGLOBAL bit is set
						 // the variable needs to be saved in savegames
	unsigned short ofs;
	int			   s_name;
} ddef_t;

#define DEF_SAVEGLOBAL (1 << 15)

#define MAX_PARMS 8

typedef struct
{
	int first_statement; // negative numbers are builtins
	int parm_start;
	int locals; // total ints of parms + locals

	int profile; // runtime

	int s_name;
	int s_file; // source file defined in

	int	 numparms;
	byte parm_size[MAX_PARMS];
} dfunction_t;

#define PROG_VERSION 6
typedef struct
{
	int version;
	int crc; // check of header file

	int ofs_statements;
	int numstatements; // statement 0 is an error

	int ofs_globaldefs;
	int numglobaldefs;

	int ofs_fielddefs;
	int numfielddefs;

	int ofs_functions;
	int numfunctions; // function 0 is an empty

	int ofs_strings;
	int numstrings; // first string is a null string

	int ofs_globals;
	int numglobals;

	int entityfields;
} dprograms_t;

#endif /* __PR_COMP_H */
