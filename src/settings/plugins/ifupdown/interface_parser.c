/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */
/* NetworkManager -- Network link manager
 *
 * Tom Parker <palfrey@tevp.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2004 Tom Parker
 */

#include "config.h"
#include "interface_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>
#include <libgen.h>
#include "nm-utils.h"

if_block* first;
if_block* last;

if_data* last_data;

void add_block(const char *type, const char* name)
{
	if_block *ret = (if_block*)calloc(1,sizeof(struct _if_block));
	ret->name = g_strdup(name);
	ret->type = g_strdup(type);
	if (first == NULL)
		first = last = ret;
	else
	{
		last->next = ret;
		last = ret;
	}
	last_data = NULL;
	//printf("added block '%s' with type '%s'\n",name,type);
}

void add_data(const char *key,const char *data)
{
	if_data *ret;
	char *idx;

	// Check if there is a block where we can attach our data
	if (first == NULL)
		return;

	ret = (if_data*) calloc(1,sizeof(struct _if_data));
	ret->key = g_strdup(key);
	// Normalize keys. Convert '_' to '-', as ifupdown accepts both variants.
	// When querying keys via ifparser_getkey(), use '-'.
	while ((idx = strrchr(ret->key, '_'))) {
		*idx = '-';
	}
	ret->data = g_strdup(data);

	if (last->info == NULL)
	{
		last->info = ret;
		last_data = ret;
	}
	else
	{
		last_data->next = ret;
		last_data = last_data->next;
	}
	//printf("added data '%s' with key '%s'\n",data,key);
}

// join values in src with spaces into dst;  dst needs to be large enough
static char *join_values_with_spaces(char *dst, char **src)
{
	if (dst != NULL) {
		*dst = '\0';
		if (src != NULL && *src != NULL) {
			strcat(dst, *src);

			for (src++; *src != NULL; src++) {
				strcat(dst, " ");
				strcat(dst, *src);
			}
		}
	}
	return(dst);
}

static void _ifparser_source (const char *path, const char *en_dir, int quiet);

static void
_recursive_ifparser (const char *eni_file, int quiet)
{
	FILE *inp;
	char line[255];
	int skip_to_block = 1;
	int skip_long_line = 0;
	int offs = 0;

	// Check if interfaces file exists and open it
	if (!g_file_test (eni_file, G_FILE_TEST_EXISTS)) {
		if (!quiet)
			g_warning ("Warning: interfaces file %s doesn't exist\n", eni_file);
		return;
	}
	inp = fopen (eni_file, "r");
	if (inp == NULL) {
		if (!quiet)
			g_warning ("Error: Can't open %s\n", eni_file);
		return;
	}
	if (!quiet)
		g_message ("      interface-parser: parsing file %s\n", eni_file);


	while (!feof(inp))
	{
		char *token[128];	// 255 chars can only be split into 127 tokens
		char value[255];	// large enough to join previously split tokens
		char *safeptr;
		int toknum;
		int len = 0;

		char *ptr = fgets(line+offs, 255-offs, inp);
		if (ptr == NULL)
			break;

		len = strlen(line);
		// skip over-long lines
		if (!feof(inp) && len > 0 &&  line[len-1] != '\n') {
			if (!skip_long_line) {
				if (!quiet)
					g_message ("Error: Skipping over-long-line '%s...'\n", line);
			}
			skip_long_line = 1;
			continue;
		}

		// trailing '\n' found: remove it & reset offset to 0
		if (len > 0 && line[len-1] == '\n') {
			line[--len] = '\0';
			offs = 0;
		}

		// if we're in long_line_skip mode, terminate it for real next line
		if (skip_long_line) {
			if (len == 0 || line[len-1] != '\\')
				skip_long_line = 0;
			continue;
		}

		// unwrap wrapped lines
		if (len > 0 && line[len-1] == '\\') {
			offs = len - 1;
			continue;
		}

		//printf(">>%s<<\n", line);

#define SPACES	" \t"
		// tokenize input;
		for (toknum = 0, token[toknum] = strtok_r(line, SPACES, &safeptr);
		     token[toknum] != NULL;
		     toknum++, token[toknum] = strtok_r(NULL, SPACES, &safeptr))
			;

		// ignore comments and empty lines
		if (toknum == 0 || *token[0]=='#')
			continue;

		if (toknum < 2) {
			if (!quiet) {
				g_message ("Error: Can't parse interface line '%s'\n",
						join_values_with_spaces(value, token));
			}
			skip_to_block = 1;
			continue;
		}

		// There are five different stanzas:
		// iface, mapping, auto, allow-* and source.
		// Create a block for each of them except source.

		// iface stanza takes at least 3 parameters
		if (strcmp(token[0], "iface") == 0) {
			if (toknum < 4) {
				if (!quiet) {
					g_message ("Error: Can't parse iface line '%s'\n",
							join_values_with_spaces(value, token));
				}
				continue;
			}
			add_block(token[0], token[1]);
			skip_to_block = 0;
			add_data(token[2], join_values_with_spaces(value, token + 3));
		}
		// auto and allow-auto stanzas are equivalent,
		// both can take multiple interfaces as parameters: add one block for each
		else if (strcmp(token[0], "auto") == 0 ||
			 strcmp(token[0], "allow-auto") == 0) {
			int i;
			for (i = 1; i < toknum; i++)
				add_block("auto", token[i]);
			skip_to_block = 0;
		}
		else if (strcmp(token[0], "mapping") == 0) {
			add_block(token[0], join_values_with_spaces(value, token + 1));
			skip_to_block = 0;
		}
		// allow-* can take multiple interfaces as parameters: add one block for each
		else if (strncmp(token[0],"allow-",6) == 0) {
			int i;
			for (i = 1; i < toknum; i++)
				add_block(token[0], token[i]);
			skip_to_block = 0;
		}
		// source stanza takes one or more filepaths as parameters
		else if (strcmp(token[0], "source") == 0) {
			int i;
			char *en_dir;

			skip_to_block = 0;

			if (toknum == 1) {
				if (!quiet)
					g_warning ("Error: Invalid source line without parameters\n");
				continue;
			}

			en_dir = g_path_get_dirname (eni_file);
			for (i = 1; i < toknum; ++i)
				_ifparser_source (token[i], en_dir, quiet);
			g_free (en_dir);
		}
		else {
			if (skip_to_block) {
				if (!quiet) {
					g_message ("Error: ignoring out-of-block data '%s'\n",
					           join_values_with_spaces(value, token));
				}
			} else
				add_data(token[0], join_values_with_spaces(value, token + 1));
		}
	}
	fclose(inp);

	if (!quiet)
		g_message ("      interface-parser: finished parsing file %s\n", eni_file);
}

