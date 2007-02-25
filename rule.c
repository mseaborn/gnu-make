/* Pattern and suffix rule internals for GNU Make.
Copyright (C) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997,
1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007 Free Software
Foundation, Inc.
This file is part of GNU Make.

GNU Make is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 3 of the License, or (at your option) any later
version.

GNU Make is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <assert.h>

#include "make.h"

#include <assert.h>

#include "dep.h"
#include "filedef.h"
#include "job.h"
#include "commands.h"
#include "variable.h"
#include "rule.h"

static void add_rule (struct rule *rule);
static void remove_rule (struct rule *rule);
static void free_rule (struct rule *rule);

/* List of rules with the same target. */
struct rule_target_list
{
  const char *target;
  int target_num;
  struct rule *rule;
  struct rule_target_list *next;
};

/* Chain of all pattern rules.  */

struct rule pattern_rules = { 1, &pattern_rules, &pattern_rules };

struct hash_table rules_by_target;

/* Number of rules in the chain.  */

unsigned int num_pattern_rules;

/* Maximum number of target patterns of any pattern rule.  */

unsigned int max_pattern_targets;

/* Maximum number of dependencies of any pattern rule.  */

unsigned int max_pattern_deps;

/* Maximum length of the name of a dependencies of any pattern rule.  */

unsigned int max_pattern_dep_length;

/* Pointer to structure for the file .SUFFIXES
   whose dependencies are the suffixes to be searched.  */

struct file *suffix_file;

/* Maximum length of a suffix.  */

unsigned int maxsuffix;

static unsigned long
rule_hash_1 (const void *key)
{
  return_ISTRING_HASH_1 (((struct rule_target_list *) key)->target);
}

static unsigned long
rule_hash_2 (const void *key)
{
  return_ISTRING_HASH_2 (((struct rule_target_list *) key)->target);
}

static int
rule_hash_cmp (const void *x, const void *y)
{
  return_ISTRING_COMPARE (((struct rule_target_list *) x)->target,
			  ((struct rule_target_list *) y)->target);
}

void rule_init ()
{
  hash_init (&rules_by_target, 199, rule_hash_1, rule_hash_2, rule_hash_cmp);
}

/* Compute the maximum dependency length and maximum number of
   dependencies of all implicit rules.  Also sets the subdir
   flag for a rule when appropriate.  */

void
count_implicit_rule_limits (void)
{
  char *name;
  int namelen;
  struct rule *rule, *lastrule;

  num_pattern_rules = max_pattern_targets = max_pattern_deps = 0;
  max_pattern_dep_length = 0;

  name = 0;
  namelen = 0;
  for (rule = pattern_rules.next; !rule->list_head; rule = rule->next)
    {
      unsigned int ndeps = 0;
      struct dep *dep;

      ++num_pattern_rules;

      if (rule->num > max_pattern_targets)
	max_pattern_targets = rule->num;

      for (dep = rule->deps; dep != 0; dep = dep->next)
	{
	  unsigned int len = strlen (dep->name);

#ifdef VMS
	  const char *p = strrchr (dep->name, ']');
          const char *p2;
          if (p == 0)
            p = strrchr (dep->name, ':');
          p2 = p != 0 ? strchr (dep->name, '%') : 0;
#else
	  const char *p = strrchr (dep->name, '/');
	  const char *p2 = p != 0 ? strchr (dep->name, '%') : 0;
#endif
	  ndeps++;

	  if (len > max_pattern_dep_length)
	    max_pattern_dep_length = len;

	  if (p != 0 && p2 > p)
	    {
	      /* There is a slash before the % in the dep name.
		 Extract the directory name.  */
	      if (p == dep->name)
		++p;
	      if (p - dep->name > namelen)
		{
		  namelen = p - dep->name;
		  name = xrealloc (name, namelen + 1);
		}
	      memcpy (name, dep->name, p - dep->name);
	      name[p - dep->name] = '\0';

	      /* In the deps of an implicit rule the `changed' flag
		 actually indicates that the dependency is in a
		 nonexistent subdirectory.  */

	      dep->changed = !dir_file_exists_p (name, "");
	    }
	  else
	    /* This dependency does not reside in a subdirectory.  */
	    dep->changed = 0;
	}

      if (ndeps > max_pattern_deps)
	max_pattern_deps = ndeps;
    }

  if (name != 0)
    free (name);
}

