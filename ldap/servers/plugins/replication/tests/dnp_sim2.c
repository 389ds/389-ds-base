/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* dnp_simulation.c - this file varifies the correctness of dnp algorithm
                      by generating random sequences of operations, applying
                      the algorithm and outputing the result

   usage: dnp_sim [-h] [-n <number of simulations> ] [-v] [-f <output file>]
		-h - print usage information.
		-n <number of simulations> - how many simulations to perform; default - 1.
		-v - verbose mode (prints full entry state after each operation execution)
		-f <output file> - file where results are stored; by default results are
						   printed to the screen.
		-o <op file> - file that contains operation sequence to execute; by default,
					   random sequence is generated.
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <memory.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define MAX_OPS		18	/* maximum number of operations in a simulation */
#define MAX_VALS	10	/* maximum number of values is entry or dn */
#define NOT_PRESENT -1

/* data types */
typedef struct value_state
{
	int value_index;			/* value */
	int presence_csn;			/* last time at which we know the value was present */
	int distinguished_index;	/* index into dncsn list */
	int delete_csn;				/* last attempt to delete this value */
	int present;				/* flag that tells whether the value iscurrently present */
} Value_State;

typedef struct dn_csn
{
	int csn;					/* dn csn */
	int value_index;			/* dn value */
} Dn_Csn;

typedef struct entry_state
{
	Dn_Csn	dn_csns [MAX_VALS + 1];	/* list of dn csns for this entry */
	int		dn_csn_count;			/* csn of the current dn */
	Value_State values[MAX_VALS];	/* values of the attribute */
	int		attr_delete_csn;		/* last attempt to delete the entire attribute */
} Entry_State;

typedef enum
{
	OP_ADD_VALUE,
	OP_DELETE_VALUE,
	OP_RENAME_ENTRY,
	OP_DELETE_ATTR,
	OP_END
} Operation_Type;

typedef struct operation
{
	Operation_Type type;	/* operation type */	
	int csn;				/* operation type */
	int value_index;		/* value to add, remove or rename from */	
}Operation;

typedef struct simulator_params
{
	int runs;			/* number of runs */
	FILE *fout;			/* output file */
	int value_count;	/* number of values */
	int	verbose;		/* verbose mode */
	Operation *ops;		/* operation sequence to apply */
	int op_count;
}Simulator_Params;


/* gloabl data */
Simulator_Params sim;
char *g_values[] =
{
	"v",
	"u",
	"w",
	NULL	
};

/* forward declarations */

/* initialization */
void process_cmd (int argc, char **argv);
void set_default_sim_params ();
int  count_values ();
void parse_operations_file (char *name);
void parse_operation (char *line, int pos);
int value2index (char *value);
void print_usage ();

/* simulation run */
void run_simulation ();
void generate_operations (Operation **ops, int *op_count);
void generate_operation (Operation *op, int csn);
int* generate_operation_order (int op_count, int seq_num);
void apply_operation_sequence (Operation *ops, int op_count, int *order, Entry_State *entry);	
void init_entry_state (Entry_State *entry);
void init_value_state (Value_State *val, int seq_num);
void apply_operation (Entry_State *entry, Operation *op);
void free_operations (Operation **ops);
int ** new_perm_table (int op_count);
void free_perm_table (int ***perm_table, int op_count);
int get_perm_count (int op_count);	
void generate_perm_table (int *elements, int element_count, int static_part,
						  int **perm_table);
void apply_add_operation (Entry_State *entry, Operation *op);
void apply_value_delete_operation (Entry_State *entry, Operation *op);
void apply_attr_delete_operation (Entry_State *entry, Operation *op);
void apply_rename_operation (Entry_State *entry, Operation *op);
void purge_value_state (Entry_State *entry, int index);
void resolve_value_state (Entry_State *entry, int value_index);
int value_distinguished_at_delete (Entry_State *entry, int value_index);
int compare_entry_state (Entry_State *entry1, Entry_State *entry2, int run);

/* dnc_csn handling */
int dn_csn_add (Entry_State *entry, int value_index, int csn);
int get_value_dn_csn (Entry_State *entry, int value_index);

/* data tracing */
void dump_operations (Operation *ops, int op_count, int *order);
void dump_operation (Operation *op);		
void dump_perm_table (int **perm_table, int op_count);
void dump_entry_state (Entry_State *entry);
void dump_value_state (Value_State *value);
void dump_dn_csn_list (Entry_State *entry);

