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
#include <windows.h>

#define MAX_OPS			18	/* maximum number of operations in a simulation */
#define MAX_VALS		10	/* maximum number of values is entry or dn */
#define MAX_ATTR_NAME	16	/* max length of the attribute name */
#define NOT_PRESENT		-1
#define SV_ATTR_NAME	"sv_attr"	/* name of the singlevalued attribute */
#define MV_ATTR_NAME	"mv_attr"	/* name of the multivalued attribute */

/* data types */

/* value */
typedef struct value_state
{
	int value_index;			/* value */
	int presence_csn;			/* last time at which we know the value was present */
	int delete_csn;				/* last attempt to delete this value */
	int present;				/* flag that tells whether the value is present */
} Value_State;

/* shared attribute state */
typedef struct attr_state
{
	int	delete_csn;				/* last deletion csn */
	int present;				/* flag that tells whether the attribute is present */
}Attr_State;

/* singlevalued attribute */
typedef struct sv_attr_state
{
	Attr_State  attr_state;		/* shared attribute state */
	Value_State current_value;	/* current attribute value */
	Value_State *pending_value;	/* latest pending value */
} SV_Attr_State;

/* maltivalued attribute */
typedef struct mv_attr_state
{
	Attr_State  attr_state;			/* shared attribute state */
	Value_State values [MAX_VALS];	/* latest pending value */
	int value_count;				/* number of values in the array */
} MV_Attr_State;

/* node of dn_csn_list */
typedef struct dn_csn
{
	int csn;						/* dn csn */
	int sv_attr;					/* is this single valued or multivalued attr */
	int value_index;				/* dn value */
} Dn_Csn;