/* Create a pattern rule from a suffix rule.
   TARGET is the target suffix; SOURCE is the source suffix.
   CMDS are the commands.
   If TARGET is nil, it means the target pattern should be `(%.o)'.
   If SOURCE is nil, it means there should be no deps.  */

static void
convert_suffix_rule (const char *target, const char *source,
                     struct commands *cmds)
{
  const char **names, **percents;
  struct dep *deps;

  names = xmalloc (sizeof (const char *));
  percents = xmalloc (sizeof (const char *));

  if (target == 0)
    {
      /* Special case: TARGET being nil means we are defining a `.X.a' suffix
         rule; the target pattern is always `(%.o)'.  */
#ifdef VMS
      *names = strcache_add_len ("(%.obj)", 7);
#else
      *names = strcache_add_len ("(%.o)", 5);
#endif
      *percents = *names + 1;
    }
  else
    {
      /* Construct the target name.  */
      unsigned int len = strlen (target);
      char *p = alloca (1 + len + 1);
      p[0] = '%';
      memcpy (p + 1, target, len + 1);
      *names = strcache_add_len (p, len + 1);
      *percents = *names;
    }

  if (source == 0)
    deps = 0;
  else
    {
      /* Construct the dependency name.  */
      unsigned int len = strlen (source);
      char *p = alloca (1 + len + 1);
      p[0] = '%';
      memcpy (p + 1, source, len + 1);
      deps = alloc_dep ();
      deps->name = strcache_add_len (p, len + 1);
    }

  create_pattern_rule (names, percents, 1, 0, deps, cmds, 0);
}

/* Convert old-style suffix rules to pattern rules.
   All rules for the suffixes on the .SUFFIXES list are converted and added to
   the chain of pattern rules.  */

void
convert_to_pattern (void)
{
  struct dep *d, *d2;
  char *rulename;

  /* We will compute every potential suffix rule (.x.y) from the list of
     suffixes in the .SUFFIXES target's dependencies and see if it exists.
     First find the longest of the suffixes.  */

  maxsuffix = 0;
  for (d = suffix_file->deps; d != 0; d = d->next)
    {
      unsigned int l = strlen (dep_name (d));
      if (l > maxsuffix)
	maxsuffix = l;
    }

  /* Space to construct the suffix rule target name.  */
  rulename = alloca ((maxsuffix * 2) + 1);

  for (d = suffix_file->deps; d != 0; d = d->next)
    {
      unsigned int slen;

      /* Make a rule that is just the suffix, with no deps or commands.
	 This rule exists solely to disqualify match-anything rules.  */
      convert_suffix_rule (dep_name (d), 0, 0);

      if (d->file->cmds != 0)
	/* Record a pattern for this suffix's null-suffix rule.  */
	convert_suffix_rule ("", dep_name (d), d->file->cmds);

      /* Add every other suffix to this one and see if it exists as a
         two-suffix rule.  */
      slen = strlen (dep_name (d));
      memcpy (rulename, dep_name (d), slen);

      for (d2 = suffix_file->deps; d2 != 0; d2 = d2->next)
	{
          struct file *f;
          unsigned int s2len;

	  s2len = strlen (dep_name (d2));

          /* Can't build something from itself.  */
	  if (slen == s2len && streq (dep_name (d), dep_name (d2)))
	    continue;

	  memcpy (rulename + slen, dep_name (d2), s2len + 1);
	  f = lookup_file (rulename);
	  if (f == 0 || f->cmds == 0)
	    continue;

	  if (s2len == 2 && rulename[slen] == '.' && rulename[slen + 1] == 'a')
	    /* A suffix rule `.X.a:' generates the pattern rule `(%.o): %.X'.
	       It also generates a normal `%.a: %.X' rule below.  */
	    convert_suffix_rule (NULL, /* Indicates `(%.o)'.  */
				 dep_name (d),
				 f->cmds);

	  /* The suffix rule `.X.Y:' is converted
	     to the pattern rule `%.Y: %.X'.  */
	  convert_suffix_rule (dep_name (d2), dep_name (d), f->cmds);
	}
    }
}


