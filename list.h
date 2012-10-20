#ifndef LIST_H
#define LIST_H

struct list_elem {
     struct list_elem *next; /* head */
     struct list_elem *prev; /* tail */
};
#define offset_of(type, elem) ((unsigned long)(&(((type *)0)->elem)))
#define container_of(x, type, elem) ((type *)(((unsigned long)(x))-offset_of(type,elem)))

#define LIST_INIT(l) do { l = NULL; } while(0)

#define SLIST_INSERT(l, n) do {				\
	(n)->next = *l;					\
	(*l)= (n);					\
	} while(0)


#define LIST_INSERT_HEAD(l,n) do {					\
     (n)->next = (l);							\
     if((l))								\
	  (n)->prev = (l)->prev;					\
     else								\
	  ((n))->prev = (l);						\
     (l) = (n);							\
     } while(0)


#define SLIST_REMOVE_ELEM(l, n)			\
	(*l) = (n)->next



#define SLIST_REMOVE_HEAD(l)			\
	(l) = (l)->next

#define SLIST_EMPTY(l) (l == NULL)
#define LIST_PRINT(l) do {						\
	  struct list_elem *le;					\
	  printf("[%s:%d]list:%s\nhead %p tail %p\n",		\
		 __FILE__, __LINE__, #l, (l), (l)->prev);		\
	  for(le = (l); le != NULL; le=le->next) {			\
	       printf("elem: %p next: %p prev: %p\n",			\
		      le, le->next, le->prev);				\
	  }								\
     } while(0)
#endif