typedef struct entry_state
{
	Dn_Csn	dn_csns [MAX_VALS + 1];	/* list of dn csns for this entry */
	int		dn_csn_count;			/* csn of the current dn */
	SV_Attr_State sv_attr;			/* singlevalued attribute */
	MV_Attr_State mv_attr;			/* singlevalued attribute */
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
	Operation_Type type;			/* operation type */	
	int csn;						/* operation csn */
	int sv_attr;					/* is this applied to singlevalued attribute */
	int value_index;				/* value to add, remove or rename from */	
	int delete_old_rdn;				/* rename only */
	int old_rdn_sv_attr;			/* is oldrdn a singlevalued attribute */
	int old_rdn_value_index;		/* index of old_rdn */
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
void init_sv_attr_state (SV_Attr_State *sv_attr);
void init_mv_attr_state (MV_Attr_State *mv_attr);
void init_value_state (Value_State *val, int seq_num);
void free_operations (Operation **ops);
int ** new_perm_table (int op_count);
void free_perm_table (int ***perm_table, int op_count);
int get_perm_count (int op_count);	
void generate_perm_table (int *elements, int element_count, int static_part,
						  int **perm_table);
void apply_operation (Entry_State *entry, Operation *op);
void apply_add_operation (Entry_State *entry, Operation *op);
void apply_value_delete_operation (Entry_State *entry, Operation *op);
void apply_attr_delete_operation (Entry_State *entry, Operation *op);
void apply_rename_operation (Entry_State *entry, Operation *op);
void resolve_mv_attr_state (Entry_State *entry, Value_State *value);
void resolve_sv_attr_state (Entry_State *entry, Value_State *value);
void purge_sv_attr_state (Entry_State *entry);
void purge_mv_attr_state (Entry_State *entry, Value_State *value);
int value_distinguished_at_csn (Entry_State *entry, int sv_attr, Value_State *value, int csn);

/* state comparison */
int compare_entry_state (Entry_State *entry1, Entry_State *entry2, int run);
int compare_entry_state_quick (Entry_State *entry1, Entry_State *entry2, int run);
int compare_sv_attr_state_quick (SV_Attr_State *sv_attr1, SV_Attr_State *sv_attr2, int run);
int compare_mv_attr_state_quick (MV_Attr_State *mv_attr1, MV_Attr_State *mv_attr2, int run);
int compare_sv_attr_state (SV_Attr_State *sv_attr1, SV_Attr_State *sv_attr2, int run);
int compare_mv_attr_state (MV_Attr_State *mv_attr1, MV_Attr_State *mv_attr2, int run);
int compare_value_state (Value_State *value1, Value_State *value2, int run);

/* dnc_csn handling */
int dn_csn_add (Entry_State *entry, int sv_attr, int value_index, int csn);

/* data tracing */
void dump_operations (Operation *ops, int op_count, int *order);
void dump_operation (Operation *op);		
void dump_perm_table (int **perm_table, int op_count);
void dump_entry_state (Entry_State *entry);
void dump_sv_attr_state (SV_Attr_State *sv_attr);
void dump_mv_attr_state (MV_Attr_State *mv_attr);
void dump_value_state (Value_State *value, int sv_attr);
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

/* file format: <operation count>
				add <attribute> <value>
				delete <attribute>[ <value>]
				rename to <attribute> <value>[ delete <attribute> <value>]

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

#define ADD_KEYWORD				"add "
#define DELETE_KEYWORD			"delete "
#define RENAME_KEYWORD			"rename to "
#define DELET_OLD_RDN_KEYWORD	" delete "

void parse_operation (char *line, int i)
{
	int rc = 0;
	char *pos;
	char buff [64];

	sim.ops [i - 1].csn = i;

	if (line[strlen(line) - 1] == '\n')
		line[strlen(line) - 1] = '\0';		

	/* add <attribute> <value> */
	if (strncmp (line, ADD_KEYWORD, strlen (ADD_KEYWORD)) == 0)
	{
		sim.ops [i - 1].type = OP_ADD_VALUE;
		pos = strchr (&line[strlen (ADD_KEYWORD)], ' ');
		if (pos == NULL)
		{
			rc = -1;
			goto done;
		}

		memset (buff, 0, sizeof (buff));
		strncpy (buff, &line[strlen (ADD_KEYWORD)], pos - &line[strlen (ADD_KEYWORD)]); 
		sim.ops [i - 1].sv_attr = strcmp (buff, MV_ATTR_NAME);
		sim.ops [i - 1].value_index = value2index (pos + 1);					
	}
	/* delete <attribute>[ <value>] */
	else if (strncmp (line, DELETE_KEYWORD, strlen (DELETE_KEYWORD)) == 0)
	{
		pos = strchr (&line[strlen (DELETE_KEYWORD)], ' ');
		if (pos == NULL)	/* delete attribute version */
		{
			sim.ops [i - 1].type = OP_DELETE_ATTR;
			sim.ops [i - 1].sv_attr = strcmp (&line[strlen (DELETE_KEYWORD)], 
												  MV_ATTR_NAME);
		}
		else	/* delete value version */
		{
			memset (buff, 0, sizeof (buff));
			sim.ops [i - 1].type = OP_DELETE_VALUE;
			strncpy (buff, &line[strlen (DELETE_KEYWORD)], 
					 pos - &line[strlen (DELETE_KEYWORD)]);
			sim.ops [i - 1].sv_attr = strcmp (buff, MV_ATTR_NAME);
			sim.ops [i - 1].value_index = value2index (pos + 1);
		}
	}
	/* rename to <attribute> <valued>[ delete <attribute> <value>] */
	else if (strncmp (line, RENAME_KEYWORD, 10) == 0)
	{		
		char *pos2;

		sim.ops [i - 1].type = OP_RENAME_ENTRY;

		pos = strchr (&line[strlen (RENAME_KEYWORD)], ' ');
		if (pos == NULL)
		{
			rc = -1;
			goto done;
		}
		
		memset (buff, 0, sizeof (buff));
		strncpy (buff, &line[strlen (RENAME_KEYWORD)], pos - &line[strlen (RENAME_KEYWORD)]); 
		sim.ops [i - 1].sv_attr = strcmp (buff, MV_ATTR_NAME);		

		pos2 = strstr (pos + 1, DELET_OLD_RDN_KEYWORD);
		if (pos2 == NULL)	/* no delete old rdn part */
		{
			sim.ops [i - 1].value_index = value2index (pos + 1);			
			sim.ops [i - 1].delete_old_rdn = 0;
		}
		else
		{
			memset (buff, 0, sizeof (buff));
			strncpy (buff, pos + 1, pos2 - pos - 1);
			sim.ops [i - 1].value_index = value2index (buff);
			pos2 += strlen (DELET_OLD_RDN_KEYWORD);
			pos = strchr (pos2, ' ');
			if (pos == NULL)
			{
				rc = -1;
				goto done;
			}

			memset (buff, 0, sizeof (buff));
			strncpy (buff, pos2, pos - pos2);
			sim.ops [i - 1].delete_old_rdn = 1;
			sim.ops [i - 1].old_rdn_sv_attr = strcmp (buff, MV_ATTR_NAME);
			sim.ops [i - 1].old_rdn_value_index = value2index (pos + 1);
		}
	}
	else
	{
		/* error */
		rc = -1;
	}	

done:
	if (rc)
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
	op->csn = csn;

	/* choose if the operation applies to the single value or
	   the multivalued attribute */
	op->sv_attr = slapi_rand () % 2;

	/* generate value to which operation applies */
	op->value_index = slapi_rand () % sim.value_count;

	if (op->type == OP_RENAME_ENTRY)
	{
		op->delete_old_rdn = slapi_rand () % 2;
		if (op->delete_old_rdn)
		{
			op->old_rdn_sv_attr = slapi_rand () % 2;
			op->old_rdn_value_index = slapi_rand () % sim.value_count;

			while (op->old_rdn_sv_attr == op->sv_attr &&
				   op->old_rdn_value_index == op->value_index)
			{
				op->old_rdn_sv_attr = slapi_rand () % 2;
				op->old_rdn_value_index = slapi_rand () % sim.value_count;
			}
		}
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
	memset (entry, 0, sizeof (*entry));
	entry->dn_csn_count = 1;

	init_sv_attr_state (&entry->sv_attr);
	init_mv_attr_state (&entry->mv_attr);
}

void init_sv_attr_state (SV_Attr_State *sv_attr)
{
	memset (sv_attr, 0, sizeof (*sv_attr));
	sv_attr->attr_state.delete_csn = NOT_PRESENT;
	sv_attr->attr_state.present = 1;
	init_value_state (&sv_attr->current_value, 1);	
}

void init_mv_attr_state (MV_Attr_State *mv_attr)
{
	int i;

	memset (mv_attr, 0, sizeof (*mv_attr));
	mv_attr->attr_state.delete_csn = NOT_PRESENT;
	mv_attr->attr_state.present = 1;
	mv_attr->value_count = sim.value_count;
	
	for (i = 0; i < mv_attr->value_count; i++)
	{
		init_value_state (&(mv_attr->values[i]), i);
	}
}

void init_value_state (Value_State *val, int seq_num)
{
	memset (val, 0, sizeof (*val));
	val->value_index = seq_num;	
	val->present = 1;
	val->delete_csn = NOT_PRESENT;
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
	if (op->sv_attr)
	{
		Value_State *val;
		Value_State temp_val;

		if (op->value_index == entry->sv_attr.current_value.value_index)
		{
			val = &entry->sv_attr.current_value;
		}
		else if (entry->sv_attr.pending_value && 
                 op->value_index == entry->sv_attr.pending_value->value_index)
		{
			val = entry->sv_attr.pending_value;
		}
		else /* new value */
		{
			init_value_state (&temp_val, op->value_index);
			val = &temp_val;			
		}

		if (val->presence_csn < op->csn)
			val->presence_csn = op->csn;

		resolve_sv_attr_state (entry, val);	
	}
	else
	{
		if (entry->mv_attr.values[op->value_index].presence_csn < op->csn)
		{
			entry->mv_attr.values[op->value_index].presence_csn = op->csn;
			resolve_mv_attr_state (entry, &(entry->mv_attr.values[op->value_index]));
		}
	}
}

void apply_value_delete_operation (Entry_State *entry, Operation *op)
{
	if (op->sv_attr)
	{
		if (entry->sv_attr.attr_state.delete_csn < op->csn)
		{
			entry->sv_attr.attr_state.delete_csn = op->csn;
			resolve_sv_attr_state (entry, NULL);			
		}
	}
	else  /* mv attr */
	{
		if (entry->mv_attr.values[op->value_index].delete_csn < op->csn)
		{
			entry->mv_attr.values[op->value_index].delete_csn = op->csn;
			resolve_mv_attr_state (entry, &(entry->mv_attr.values[op->value_index]));	
		}
	}
}

void apply_attr_delete_operation (Entry_State *entry, Operation *op)
{
	int i;

	if (op->sv_attr)
	{
		if (entry->sv_attr.attr_state.delete_csn < op->csn)
		{
			entry->sv_attr.attr_state.delete_csn = op->csn;
			resolve_sv_attr_state (entry, NULL);			
		}
	}
	else /* mv attr */
	{
		if (entry->mv_attr.attr_state.delete_csn < op->csn)
		{
			entry->mv_attr.attr_state.delete_csn = op->csn;

			for (i = 0; i < sim.value_count; i++)
			{
				resolve_mv_attr_state (entry, &(entry->mv_attr.values[i]));	
			}
		}
	}
}

void apply_rename_operation (Entry_State *entry, Operation *op)
{
	int index;
	Operation del_op;

	/* insert new dn into dn_csn_list */
	index = dn_csn_add (entry, op->sv_attr, op->value_index, op->csn);

	/* issue delete value operation for the old rdn */
	if (op->delete_old_rdn)
	{
		del_op.type			= OP_DELETE_VALUE;
		del_op.csn			= op->csn;
		del_op.sv_attr		= op->old_rdn_sv_attr;
		del_op.value_index	= op->old_rdn_value_index;

		apply_value_delete_operation (entry, &del_op);	
	}

	/* resolve state of the previous node in dn_csn_list */
	if (index > 0)
	{
		if (entry->dn_csns[index-1].sv_attr)
		{
			if (entry->dn_csns[index-1].value_index == 
				entry->sv_attr.current_value.value_index)
			{
				resolve_sv_attr_state (entry, &(entry->sv_attr.current_value));	
			}
			else if (entry->sv_attr.pending_value && 
                     entry->dn_csns[index-1].value_index == 
					 entry->sv_attr.pending_value->value_index)
			{
				resolve_sv_attr_state (entry, entry->sv_attr.pending_value);	
			}
		}
		else
		{
			int i = entry->dn_csns[index-1].value_index;
			resolve_mv_attr_state (entry, &(entry->mv_attr.values[i]));
		} 
	}

	/* resolve state of the new dn */
	if (op->sv_attr)
	{
		Value_State *value;
		Value_State temp_val;
		if (op->value_index == entry->sv_attr.current_value.value_index)
		{
			value = &entry->sv_attr.current_value;
		}
		else if (entry->sv_attr.pending_value && 
                 op->value_index == entry->sv_attr.pending_value->value_index)
		{
			value = entry->sv_attr.pending_value;	
		}
		else /* new value */
		{
			init_value_state (&temp_val, op->value_index);
			value = &temp_val;
		}

		if (value->presence_csn == NOT_PRESENT || value->presence_csn < op->csn)
			value->presence_csn = op->csn;			
		resolve_sv_attr_state (entry, value);
	}
	else
	{
		if (entry->mv_attr.values[op->value_index].presence_csn == NOT_PRESENT ||
			entry->mv_attr.values[op->value_index].presence_csn < op->csn)
			entry->mv_attr.values[op->value_index].presence_csn = op->csn;	

		resolve_mv_attr_state (entry, &(entry->mv_attr.values[op->value_index]));
	}
}

void purge_mv_attr_state (Entry_State *entry, Value_State *value)
{
	if (value->presence_csn > value->delete_csn)
		value->delete_csn = NOT_PRESENT;	
}

void purge_sv_attr_state (Entry_State *entry)
{
	if (entry->sv_attr.attr_state.delete_csn != NOT_PRESENT)
	{
		if (entry->sv_attr.pending_value)
		{
			if (entry->sv_attr.attr_state.delete_csn < 
                entry->sv_attr.pending_value->presence_csn)
			{
				entry->sv_attr.attr_state.delete_csn = NOT_PRESENT;
			}
		}	
		else
		{
			if (entry->sv_attr.attr_state.delete_csn < 
				entry->sv_attr.current_value.presence_csn)
				entry->sv_attr.attr_state.delete_csn = NOT_PRESENT;
		}
	}
}

void resolve_mv_attr_state (Entry_State *entry, Value_State *value)
{
	purge_mv_attr_state (entry, value);

	/* no deletes that effect the state */
	if (max_val (value->delete_csn, entry->mv_attr.attr_state.delete_csn) <
        value->presence_csn)
	{
		value->present = 1;
		return;
	}

	if (value->present)	/* check if it should be removed based on the current state */
	{
		if (!value_distinguished_at_csn (entry, 0, value, 
							max (value->delete_csn, entry->mv_attr.attr_state.delete_csn)))
		{
			value->present = 0;
		}			
	}
	else	/* not present - check if it should be restored */
	{
		if (value_distinguished_at_csn (entry, 0, value,
							max (value->delete_csn, entry->mv_attr.attr_state.delete_csn)))
		{
			value->present = 1;	
		}
	}

	if (entry->mv_attr.attr_state.delete_csn == NOT_PRESENT)
	{
		entry->mv_attr.attr_state.present = 1;
	}
	else
	{
		int i;
		int distinguished = 0;

		for (i = 0; i < entry->mv_attr.value_count; i ++)
		{
			distinguished |= value_distinguished_at_csn (entry, 0, 
                                                         &(entry->mv_attr.values[i]),
														 entry->mv_attr.attr_state.delete_csn);
		}

		entry->mv_attr.attr_state.present = distinguished;
	}
}

void resolve_sv_attr_state (Entry_State *entry, Value_State *value)
{
	purge_sv_attr_state (entry);
		 
	if (value)
	{
		/* existing value is modified */
		if (value == &(entry->sv_attr.current_value) ||
			value == entry->sv_attr.pending_value)
		{
			/* check if current value should be replaced with the pending value */
			if (entry->sv_attr.pending_value)
			{
				if (!value_distinguished_at_csn (entry, 1, &entry->sv_attr.current_value,
					entry->sv_attr.current_value.presence_csn))
				{
					/* replace current value with the pending value */
					memcpy (&entry->sv_attr.current_value, entry->sv_attr.pending_value,
							sizeof (Value_State));
					free (entry->sv_attr.pending_value);
					entry->sv_attr.pending_value = NULL;
				}
			} 
		}
		else	/* addition of a new value */
		{
			/* new value is before the current value; note that, for new value,
               presence_csn is the same as distinguished_csn */
			if (value->presence_csn < entry->sv_attr.current_value.presence_csn)
			{
				/* if new value is distinguished, it should become current and the
				   current can become pending */
				if (value_distinguished_at_csn (entry, 1, value,
					                            entry->sv_attr.current_value.presence_csn))
				{
					if (entry->sv_attr.pending_value == NULL)
					{
						entry->sv_attr.pending_value = (Value_State*)
														malloc (sizeof (Value_State));
						memcpy (entry->sv_attr.pending_value, &entry->sv_attr.current_value,
								sizeof (Value_State));
					}

					memcpy (&entry->sv_attr.current_value, value, sizeof (Value_State));
				}
			} 
			else /* new value is after the current value */
			{
				/* if current value is not distinguished, new value should 
                   become distinguished */
				if (!value_distinguished_at_csn (entry, 1, &entry->sv_attr.current_value, 
                     value->presence_csn))
				{
					memcpy (&entry->sv_attr.current_value, value, sizeof (Value_State));
				}
				else /* current value is distinguished - check if new value should replace
                        the pending value */
				{	if (entry->sv_attr.pending_value)
					{
						if (value->presence_csn > entry->sv_attr.pending_value->presence_csn)
						{
							memcpy (entry->sv_attr.pending_value, value, sizeof (Value_State));
						}
					}
					else
					{
						entry->sv_attr.pending_value = (Value_State*)malloc (sizeof (Value_State));	
						memcpy (entry->sv_attr.pending_value, value, sizeof (Value_State));
					}
				}
			}
		}
	}
	
	/* update the attribute state */
	purge_sv_attr_state (entry);

	/* set attribute state */
	if (entry->sv_attr.attr_state.delete_csn != NOT_PRESENT &&
		!value_distinguished_at_csn (entry, 1, &entry->sv_attr.current_value, 
									 entry->sv_attr.attr_state.delete_csn))
	{
		entry->sv_attr.attr_state.present = 0;
	}
	else
	{
		entry->sv_attr.attr_state.present = 1;
	}
}

int value_distinguished_at_csn (Entry_State *entry, int sv_attr, Value_State *value, int csn)
{
	int i;

	for (i = 0; i < entry->dn_csn_count; i++)
	{
		if (entry->dn_csns[i].csn > csn)
			break;
	}
	
	/* i is never equal to 0 because the csn of the first element is always
	   smaller than csn of any operation we can receive */
	return (entry->dn_csns[i-1].value_index == value->value_index &&
			entry->dn_csns[i-1].sv_attr == sv_attr);	
}

int compare_entry_state (Entry_State *entry1, Entry_State *entry2, int run)
{
	int i;
	int error = 0;

	error = compare_entry_state_quick (entry1, entry2, run);

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
			entry1->dn_csns [i].sv_attr != entry2->dn_csns [i].sv_attr || 			
			entry1->dn_csns [i].value_index != entry2->dn_csns [i].value_index)
		{
			fprintf (sim.fout,"elements %d of dn csn list are different:\n"
					 "\tfirst run: csn - %d, attr - %s, value - %s\n"
					 "\t%d run: csn - %d, attr - %s value - %s\n", i, 
					 entry1->dn_csns [i].csn, 
					 entry1->dn_csns [i].sv_attr ? SV_ATTR_NAME : MV_ATTR_NAME,
					 g_values[entry1->dn_csns [i].value_index],
					 run, entry2->dn_csns [i].csn, 
					 entry2->dn_csns [i].sv_attr ? SV_ATTR_NAME : MV_ATTR_NAME,
					 g_values[entry2->dn_csns [i].value_index]);

			error = 1;
		}
	}

	error |= compare_sv_attr_state (&entry1->sv_attr, &entry2->sv_attr, run);

	error |= compare_mv_attr_state (&entry1->mv_attr, &entry2->mv_attr, run);
	
	if (error != 0)
	{
		return 1;
	}
	else
		return 0;
}