/* misc functions */
int max_val (int a, int b);

int main (int argc, char **argv)
{
	int i;
	
	process_cmd (argc, argv);

	for (i = 0; i < sim.runs; i++)
	{
		fprintf (sim.fout, "*******running simulation #%d ...\n\n", i+1);
		run_simulation ();
		fprintf (sim.fout, "\n*******done with simulation #%d ...\n\n", i+1);
	}

	if (sim.fout != stdout)
		fclose (sim.fout);

	return 0;
}

void process_cmd (int argc, char **argv)
{
	int i;

	set_default_sim_params ();

	if (argc == 1)
	{
		return;
	}

	if (strcmp (argv[1], "-h") == 0)	/* print help */
	{
		print_usage ();
		exit (0);
	}

	i = 1;
	while (i < argc)
	{
		if (strcmp (argv[i], "-v") == 0)	/* verbose mode */
		{
			sim.verbose = 1;
			i ++;
		}
		else if (strcmp (argv[i], "-n") == 0)
		{
			if (i < argc - 1)
			{
				int runs = atoi (argv[i + 1]);
				if (runs > 0)
					sim.runs = runs;
				i+=2;
			}
			else
			{
				/* ONREPL print warning */
				i++;
			}
		}
		else if (strcmp (argv[i], "-f") == 0)  /* output file */
		{
			if (i < argc - 1)
			{
				FILE *f = fopen (argv[i + 1], "w");
				if (f == 0)
				{
					printf ("failed to open output file; error - %s, using stdout\n", 
							strerror(errno));
				}
				else
				{
					/* ONREPL print warning */
					sim.fout = f;
				}

				i += 2;
			}	
			else
				i++;
		}
		else if (strcmp (argv[i], "-o") == 0)  /* file with operation sequence */
		{
			if (i < argc - 1) 
			{
				parse_operations_file (argv[i+1]);
				i += 2;				
			}
			else
			{
				/* ONREPL print warning */
				i ++;
			}
		}
		else /* unknown option */
		{
			printf ("unknown option - %s; ignored\n", argv[i]);
			i ++;
		}
			
	}	
}

void set_default_sim_params ()
{
	memset (&sim, 0, sizeof (sim));
	sim.runs = 1;
	sim.fout = stdout;
	sim.value_count = count_values ();
}

/* file format: <op count>
				add <value>
				delete <value>
				delete attribute
				rename to <value>

	all spaces are significant
 */
void parse_operations_file (char *name)
{
	FILE *file = fopen (name, "r");
	char line [256];
	int i;

	if (file == NULL)
	{
		printf ("failed to open operations file %s: error = %d\n", name, errno);
		print_usage ();
		exit (1);
	}

	i = 0;
	while (fgets (line, sizeof (line), file))
	{
		if (i == 0)
		{
			/* read operation count */
			sim.op_count = atoi (line);
			if (sim.op_count < 1 || sim.op_count > MAX_OPS/2)
			{
				printf ("invalid operation count - %d; value must be between 1 and %d\n",
						 sim.op_count, MAX_OPS/2);
				print_usage ();
				exit (1);
			}
			else
			{
				sim.ops = (Operation*)malloc (sim.op_count * sizeof (Operation));
			}
		}
		else
		{	
			if (strlen (line) == 0)	 /* skip empty lines */
				continue;
			parse_operation (line, i);
		}

		i ++;			
	}	
}

void parse_operation (char *line, int i)
{
	sim.ops [i - 1].csn = i;

	if (line[strlen(line) - 1] == '\n')
		line[strlen(line) - 1] = '\0';		

	if (strncmp (line, "add", 3) == 0)
	{
		sim.ops [i - 1].type = OP_ADD_VALUE;
		sim.ops [i - 1].value_index = value2index (&line[4]);					
	}
	else if (strncmp (line, "delete attribute", 16) == 0)
	{
		sim.ops [i - 1].type = OP_DELETE_ATTR;
	}
	else if (strncmp (line, "delete", 6) == 0)
	{
		sim.ops [i - 1].type = OP_DELETE_VALUE;
		sim.ops [i - 1].value_index = value2index (&line[7]);					
	}
	else if (strncmp (line, "rename to", 6) == 0)
	{
		sim.ops [i - 1].type = OP_RENAME_ENTRY;
		sim.ops [i - 1].value_index = value2index (&line[10]);			
	}
	else
	{
		/* invalid line */
		printf ("invalid operation: %s\n", line);
		exit (1);
	}	
}

