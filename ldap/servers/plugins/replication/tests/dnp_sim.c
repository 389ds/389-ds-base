/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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

#define MAX_OPS		12	/* maximum number of operations in a simulation */
#define MAX_VALS	10	/* maximum number of values is entry or dn */
#define NOT_PRESENT -1

/* data types */
typedef struct value_state
{
	int value_index;					/* value */
	int presense_csn;					/* last time at which we know the value was present */
	int distinguished_csn;				/* last time at which we know the value was distinguished */
	int delete_csn;						/* last attempt to delete this value */
	int	non_distinguished_csns [MAX_OPS];/* list of times at which value became non-distinguished */
	int present;						/* flag that tells whether the value iscurrently present */
} Value_State;

typedef struct entry_state
{
	int dn_index;
	int  dn_csn;
	Value_State values[MAX_VALS];		/* values of the attribute */
	int attr_delete_csn;				/* last attempt to delete the entire attribute */
} Entry_State;

typedef enum
{
	OP_ADD_VALUE,
	OP_RENAME_ENTRY,
	OP_DELETE_VALUE,
	OP_DELETE_ATTR,
	OP_END
} Operation_Type;

typedef struct operation
{
	Operation_Type type;	/* operation type */	
	int csn;				/* operation type */
	int value_index;		/* value to add, remove or rename from */	
	int old_dn_index;		/* new dn - rename only */
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
void generate_operation (Operation *op, int csn, int *last_dn_index);
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
void make_value_distinguished (int op_csn, Entry_State *entry, int value_index);
void make_value_non_distinguished (int op_csn, Entry_State *entry, int value_index);
void purge_value_state (Value_State *value);
void purge_non_distinguished_csns (Value_State *value); 
void resolve_value_state (Entry_State *entry, int value_index);
int value_distinguished_at_delete (Value_State *value, int attr_delete_csn);
int compare_entry_state (Entry_State *entry1, Entry_State *entry2, int run);

/* data tracing */
void dump_operations (Operation *ops, int op_count, int *order);
void dump_operation (Operation *op);		
void dump_perm_table (int **perm_table, int op_count);
void dump_entry_state (Entry_State *entry);
void dump_value_state (Value_State *value);
void dump_list (int *list);

/* misc functions */
int max_val (int a, int b);
int is_list_empty (int *list);
int min_list_val (int *list);
int list_equal (int *list1, int *list2);

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
				rename <value> to <value>
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
	else if (strncmp (line, "rename", 6) == 0)
	{
		char *tok;
		sim.ops [i - 1].type = OP_RENAME_ENTRY;
		/* strtok() is not MT safe, but it is okay to call here because this is a program test */
		tok = strtok (&line[7], " ");
		sim.ops [i - 1].old_dn_index = value2index (tok);
		/* skip to */
		tok = strtok (NULL, " ");
		tok = strtok (NULL, " ");
		sim.ops [i - 1].value_index = value2index (tok);			
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
	int last_dn_index = 0;

	/* generate number operations in the sequence */
	*op_count = slapi_rand () % (MAX_OPS / 2) + 1;
	*ops	= (Operation *)malloc (*op_count * sizeof (Operation));

	for (i = 0; i < *op_count; i ++)
	{
		generate_operation (&((*ops)[i]), i + 1, &last_dn_index);
	}
}

void generate_operation (Operation *op, int csn, int *last_dn_index)
{
	/* generate operation type */
	op->type = slapi_rand () % OP_END;

	/* generate value to which operation applies */
	op->value_index = slapi_rand () % sim.value_count;

	op->csn = csn;

	/* generate new distinguished value */
	if (op->type == OP_RENAME_ENTRY)
	{		
		op->old_dn_index = *last_dn_index;
		while (op->value_index == op->old_dn_index)
			op->value_index = slapi_rand () % sim.value_count;		
		*last_dn_index = op->value_index;
	}	
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
		/* dump_perm_table (perm_table, op_count);*/
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
		val->distinguished_csn = -1;
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
	if (entry->values[op->value_index].presense_csn < op->csn)
	{
		entry->values[op->value_index].presense_csn = op->csn;
		entry->values[op->value_index].present = 1;
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
	if (entry->dn_csn < op->csn)
	{
		entry->dn_index = op->value_index;
		entry->dn_csn = op->csn;
	}

	make_value_non_distinguished (op->csn, entry, op->old_dn_index);
	make_value_distinguished (op->csn, entry, op->value_index);
}

void make_value_distinguished (int op_csn, Entry_State *entry, int value_index)
{
	Value_State *value = &(entry->values[value_index]);

	if (value->distinguished_csn < op_csn)
	{
		value->distinguished_csn = op_csn;

		if (value->presense_csn < op_csn)
		{
			value->present = 1;
			value->presense_csn = op_csn;
		}

		resolve_value_state (entry, value_index);
	}
}

void make_value_non_distinguished (int op_csn, Entry_State *entry, int value_index)
{
	int i = 0;
	int index;
	Value_State *value = &(entry->values[value_index]);

	if (op_csn < value->distinguished_csn)
		return;

	/* insert into sorted list */
	while (value->non_distinguished_csns[i] && value->non_distinguished_csns[i] < op_csn)
		i++;

	if (value->non_distinguished_csns[i] == 0)
		value->non_distinguished_csns[i] = op_csn;
	else
	{
		index = i;

		while (value->non_distinguished_csns[i])
			i++;

		memcpy (&(value->non_distinguished_csns[index + 1]), 
				&(value->non_distinguished_csns[index]), (i - index) * sizeof (int));

		value->non_distinguished_csns[index] = op_csn;
	}

	resolve_value_state (entry, value_index);
}

void purge_value_state (Value_State *value)
{
	/* value state information can be purged once a value was
	   readed/made distinguished because at that point we know that the value
	   existed/was distinguished */
	   
	purge_non_distinguished_csns (value); 

	if (value->delete_csn < max_val (value->distinguished_csn, value->presense_csn))
		value->delete_csn = NOT_PRESENT;	
}

void purge_non_distinguished_csns (Value_State *value)
{
	int i = 0;
	int index;

	while (value->non_distinguished_csns[i] &&
		   value->non_distinguished_csns[i]	< value->distinguished_csn)
		i ++;

	if (i > 0)
	{
		index = i-1;
		while (value->non_distinguished_csns[i])
			i ++;

		if (i > index + 1)
		{
			memcpy (value->non_distinguished_csns, &value->non_distinguished_csns[index+1],
					(i - index - 1) * sizeof (int));
			memset (&(value->non_distinguished_csns[index+1]), 0, sizeof (int) * (i - index - 1));
		}
		else
		{
			memset (value->non_distinguished_csns, 0, sizeof (int) * i);
		}
	}
}

int is_list_empty (int *list)
{
	return (list[0] == 0);
}

int min_list_val (int *list)
{
	return (list [0]);
}

void resolve_value_state (Entry_State *entry, int value_index)
{
	Value_State *value = &(entry->values[value_index]);

	purge_value_state (value);

	/* no deletes that effect the state */
	if (max_val (value->delete_csn, entry->attr_delete_csn) <
		max_val (value->distinguished_csn, value->presense_csn))
		return;

	if (value->present)	/* check if it should be removed based on the current state */
	{
		if (!value_distinguished_at_delete (value, entry->attr_delete_csn))
		{
			/* note that we keep presence csn because we might have to restore
			   the value in the future */
			value->present = 0;
		}			
	}
	else	/* not present - check if it should be restored */
	{
		if (value_distinguished_at_delete (value, entry->attr_delete_csn))
		{
			value->present = 1;	
		}
	}
}

/* Note we can't trim distinguished_csn (even during regular trimming)
   because in some cases we would not be able to figure out whether
   a value was distinguished or not at the time of deletion. 

   Example 1:							Example2:
   csn order operation					csn	order	operation
   1    1	 make V  distinguished		1	  1		make V distinguished
   3	3	 delete V 					2	  2		make V non distinguished
   4	2	 make V non-distinguished	3	  4		delete V
										4	  3     make V non distinguished (on another server)

   if the csns up to 2 were triimed, when delete operation is received, the state
   is exactly the same in both examples but in example one delete should not go
   through while in example 2 it should

 */
int value_distinguished_at_delete (Value_State *value, int attr_delete_csn)
{
	if (value->distinguished_csn >= 0 &&
		(is_list_empty (value->non_distinguished_csns) ||
		 min_list_val (value->non_distinguished_csns) > 
		 max_val (value->delete_csn, attr_delete_csn)))
		return 1;
	else
		return 0;
}

int compare_entry_state (Entry_State *entry1, Entry_State *entry2, int run)
{
	int j;
	int error = 0;

	/* first - quick check for present / not present */
	for (j = 0; j < sim.value_count; j++)
	{
		if (entry1->values[j].present != entry2->values[j].present)
		{
			fprintf (sim.fout, 
				"value %s is %s present in the first run but %s present in the %d run\n",
					 g_values[j], entry1->values[j].present ? "" : "not",	
					 entry2->values[j].present ? "" : "not", run);
			error = 1;
		}					 
	}

	if (error)
		return 3;

	/* compare value state */
	error = 0;
	if (entry1->attr_delete_csn != entry2->attr_delete_csn)
	{
		fprintf (sim.fout, "attribute delete csn is %d for run 1 "
				 "but is %d for run %d\n", entry1->attr_delete_csn, 
				 entry2->attr_delete_csn, run);
		error = 1;
	}

	for (j = 0; j < sim.value_count; j++)
	{
		if (entry1->values[j].presense_csn != entry2->values[j].presense_csn)
		{
			fprintf (sim.fout, "presence csn for value %s is %d in run 1 "
					 "but is %d in run %d\n", g_values[j], entry1->values[j].presense_csn, 
					 entry2->values[j].presense_csn, run);
			error = 1;
		}

		if (entry1->values[j].distinguished_csn != entry2->values[j].distinguished_csn)
		{
			fprintf (sim.fout, "distinguished csn for value %s is %d in run 1 "
					 "but is %d in run %d\n", g_values[j], entry1->values[j].distinguished_csn, 
					 entry2->values[j].distinguished_csn, run);
			error = 1;
		}

		if (entry1->values[j].delete_csn != entry2->values[j].delete_csn)
		{
			fprintf (sim.fout, "delete csn for value %s is %d in run 1 "
					 "but is %d in run %d\n", g_values[j], entry1->values[j].delete_csn, 
					 entry2->values[j].delete_csn, run);
			error = 1;
		}
		
		if (!list_equal (entry1->values[j].non_distinguished_csns,
						 entry2->values[j].non_distinguished_csns))
		{
			fprintf (sim.fout, "pending list mismatch for valye %s in runs 1 and %d\n", 
					 g_values[j], run);
			dump_list (entry1->values[j].non_distinguished_csns);
			dump_list (entry2->values[j].non_distinguished_csns);			
		}			 
	}

	if (error != 0)
	{
		return 1;
	}
	else
		return 0;
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
				fprintf (sim.fout, "\t%d rename entry from %s to %s\n", op->csn, 
						 g_values [op->old_dn_index], g_values [op->value_index]);
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
	fprintf (sim.fout, "\tentry dn: %s; dn csn - %d\n", 
			 g_values [entry->dn_index], entry->dn_csn);

	if (sim.verbose)
		fprintf (sim.fout, "\n");

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
		fprintf (sim.fout, "\t\tpresent csn: %d\n", value->presense_csn);
		fprintf (sim.fout, "\t\tdistinguished csn: %d\n", value->distinguished_csn);
		fprintf (sim.fout, "\t\tdelete value csn: %d\n", value->delete_csn);
		fprintf (sim.fout, "\t\tnon distinguished csns: ");

		dump_list (value->non_distinguished_csns);

		fprintf (sim.fout, "\n");	
	}
}

void dump_list (int *list)
{
	int i = 0;

	while (list[i])
	{
		fprintf (sim.fout, "%d ", list[i]);
		i ++;
	}
	
	fprintf (sim.fout, "\n");
}

/* misc functions */
int max_val (int a, int b)
{
	if (a >= b)
		return a;
	else
		return b;
}

int list_equal (int *list1, int *list2)
{
	int i;

	i = 0;
	while (list1[i] && list2[i])
	{
		if (list1[i] != list2[i])
			return 0;

		i ++;
	}

	if (list1[i] != list2[i])
		return 0;
	else
		return 1;
}