/* just compare if the same attributes and values are present */
int compare_entry_state_quick (Entry_State *entry1, Entry_State *entry2, int run)
{
	int error;

	error = compare_sv_attr_state_quick (&entry1->sv_attr, &entry2->sv_attr, run);
	
	error |= compare_mv_attr_state_quick (&entry1->mv_attr, &entry2->mv_attr, run);

	return error;
}

int compare_sv_attr_state_quick (SV_Attr_State *sv_attr1, SV_Attr_State *sv_attr2, int run)
{
	int error = 0;
	if (sv_attr1->attr_state.present !=	sv_attr2->attr_state.present)
	{
		fprintf (sim.fout, "singlevalued attribute is %s present in the first run "
				 "but is %s present in the %d run\n", 
				 sv_attr1->attr_state.present ? "" : "not", 
				 sv_attr2->attr_state.present ? "" : "not", run);
		return 1;	
	}

	if (sv_attr1->attr_state.present && 
        sv_attr1->current_value.value_index != sv_attr2->current_value.value_index)
	{
		fprintf (sim.fout, "different values for singlevalued attribute: %s for the \n"
                 "first run and %s for the %d run\n", 
				 g_values [sv_attr1->current_value.value_index],
				 g_values [sv_attr2->current_value.value_index], run);
		return 1;
	}

	return 0;
}

