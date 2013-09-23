#include <assert.h>
/* A simple arena allocator somewhat similar
 * to "Fast Allocation and Deallocation of Memory Based on Object Lifetimes"
 */
struct ArenaPool
{
	struct Arena
	{
		Arena()
		{
			avail = buf;
			limit = buf + sizeof(buf);
			next = NULL;
		}
		struct Arena *next;
		char *limit;
		char *avail;
		char buf[8192-sizeof(char*)*3];
	};

	ArenaPool()
	{
		current = new Arena();
		free_list = NULL;
	}

	Arena *current;
	Arena *free_list;
	void *alloc(size_t size)
	{
		assert(size < sizeof(current->buf));
		// XXX: is >= correct
		if (current->avail + size >= current->limit) {
			// not enough room for size
			Arena *a;
			// reuse Arenas from the free_list first
			if (free_list) {
				// swap free_list into current an new entry
				a = free_list;
				free_list = free_list->next;
				a->next = current;
				current = a;

				current->avail = current->buf;
			} else {
				Arena *a = new Arena();
				a->next = current;
				current = a;
			}
		}

		current->avail += size;
		return current->avail - size;
	}

	// make all of the memory associated with the
	// arena pool available for allocation again
	void reset()
	{
		// find the end of the free list and add
		// the buffers after 'current' to it
		Arena **free_list_end = &free_list;
		while (*free_list_end) {
			free_list_end = &(*free_list_end)->next;
		}
		*free_list_end = current->next;
		current->next = NULL;

		// make all of current available
		current->avail = current->buf;
	}

	// free all of the memory associated with
	// the arena pool
	~ArenaPool ()
	{
		Arena *a = current;
		while (a) {
			Arena *b = a;
			a = b->next;
			delete b;
		}
		a = free_list;
		while (a) {
			Arena *b = a;
			a = b->next;
			delete b;
		}
	}

};