static void
_ifparser_source (const char *path, const char *en_dir, int quiet)
{
	char *abs_path;
	wordexp_t we;
	uint i;

	if (g_path_is_absolute (path))
		abs_path = g_strdup (path);
	else
		abs_path = g_build_filename (en_dir, path, NULL);

	if (!quiet)
		g_message ("      interface-parser: source line includes interfaces file(s) %s\n", abs_path);

	/* ifupdown uses WRDE_NOCMD for wordexp. */
	if (wordexp (abs_path, &we, WRDE_NOCMD)) {
		if (!quiet)
			g_warning ("Error: word expansion for %s failed\n", abs_path);
	} else {
		for (i = 0; i < we.we_wordc; i++)
			_recursive_ifparser (we.we_wordv[i], quiet);
		wordfree (&we);
	}
	g_free (abs_path);
}

void ifparser_init (const char *eni_file, int quiet)
{
	first = last = NULL;
	_recursive_ifparser (eni_file, quiet);
}

void _destroy_data(if_data *ifd)
{
	if (ifd == NULL)
		return;
	_destroy_data(ifd->next);
	free(ifd->key);
	free(ifd->data);
	free(ifd);
	return;
}

void _destroy_block(if_block* ifb)
{
	if (ifb == NULL)
		return;
	_destroy_block(ifb->next);
	_destroy_data(ifb->info);
	free(ifb->name);
	free(ifb->type);
	free(ifb);
	return;
}

void ifparser_destroy(void)
{
	_destroy_block(first);
	first = last = NULL;
}

if_block *ifparser_getfirst(void)
{
	return first;
}

int ifparser_get_num_blocks(void)
{
	int i = 0;
	if_block *iter = first;

	while (iter) {
		i++;
		iter = iter->next;
	}
	return i;
}

if_block *ifparser_getif(const char* iface)
{
	if_block *curr = first;
	while(curr!=NULL)
	{
		if (strcmp(curr->type,"iface")==0 && strcmp(curr->name,iface)==0)
			return curr;
		curr = curr->next;
	}
	return NULL;
}

const char *ifparser_getkey(if_block* iface, const char *key)
{
	if_data *curr = iface->info;
	while(curr!=NULL)
	{
		if (strcmp(curr->key,key)==0)
			return curr->data;
		curr = curr->next;
	}
	return NULL;
}

gboolean
ifparser_haskey(if_block* iface, const char *key)
{
	if_data *curr = iface->info;

	while (curr != NULL) {
		if (strcmp (curr->key, key) == 0)
			return TRUE;
		curr = curr->next;
	}
	return FALSE;
}

int ifparser_get_num_info(if_block* iface)
{
	int i = 0;
	if_data *iter = iface->info;

	while (iter) {
		i++;
		iter = iter->next;
	}
	return i;
}