int compare_mv_attr_state_quick (MV_Attr_State *mv_attr1, MV_Attr_State *mv_attr2, int run)
{	
	int i;
	int error = 0;
 
	if (mv_attr1->attr_state.present !=	mv_attr2->attr_state.present)
	{
		fprintf (sim.fout, "multivalued attribute is %s present in the first run "
				 "but is %s present in the %d run\n", 
				 mv_attr1->attr_state.present ? "" : "not", 
				 mv_attr2->attr_state.present ? "" : "not", run);
		return 1;	
	}

	/* value count does not change during the iteration, so we don't have
       to check if the count is the same for both attributes */
	for (i = 0; i < mv_attr1->value_count; i++)
	{
		if (mv_attr1->values[i].present != mv_attr2->values[i].present)
		{
			fprintf (sim.fout, "value %s is %s present in the multivalued attribute\n"
                     "in the first run but %s present in the %d run\n",
					 g_values[i], mv_attr1->values[i].present ? "" : "not",	
					 mv_attr2->values[i].present ? "" : "not", run);
			error = 1;
		}					 
	}

	return error;
}

int compare_sv_attr_state (SV_Attr_State *sv_attr1, SV_Attr_State *sv_attr2, int run)
{
	int error = 0;

	if (sv_attr1->attr_state.delete_csn != sv_attr2->attr_state.delete_csn)
	{
		fprintf (sim.fout, "singlevalued attribute deletion csn is %d for run 1 "
				 "but is %d for run %d\n", sv_attr1->attr_state.delete_csn, 
				 sv_attr2->attr_state.delete_csn, run);
		error = 1;
	}

	error |= compare_value_state (&sv_attr1->current_value, &sv_attr2->current_value, run);

	if ((sv_attr1->pending_value && !sv_attr1->pending_value) ||
		(!sv_attr1->pending_value && sv_attr1->pending_value))
	{
		fprintf (sim.fout, "pending value is %s present in the singlevalued attribute\n"
				 " in the first run but is %s in the %d run\n",
				 sv_attr1->pending_value ? "" : "not",
				 sv_attr2->pending_value ? "" : "not", run);

		return 1;
	}	

	if (sv_attr1->pending_value)
		error |= compare_value_state (sv_attr1->pending_value, sv_attr2->pending_value, run);

	return 0; 
}

