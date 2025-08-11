#ifndef LIST_H_
#define LIST_H_

#include <wayland-util.h>

/* The only thing this header does, is alias wl_list* stuff */

#define list wl_list

#define list_for_each      wl_list_for_each
#define list_for_each_safe wl_list_for_each_safe

#define list_for_each_reverse      wl_list_for_each_reverse
#define list_for_each_reverse_safe wl_list_for_each_reverse_safe

#define list_init   wl_list_init
#define list_remove wl_list_remove
#define list_insert wl_list_insert
#define list_length wl_list_length
#define list_empty  wl_list_empty
#define list_insert_list wl_list_insert_list

#endif