int value2index (char *value)
{
	int i;

	for (i = 0; i < sim.value_count; i++)
	{
		if (strcmp (g_values[i], value) == 0)
			return i;
	}

	return -1;
}

void print_usage ()
{
	printf ("usage: dnp_sim [-h] [-n <number of simulations> ] [-v] [-f <output file>]\n"
			"\t-h - print usage information\n"
			"\t-n <number of simulations>; default - 1\n"
			"\t-v - verbose mode\n"
			"\t-f <output file> - by default, results are printed to the screen\n"
			"\t-o <op file> - file that contains operation sequence to execute;\n"
			"\tby default, random sequence is generated.\n");
}

int count_values ()
{
	int i;
	
	for (i = 0; g_values[i]; i++);

	return i;
}

void run_simulation ()
{
	int *order;
	int i;
	int perm_count;
	Entry_State entry_first, entry_current;
	int error = 0;

	init_entry_state (&entry_first);
	fprintf (sim.fout, "initial entry state :\n");
	dump_entry_state (&entry_first);

	if (sim.ops == NULL)
	{
		generate_operations (&sim.ops, &sim.op_count);
	}
	fprintf (sim.fout, "initial operation set:\n");
	dump_operations (sim.ops, sim.op_count, NULL/* order */);

	//DebugBreak ();

	perm_count = get_perm_count (sim.op_count);
	for (i = 0; i < perm_count; i++)
	{
		fprintf (sim.fout, "--------------------------------\n");	
		fprintf (sim.fout, "simulation run %d\n", i + 1);
		fprintf (sim.fout, "--------------------------------\n");	
		order = generate_operation_order (sim.op_count, i);	
		if (i == 0)
			apply_operation_sequence (sim.ops, sim.op_count, order, &entry_first);	
		else
		{
			apply_operation_sequence (sim.ops, sim.op_count, order, &entry_current);	
			error |= compare_entry_state (&entry_first, &entry_current, i + 1);
		}	
	} 	

	switch (error)
	{
		case 0: fprintf (sim.fout, "all runs left the entry in the same state\n");
				break;
		case 1: fprintf (sim.fout, "while value presence is consistent across all runs, "
						"the exact state does not match\n");
				break;
		case 3:	fprintf (sim.fout, "the runs left entries in an inconsistent state\n");
				break;
	}

	free_operations (&sim.ops);
}

void generate_operations (Operation **ops, int *op_count)
{
	int i;

	/* generate number operations in the sequence */
	*op_count = slapi_rand () % (MAX_OPS / 2) + 1;
	*ops	= (Operation *)malloc (*op_count * sizeof (Operation));

	for (i = 0; i < *op_count; i ++)
	{
		generate_operation (&((*ops)[i]), i + 1);
	}
}

void generate_operation (Operation *op, int csn)
{
	/* generate operation type */
	op->type = slapi_rand () % OP_END;

	/* generate value to which operation applies */
	op->value_index = slapi_rand () % sim.value_count;

	op->csn = csn;
}

int* generate_operation_order (int op_count, int seq_num)
{
	static int **perm_table = NULL;

	/* first request - generate pemutation table */
	if (seq_num == 0)
	{
		int elements [MAX_OPS];
		int i;

		if (perm_table)
			free_perm_table (&perm_table, op_count);
		perm_table = new_perm_table (op_count);

		for (i = 0; i < op_count; i++)
			elements [i] = i;

		generate_perm_table	(elements, op_count, 0 /* static part */,
							 perm_table);
	}
	
	return perm_table [seq_num];
}

void apply_operation_sequence (Operation *ops, int op_count, int *order, Entry_State *entry)
{
	int i;

	init_entry_state (entry);

	if (!sim.verbose)
	{
		if (!sim.verbose)
		{
			fprintf (sim.fout, "operation_sequence for this run:\n");
			dump_operations (ops, op_count, order);
		}
	}

	for (i = 0; i < op_count; i++)
	{
		apply_operation (entry, &(ops [order[i]]));
	}

	if (!sim.verbose)
	{
		fprintf (sim.fout, "final entry state :\n");
		dump_entry_state (entry);
	}

}

void init_entry_state (Entry_State *entry)
{
	int i;

	memset (entry, 0, sizeof (*entry));
	entry->attr_delete_csn = NOT_PRESENT;
	entry->dn_csn_count = 1;

	for (i = 0; i < sim.value_count; i++)
	{	
		init_value_state (&(entry->values[i]), i);
	}
}