int compare_mv_attr_state (MV_Attr_State *mv_attr1, MV_Attr_State *mv_attr2, int run)
{
	int error = 0;
	int i;

	if (mv_attr1->attr_state.delete_csn != mv_attr2->attr_state.delete_csn)
	{
		fprintf (sim.fout, "multivalued attribute deletion csn is %d for run 1 "
				 "but is %d for run %d\n", mv_attr1->attr_state.delete_csn, 
				 mv_attr2->attr_state.delete_csn, run);
		error = 1;
	}
	
	for (i = 0; i < mv_attr1->value_count; i++)
	{
		error |= compare_value_state (&mv_attr1->values[i], &mv_attr2->values[i], run);
	}

	return error;
}

int compare_value_state (Value_State *value1, Value_State *value2, int run)
{
	int error = 0;

	if (value1->presence_csn != value2->presence_csn)
	{
		fprintf (sim.fout, "multivalued attribute: presence csn for value %s is %d "
				 "in run 1 but is %d in run %d\n", g_values[value1->value_index], 
				 value1->presence_csn, value2->presence_csn, run);
		error = 1;
	}

	if (value1->delete_csn != value2->delete_csn)
	{
		fprintf (sim.fout, "multivalued attribute: delete csn for value %s is %d in run 1 "
				 "but is %d in run %d\n", g_values[value1->value_index], 
				 value1->delete_csn, value2->delete_csn, run);
		error = 1;
	}

	return error;
}

