/*
 * Today's hack: quantum tunneling in structs
 *
 * 'entries' and 'term' are never anywhere referenced by word in code. In fact,
 * they serve as the hanging-off data accessed through repl.data[].
 */

#define xt_alloc_initial_table(type, typ2) ({ \
	unsigned int hook_mask = info->valid_hooks; \
	unsigned int nhooks = hweight32(hook_mask); \
	unsigned int bytes = 0, hooknum = 0, i = 0; \
	size_t total_size = sizeof(struct type##_replace) + \
	                    nhooks * sizeof(struct type##_standard) + \
	                    sizeof(struct type##_error); \
	void *tbl = kzalloc(total_size, GFP_KERNEL); \
	if (tbl == NULL) \
		return NULL; \
	struct type##_replace *repl = tbl; \
	struct type##_standard *entries = (void *)repl + sizeof(*repl); \
	struct type##_error *term = (void *)entries + nhooks * sizeof(struct type##_standard); \
	strncpy(repl->name, info->name, sizeof(repl->name)); \
	*term = (struct type##_error)typ2##_ERROR_INIT;  \
	repl->valid_hooks = hook_mask; \
	repl->num_entries = nhooks + 1; \
	repl->size = nhooks * sizeof(struct type##_standard) + \
	             sizeof(struct type##_error); \
	for (; hook_mask != 0; hook_mask >>= 1, ++hooknum) { \
		if (!(hook_mask & 1)) \
			continue; \
		repl->hook_entry[hooknum] = bytes; \
		repl->underflow[hooknum]  = bytes; \
		entries[i++] = (struct type##_standard) \
			typ2##_STANDARD_INIT(NF_ACCEPT); \
		bytes += sizeof(struct type##_standard); \
	} \
	tbl; \
})