void init_value_state (Value_State *val, int seq_num)
{
	memset (val, 0, sizeof (*val));
	val->value_index = seq_num;	
	val->present = 1;
	val->delete_csn = NOT_PRESENT;
	if (seq_num > 0)	/* only first value is distinguished */
		val->distinguished_index = -1;
}

void apply_operation (Entry_State *entry, Operation *op)
{
	switch (op->type)
	{
		case OP_ADD_VALUE:		apply_add_operation (entry, op);
								break;
			
		case OP_DELETE_VALUE:	apply_value_delete_operation (entry, op);
								break;
		
		case OP_DELETE_ATTR:	apply_attr_delete_operation (entry, op);
								break;
				
		case OP_RENAME_ENTRY:	apply_rename_operation (entry, op);
								break;
	}

	if (sim.verbose)
	{	
		fprintf (sim.fout, "operation: ");
		dump_operation (op);
		fprintf (sim.fout, "\n");
		dump_entry_state (entry);
	}
}

void free_operations (Operation **ops)
{
	free (*ops);
	*ops = NULL;
}

int **new_perm_table (int op_count)
{
	int i;
	int **perm_table;
	int perm_count = get_perm_count (op_count);

	perm_table = (int**)malloc (perm_count * sizeof (int*));
	for (i = 0; i < perm_count; i ++)
		perm_table [i] = (int*) malloc (op_count * sizeof (int));

	return perm_table;	
}

void free_perm_table (int ***perm_table, int op_count)
{
	int i;
	int perm_count = get_perm_count (op_count);

	for (i = 0; i < perm_count; i ++)
		free ((*perm_table)[i]);

	free (*perm_table);
	*perm_table = NULL;
}

void generate_perm_table (int *elements, int element_count, int static_part,
						  int **perm_table)
{
	int i;
	int elements_copy [MAX_OPS];
	int start_pos;

	if (element_count - 1 == static_part)
	{
		memcpy (*perm_table, elements, element_count * sizeof (int));
		return;
	}

	start_pos = 0;
	for (i = 0; i < element_count - static_part; i ++)
	{
		memcpy (elements_copy, elements, element_count * sizeof (int));
		elements_copy [static_part] = elements [static_part + i];
		elements_copy [static_part + i] = elements [static_part];
		generate_perm_table (elements_copy, element_count, static_part + 1, 
							 &perm_table [start_pos]);
		start_pos += get_perm_count (element_count - static_part - 1);
	}
}

int get_perm_count (int op_count)
{
	int i;
	int perm_count = 1;

	for (i = 2; i <= op_count; i ++)
		perm_count *= i;

	return perm_count;		
}

void apply_add_operation (Entry_State *entry, Operation *op)
{
	if (entry->values[op->value_index].presence_csn < op->csn)
	{
		entry->values[op->value_index].presence_csn = op->csn;
		resolve_value_state (entry, op->value_index);
	}
}

void apply_value_delete_operation (Entry_State *entry, Operation *op)
{
	if (entry->values[op->value_index].delete_csn < op->csn)
	{
		entry->values[op->value_index].delete_csn = op->csn;
		resolve_value_state (entry, op->value_index);	
	}
}

void apply_attr_delete_operation (Entry_State *entry, Operation *op)
{
	int i;

	if (entry->attr_delete_csn < op->csn)
	{
		entry->attr_delete_csn = op->csn;

		for (i = 0; i < sim.value_count; i++)
		{
			resolve_value_state (entry, i);	
		}
	}
}

void apply_rename_operation (Entry_State *entry, Operation *op)
{
	int index;

	if (entry->values[op->value_index].presence_csn == NOT_PRESENT)
		entry->values[op->value_index].presence_csn = op->csn;	

	index = dn_csn_add (entry, op->value_index, op->csn);

	if (index > 0)
		resolve_value_state (entry, entry->dn_csns[index - 1].value_index);

	resolve_value_state (entry, entry->dn_csns[index].value_index);
}

void purge_value_state (Entry_State *entry, int value_index)
{
	Value_State *value = &(entry->values[value_index]);
	int value_distinguished_csn = get_value_dn_csn (entry, value_index);

	if (value_distinguished_csn == -1 && value->presence_csn > value->delete_csn)
		value->delete_csn = NOT_PRESENT;	
	else if (value->delete_csn < max_val (value_distinguished_csn, value->presence_csn))
		value->delete_csn = NOT_PRESENT;	
}