int dn_csn_add (Entry_State *entry, int sv_attr, int value_index, int csn)
{
	int i;

	for (i = 0; i < entry->dn_csn_count; i++)
	{
		if (entry->dn_csns[i].csn > csn)
			break;
	}
	
	if (i < entry->dn_csn_count)
	{
		memcpy (&(entry->dn_csns[i+1]), &(entry->dn_csns[i]), 
				(entry->dn_csn_count - i) * sizeof (Dn_Csn));	
	}

	entry->dn_csns[i].csn = csn;
	entry->dn_csns[i].sv_attr = sv_attr;
	entry->dn_csns[i].value_index = value_index;
	entry->dn_csn_count ++;

	return i;				
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
				fprintf (sim.fout, "\t%d add value %s to %s\n", op->csn, 
						 g_values [op->value_index],
						 op->sv_attr ? SV_ATTR_NAME : MV_ATTR_NAME);
				break; 
		case OP_DELETE_VALUE:	
				fprintf (sim.fout, "\t%d delete value %s from %s\n", op->csn, 
						 g_values [op->value_index],
						 op->sv_attr ? SV_ATTR_NAME : MV_ATTR_NAME);
				break;
		case OP_DELETE_ATTR:
				fprintf (sim.fout, "\t%d delete %s attribute\n", op->csn,
						 op->sv_attr ? SV_ATTR_NAME : MV_ATTR_NAME);
				break;
		case OP_RENAME_ENTRY:
				fprintf (sim.fout, "\t%d rename entry to %s=%s", op->csn, 
						 op->sv_attr ? SV_ATTR_NAME : MV_ATTR_NAME,
						 g_values [op->value_index]);
				if (op->delete_old_rdn)
					fprintf (sim.fout, " delete old rdn %s=%s\n", 
						     op->old_rdn_sv_attr ? SV_ATTR_NAME : MV_ATTR_NAME,
						     g_values [op->old_rdn_value_index]);
				else
					fprintf (sim.fout, "\n");	
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
	dump_dn_csn_list (entry);

	dump_sv_attr_state (&entry->sv_attr);
	dump_mv_attr_state (&entry->mv_attr);

	fprintf (sim.fout, "\n");
}