static int
rule_targets_superset (struct rule *rule1, struct rule *rule2)
{
  int i;
  for (i = 0; i < rule1->num; i++)
    {
      int j;
      for (j = 0; j < rule2->num; j++)
	if (!streq (rule1->targets[i], rule2->targets[j]))
	  break;
      if (rule2->targets[j] == NULL)
	return 1;
    }
  return 0;
}

static int
rule_dependency_lists_equal (struct rule *rule1, struct rule *rule2)
{
  struct dep *dep1, *dep2;
  for (dep1 = rule1->deps, dep2 = rule2->deps;
       dep1 != NULL && dep2 != NULL;
       dep1 = dep1->next, dep2 = dep2->next)
    if (!streq (dep_name (dep1), dep_name (dep2)))
      return 0;
  return dep1 == NULL && dep2 == NULL;
}

/* Install the pattern rule RULE (whose fields have been filled in) at the end
   of the list (so that any rules previously defined will take precedence).
   If this rule duplicates a previous one (identical target and dependencies),
   the old one is replaced if OVERRIDE is nonzero, otherwise this new one is
   thrown out.  When an old rule is replaced, the new one is put at the end of
   the list.  Return nonzero if RULE is used; zero if not.  */

static int
new_pattern_rule (struct rule *rule, int override)
{
  struct rule *r;

  rule->in_use = 0;
  rule->terminal = 0;

  /* Search for an identical rule.  */
  for (r = pattern_rules.next; !r->list_head; r = r->next)
    {
      if (rule_targets_superset (rule, r) &&
	  rule_dependency_lists_equal (rule, r))
	{
	  /* All the dependencies matched.  */
	  if (override)
	    {
	      remove_rule (r);
	      free_rule (r);
	      
	      add_rule (rule);
	      
	      /* We got one.  Stop looking.  */
	      return 1;
	    }
	  else
	    {
	      /* The old rule stays intact.  Destroy the new one.  */
	      free_rule (rule);
	      return 0;
	    }
	}
    }

  /* There was no rule to replace.  */
  add_rule (rule);

  return 1;
}

static void
add_rule (struct rule *rule)
{
  /* Add to hash table. */
  int i;
  for (i = 0; i < rule->num; i++)
    {
      struct rule_target_list *node =
	(void *) xmalloc (sizeof (struct rule_target_list));
      node->target = rule->targets[i];
      node->target_num = i;
      node->rule = rule;
      node->next = hash_find_item (&rules_by_target, node);
      hash_insert (&rules_by_target, node);
    }

  /* Add to list. */
  rule->list_head = 0;
  rule->next = &pattern_rules;
  rule->prev = pattern_rules.prev;
  pattern_rules.prev->next = rule;
  pattern_rules.prev = rule;
}

static void
remove_rule (struct rule *rule)
{
  /* Remove from hash table. */
  int i;
  for (i = 0; i < rule->num; i++)
    {
      struct rule_target_list key, **list;
      key.target = rule->targets[i];
      list = (struct rule_target_list **) hash_find_slot (&rules_by_target,
							  &key);
      while(1)
	{
	  assert (*list != NULL);
	  if ((*list)->rule == rule &&
	      (*list)->target_num == i)
	    {
	      struct rule_target_list *node = *list;
	      *list = node->next;
	      free (node);
	      break;
	    }
	  list = &(*list)->next;
	}
    }

  /* Remove from list. */
  rule->prev->next = rule->next;
  rule->next->prev = rule->prev;
}


/* Install an implicit pattern rule based on the three text strings
   in the structure P points to.  These strings come from one of
   the arrays of default implicit pattern rules.
   TERMINAL specifies what the `terminal' field of the rule should be.  */

void
install_pattern_rule (struct pspec *p, int terminal)
{
  struct rule *r;
  char *ptr;

  r = xmalloc (sizeof (struct rule));

  r->num = 1;
  r->targets = xmalloc (sizeof (const char *));
  r->suffixes = xmalloc (sizeof (const char *));
  r->lens = xmalloc (sizeof (unsigned int));

  r->lens[0] = strlen (p->target);
  r->targets[0] = p->target;
  r->suffixes[0] = find_percent_cached (&r->targets[0]);
  assert (r->suffixes[0] != NULL);
  ++r->suffixes[0];

  ptr = p->dep;
  r->deps = (struct dep *) multi_glob (parse_file_seq (&ptr, '\0',
                                                       sizeof (struct dep), 1),
				       sizeof (struct dep));

  if (new_pattern_rule (r, 0))
    {
      r->terminal = terminal;
      r->cmds = xmalloc (sizeof (struct commands));
      r->cmds->fileinfo.filenm = 0;
      r->cmds->fileinfo.lineno = 0;
      /* These will all be string literals, but we malloc space for them
	 anyway because somebody might want to free them later.  */
      r->cmds->commands = xstrdup (p->commands);
      r->cmds->command_lines = 0;
    }
}


