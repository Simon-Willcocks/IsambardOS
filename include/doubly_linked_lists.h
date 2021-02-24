
// Owned doubly linked lists. (Each element knows what list it is a part of.)
//
// Usage notes:
//   To fill a list with objects so they stay in the order they're inserted, use _as_tail
//   To reverse the order, use _as_head

#define DEFINE_DOUBLE_LINKED_LIST( name, structure, next, prev, owner ) \
static inline void insert_new_##name##_after_old( structure* new, structure *old ) \
{ \
  new->next = old->next; \
  new->prev = old; \
  old->next = new; \
  new->next->prev = new; \
  new->owner = old->owner; \
} \
static inline void insert_new_##name##_before_old( structure* new, structure *old ) \
{ \
  new->next = old; \
  new->prev = old->prev; \
  old->prev = new; \
  new->prev->next = new; \
  new->owner = old->owner; \
} \
static inline void insert_##name##_at_tail( structure** list, structure *new_tail ) \
{ \
  structure *old_head = *list; \
  if (old_head == 0) { \
    new_tail->next = new_tail; \
    new_tail->prev = new_tail; \
    new_tail->owner = list; \
    *list = new_tail; \
  } \
  else { \
    insert_new_##name##_before_old( new_tail, old_head ); \
  } \
} \
static inline void insert_##name##_as_head( structure** list, structure *new_head ) \
{ \
  insert_##name##_at_tail( list, new_head ); \
  *list = new_head; \
} \
static inline int count_##name##_entries( structure *list ) \
{ \
  if (list == 0) return 0; \
  structure *entry = list->next; \
  int result = 1; \
  while (list != entry) { \
    entry = entry->next; \
    result++; \
  } \
  return result; \
} \
static inline int foreach_##name##_entry_until( structure *list, int (*func)( structure *entry ) ) \
{ \
  if (list == 0) return 0; \
  structure *entry = list; \
  int result = 0; \
  do { \
    result = func( entry ); \
    entry = entry->next; \
  } while (list != entry && result == 0); \
  return result; \
} \
static inline void remove_##name ( structure *entry ) \
{ \
  structure *head = *entry->list; \
  if (head == entry) \
  { \
    head = head->next; \
    if (head == entry) { \
      *entry->list = 0; /* Emptied list */ \
    } \
    else { \
      *entry->list = head; /* Not the entry */ \
    } \
  } \
  entry->prev->next = entry->next; \
  entry->next->prev = entry->prev; \
  entry->prev = entry; \
  entry->next = entry; \
}