void dump_sv_attr_state (SV_Attr_State *sv_attr)
{
	fprintf (sim.fout, "\tattribute %s is %s present", SV_ATTR_NAME, 
			 sv_attr->attr_state.present ? "" : "not");
	if (sv_attr->attr_state.present)
	{
		fprintf (sim.fout, " and has the value of %s\n", 
				 g_values[sv_attr->current_value.value_index]);
	}
	else
	{ 
		fprintf (sim.fout, "\n");
	}

	if (sim.verbose)
	{
		fprintf (sim.fout, "\t\tdeletion csn: %d\n", sv_attr->attr_state.delete_csn);	
		fprintf (sim.fout, "\t\tcurrent value: ");
		dump_value_state (&sv_attr->current_value, 1/* for single valued attr */);
		if (sv_attr->pending_value)
		{
			fprintf (sim.fout, "\t\tpending value: ");
			dump_value_state (sv_attr->pending_value, 1/* for single valued attr */);
		}
	}
}

void dump_mv_attr_state (MV_Attr_State *mv_attr)
{
	int i;

	fprintf (sim.fout, "\tattribute %s is %s present\n", MV_ATTR_NAME, 
	         mv_attr->attr_state.present ? "" : "not");

	if (sim.verbose)
	{
		fprintf (sim.fout, "\t\tdeletion csn: %d\n", mv_attr->attr_state.delete_csn);
	}	

	for (i = 0; i < mv_attr->value_count; i++)
	{
		dump_value_state (&(mv_attr->values[i]), 0);
	}
}

void dump_value_state (Value_State *value, int sv_attr)
{
	if (!sv_attr)
	{
		fprintf (sim.fout, "\tvalue %s is %s present\n", g_values[value->value_index], 
				 value->present ? "" : "not");
	}
	else
	{
		fprintf (sim.fout, "%s\n", g_values[value->value_index]); 
	}

	if (sim.verbose)
	{	
		fprintf (sim.fout, "\t\t\tpresence csn: %d\n", value->presence_csn);
		fprintf (sim.fout, "\t\t\tdeletion value csn: %d\n", value->delete_csn);
	}
}

void dump_dn_csn_list (Entry_State *entry)
{
	int i;

	fprintf (sim.fout, "\tdn csn list: \n");	
	for (i = 0; i < entry->dn_csn_count; i++)
	{
		fprintf (sim.fout, "\t\t %s=%s, csn: %d\n", 
				 entry->dn_csns[i].sv_attr ? SV_ATTR_NAME : MV_ATTR_NAME,
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
