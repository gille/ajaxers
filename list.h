#ifndef LIST_H
#define LIST_H

struct list_elem {
     struct list_elem *next; /* head */
     struct list_elem *prev; /* tail */
};
#define offset_of(type, elem) ((unsigned long)(&(((type *)0)->elem)))
#define container_of(x, type, elem) ((type *)(((unsigned long)(x))-offset_of(type,elem)))

#define LIST_INIT(l) do { l = NULL; } while(0)

#define LIST_INSERT_TAIL(l, n) do {				\
	  (n)->next = NULL;					\
	  if((l) == NULL) {					\
	       (l) = n;						\
	  } else {						\
	       (l)->prev->next = (n);				\
	       (n)->prev = (l)->prev;				\
	  }							\
	  (l)->prev = (n);					\
     } while(0)


#define LIST_INSERT_HEAD(l,n) do {					\
     (n)->next = (l);							\
     if((l))								\
	  (n)->prev = (l)->prev;					\
     else								\
	  ((n))->prev = (l);						\
     (l) = (n);							\
     } while(0)

/* check for NULL? */
#define LIST_REMOVE_HEAD(l) do {			\
	  struct list_elem *n=(l);			\
	  (l) = (l)->next;				\
	  if((l))					\
	       (l)->prev=n->prev;			\
     } while(0)


/* FIXME: Implement in a non broken way! */
#define LIST_REMOVE_ELEM(l, n) do {					\
	  if((n)->prev != NULL) {					\
	       (n)->prev->next = (n)->next;				\
	  }								\
	  if((n)->next != NULL) {					\
	       (n)->next->prev = (n)->prev;				\
	  }								\
	  if((n)== (l)) {						\
	       /* first element */					\
	       (l)=(l)->next;						\
	  } else							\
	       if((n) == (l)->prev) {					\
		    /* last element? */					\
		    (l)->prev = (l)->prev->prev;			\
	       }							\
     } while(0)

#define LIST_GET_HEAD(l) l

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
