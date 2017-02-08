#ifndef FRAME_H_
#define FRAME_H_

// darren
// kevin
struct frame_entry
{
	/* data of the frame entry */
	void* phys_addr; 
	void* user_addr;
	struct thread* owner;
	bool pin;
};


/* initializes frame table */
void frame_init(void);

/* synchronization */
void acquire_frame_lock(void);
bool has_frame_lock(void);
void release_frame_lock(void);

/* other possibly useful functions */
struct frame_entry* get_frame(void* upage); /* insert a page into the frame table */
void clear_frames(void); /* clears out all of a thread's frames from the frame table */

/* debugging */
void dump_frame_table(void);

#endif