/* Free all the storage used in RULE.  */

static void
free_rule (struct rule *rule)
{
  struct dep *dep;

  dep = rule->deps;
  while (dep)
    {
      struct dep *t = dep->next;
      free_dep (dep);
      dep = t;
    }

  free (rule->targets);
  free (rule->suffixes);
  free (rule->lens);

  /* We can't free the storage for the commands because there
     are ways that they could be in more than one place:
       * If the commands came from a suffix rule, they could also be in
       the `struct file's for other suffix rules or plain targets given
       on the same makefile line.
       * If two suffixes that together make a two-suffix rule were each
       given twice in the .SUFFIXES list, and in the proper order, two
       identical pattern rules would be created and the second one would
       be discarded here, but both would contain the same `struct commands'
       pointer from the `struct file' for the suffix rule.  */

  free (rule);
}

/* Create a new pattern rule with the targets in the nil-terminated array
   TARGETS.  TARGET_PERCENTS is an array of pointers to the % in each element
   of TARGETS.  N is the number of items in the array (not counting the nil
   element).  The new rule has dependencies DEPS and commands from COMMANDS.
   It is a terminal rule if TERMINAL is nonzero.  This rule overrides
   identical rules with different commands if OVERRIDE is nonzero.

   The storage for TARGETS and its elements and TARGET_PERCENTS is used and
   must not be freed until the rule is destroyed.  */

void
create_pattern_rule (const char **targets, const char **target_percents,
                     unsigned int n, int terminal, struct dep *deps,
                     struct commands *commands, int override)
{
  unsigned int i;
  struct rule *r = xmalloc (sizeof (struct rule));

  r->num = n;
  r->cmds = commands;
  r->deps = deps;
  r->targets = targets;
  r->suffixes = target_percents;
  r->lens = xmalloc (n * sizeof (unsigned int));

  for (i = 0; i < n; ++i)
    {
      r->lens[i] = strlen (targets[i]);
      assert (r->suffixes[i] != NULL);
      ++r->suffixes[i];
    }

  if (new_pattern_rule (r, override))
    r->terminal = terminal;
}

/* Print the data base of rules.  */

static void			/* Useful to call from gdb.  */
print_rule (struct rule *r)
{
  unsigned int i;
  struct dep *d;

  for (i = 0; i < r->num; ++i)
    {
      fputs (r->targets[i], stdout);
      putchar ((i + 1 == r->num) ? ':' : ' ');
    }
  if (r->terminal)
    putchar (':');

  for (d = r->deps; d != 0; d = d->next)
    printf (" %s", dep_name (d));
  putchar ('\n');

  if (r->cmds != 0)
    print_commands (r->cmds);
}

void
print_rule_data_base (void)
{
  unsigned int rules, terminal;
  struct rule *r;

  puts (_("\n# Implicit Rules"));

  rules = terminal = 0;
  for (r = pattern_rules.next; !r->list_head; r = r->next)
    {
      ++rules;

      putchar ('\n');
      print_rule (r);

      if (r->terminal)
	++terminal;
    }

  if (rules == 0)
    puts (_("\n# No implicit rules."));
  else
    {
      printf (_("\n# %u implicit rules, %u"), rules, terminal);
#ifndef	NO_FLOAT
      printf (" (%.1f%%)", (double) terminal / (double) rules * 100.0);
#else
      {
	int f = (terminal * 1000 + 5) / rules;
	printf (" (%d.%d%%)", f/10, f%10);
      }
#endif
      puts (_(" terminal."));
    }

  if (num_pattern_rules != rules)
    {
      /* This can happen if a fatal error was detected while reading the
         makefiles and thus count_implicit_rule_limits wasn't called yet.  */
      if (num_pattern_rules != 0)
        fatal (NILF, _("BUG: num_pattern_rules wrong!  %u != %u"),
               num_pattern_rules, rules);
    }
}