void resolve_value_state (Entry_State *entry, int value_index)
{
	Value_State *value = &(entry->values[value_index]);
	int value_distinguished_csn = get_value_dn_csn (entry, value_index);

	purge_value_state (entry, value_index);

	/* no deletes that effect the state */
	if (max_val (value->delete_csn, entry->attr_delete_csn) <
		max_val (value_distinguished_csn, value->presence_csn))
	{
		value->present = 1;
		return;
	}

	if (value->present)	/* check if it should be removed based on the current state */
	{
		if (!value_distinguished_at_delete (entry, value_index))
		{
			value->present = 0;
		}			
	}
	else	/* not present - check if it should be restored */
	{
		if (value_distinguished_at_delete (entry, value_index))
		{
			value->present = 1;	
		}
	}
}

int value_distinguished_at_delete (Entry_State *entry, int value_index)
{
	Value_State *value = &(entry->values[value_index]);
	int value_distinguished_csn = get_value_dn_csn (entry, value_index);
	int	delete_csn;
	int i;

	/* value has never been distinguished */
	if (value_distinguished_csn == -1)
		return 0;

	delete_csn = max_val (entry->attr_delete_csn, value->delete_csn);

	for (i = 0; i < entry->dn_csn_count; i++)
	{
		if (entry->dn_csns[i].csn > delete_csn)
			break;
	}

	/* i is never equal to 0 because the csn of the first element is always
	   smaller than csn of any operation we can receive */
	return (entry->dn_csns[i-1].value_index == value_index);	
}

int compare_entry_state (Entry_State *entry1, Entry_State *entry2, int run)
{
	int i;
	int error = 0;

	/* first - quick check for present / not present */
	for (i = 0; i < sim.value_count; i++)
	{
		if (entry1->values[i].present != entry2->values[i].present)
		{
			fprintf (sim.fout, 
				"value %s is %s present in the first run but %s present in the %d run\n",
					 g_values[i], entry1->values[i].present ? "" : "not",	
					 entry2->values[i].present ? "" : "not", run);
			error = 1;
		}					 
	}

	if (error)
		return 3;

	/* compare dnc_csn list */
	if (entry1->dn_csn_count != entry2->dn_csn_count)
	{
		fprintf (sim.fout, "dn_csn count is %d for run 1 and %d for run %d\n",
				entry1->dn_csn_count, entry2->dn_csn_count, run);
		error = 1;
	}

	for (i = 0; i < entry1->dn_csn_count; i++)
	{
		if (entry1->dn_csns [i].csn != entry2->dn_csns [i].csn || 
			entry1->dn_csns [i].value_index != entry2->dn_csns [i].value_index)
		{
			fprintf (sim.fout,"elements %d of dn csn list are different:\n"
					 "\tfirst run: csn - %d, value - %s\n"
					 "\t%d run: csn - %d, value - %s\n", i, 
					 entry1->dn_csns [i].csn, 
					 g_values[entry1->dn_csns [i].value_index],
					 run, entry2->dn_csns [i].csn, 
					 g_values[entry2->dn_csns [i].value_index]);

			error = 1;
		}
	}

	/* compare value state */
	if (entry1->attr_delete_csn != entry2->attr_delete_csn)
	{
		fprintf (sim.fout, "attribute delete csn is %d for run 1 "
				 "but is %d for run %d\n", entry1->attr_delete_csn, 
				 entry2->attr_delete_csn, run);
		error = 1;
	}

	for (i = 0; i < sim.value_count; i++)
	{
		if (entry1->values[i].presence_csn != entry2->values[i].presence_csn)
		{
			fprintf (sim.fout, "presence csn for value %s is %d in run 1 "
					 "but is %d in run %d\n", g_values[i], entry1->values[i].presence_csn, 
					 entry2->values[i].presence_csn, run);
			error = 1;
		}

		if (entry1->values[i].distinguished_index != entry2->values[i].distinguished_index)
		{
			fprintf (sim.fout, "distinguished index for value %s is %d in run 1 "
					 "but is %d in run %d\n", g_values[i], 
					 entry1->values[i].distinguished_index, 
					 entry2->values[i].distinguished_index, run); 					 
			error = 1;
		}

		if (entry1->values[i].delete_csn != entry2->values[i].delete_csn)
		{
			fprintf (sim.fout, "delete csn for value %s is %d in run 1 "
					 "but is %d in run %d\n", g_values[i], entry1->values[i].delete_csn, 
					 entry2->values[i].delete_csn, run);
			error = 1;
		}
	}

	if (error != 0)
	{
		return 1;
	}
	else
		return 0;
}

int dn_csn_add (Entry_State *entry, int value_index, int csn)
{
	int i, j;
	int distinguished_index;

	for (i = 0; i < entry->dn_csn_count; i++)
	{
		if (entry->dn_csns[i].csn > csn)
			break;
	}
	
	if (i < entry->dn_csn_count)
	{
		distinguished_index = i;
		for (j = i; j < entry->dn_csn_count; j ++)
		{
			if (entry->dn_csns[j].value_index == value_index)
				distinguished_index = j + 1;

			if (entry->values [entry->dn_csns[j].value_index].distinguished_index == j)
				entry->values [entry->dn_csns[j].value_index].distinguished_index ++;
		}

		memcpy (&(entry->dn_csns[i+1]), &(entry->dn_csns[i]), 
				(entry->dn_csn_count - i) * sizeof (Dn_Csn));	
	}
	else
	{
		distinguished_index = entry->dn_csn_count;
	}

	entry->values[value_index].distinguished_index = distinguished_index;
	entry->dn_csns[i].csn = csn;
	entry->dn_csns[i].value_index = value_index;
	entry->dn_csn_count ++;

	return i;				
}

int get_value_dn_csn (Entry_State *entry, int value_index)
{
	Value_State *value = &(entry->values [value_index]);

	if (value->distinguished_index == -1)
		return -1;
	else
		return entry->dn_csns [value->distinguished_index].csn;
}

void dump_operations (Operation *ops, int op_count, int *order)
{
	int index;
	int i;

	for (i = 0; i < op_count; i ++)
	{
		if (order == NULL)	/* current order */
			index = i;
		else
			index = order [i];

		dump_operation (&ops[index]);
	}

	fprintf (sim.fout, "\n");
}

void dump_operation (Operation *op)
{
	switch (op->type)
	{
		case OP_ADD_VALUE:		
				fprintf (sim.fout, "\t%d add value %s\n", op->csn, 
						 g_values [op->value_index]);
				break; 
		case OP_DELETE_VALUE:	
				fprintf (sim.fout, "\t%d delete value %s\n", op->csn, 
						 g_values [op->value_index]);
				break;
		case OP_DELETE_ATTR:
				fprintf (sim.fout, "\t%d delete attribute\n", op->csn);
				break;
		case OP_RENAME_ENTRY:
				fprintf (sim.fout, "\t%d rename entry to %s\n", op->csn, 
						 g_values [op->value_index]);
				break;
	}			
}

void dump_perm_table (int **perm_table, int op_count)
{
	int i, j;
	int perm_count = get_perm_count (op_count);

	for (i = 0; i < op_count; i++)
	{
		for (j = 0; j < perm_count; j++)
		{
			fprintf (sim.fout, "%d  ", perm_table [j][i]);
		}

		fprintf (sim.fout, "\n");
	}
}

void dump_entry_state (Entry_State *entry)
{
	int i;

	dump_dn_csn_list (entry);

	for (i = 0; i < sim.value_count; i++)
	{
		dump_value_state (&(entry->values[i]));
	}

	fprintf (sim.fout, "\n");
}

void dump_value_state (Value_State *value)
{
	fprintf (sim.fout, "\tvalue %s is %s\n", g_values[value->value_index], 
			 value->present ? "present" : "not present");

	if (sim.verbose)
	{	
		fprintf (sim.fout, "\t\tpresent csn: %d\n", value->presence_csn);
		fprintf (sim.fout, "\t\tdistinguished index: %d\n", value->distinguished_index);
		fprintf (sim.fout, "\t\tdelete value csn: %d\n", value->delete_csn);
	}
}

void dump_dn_csn_list (Entry_State *entry)
{
	int i;

	fprintf (sim.fout, "\tdn csn list: \n");	
	for (i = 0; i < entry->dn_csn_count; i++)
	{
		fprintf (sim.fout, "\t\tvalue: %s, csn: %d\n", 
				 g_values[entry->dn_csns[i].value_index], entry->dn_csns[i].csn);
	}
}

/* misc functions */
int max_val (int a, int b)
{
	if (a >= b)
		return a;
	else
		return b;
}
