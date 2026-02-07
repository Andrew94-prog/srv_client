#ifndef SRV_QLIST_H
#define SRV_QLIST_H

#include <stddef.h>

struct qlist_head {
    struct qlist_head *prev;
    struct qlist_head *next;
};

#define qlist_head_init(head)        \
({                                   \
    (head)->prev = (head);           \
    (head)->next = (head);           \
})

#define qlist_empty(head)            \
    ((head)->next == (head))

#define qlist_entry(type, list_field, item_ptr)                         \
    ((type *)((void *)(item_ptr) - offsetof(type, list_field)))

#define qlist_next_entry(pos, list_field)                               \
    qlist_entry(typeof(*pos), list_field, pos->list_field.next)

#define qlist_first_entry(type, list_field, head)			\
    qlist_entry(type, list_field, (head)->next)

#define qlist_entry_is_head(head, pos, list_field)                      \
    (&(pos)->list_field == (head))

#define qlist_add_head(head, item_ptr)                \
({                                                    \
    struct qlist_head *next = (head)->next;           \
    (head)->next = (item_ptr);                        \
    next->prev = (item_ptr);                          \
    (item_ptr)->prev = (head);                        \
    (item_ptr)->next = next;                          \
})

#define qlist_add_tail(head, item_ptr)                \
({                                                    \
    struct qlist_head *prev = (head)->prev;           \
    (head)->prev = (item_ptr);                        \
    prev->next = (item_ptr);                          \
    (item_ptr)->next = (head);                        \
    (item_ptr)->prev = prev;                          \
})

#define qlist_del_entry(item_ptr)                     \
({                                                    \
    struct qlist_head *prev = (item_ptr)->prev;       \
    struct qlist_head *next = (item_ptr)->next;       \
    prev->next = next;                                \
    next->prev = prev;                                \
    (item_ptr)->next = NULL;                          \
    (item_ptr)->prev = NULL;                          \
})

#define qlist_foreach_entry(head, pos, list_field)                                    \
    for (pos = qlist_first_entry(typeof(*pos), list_field, head);                     \
         !qlist_entry_is_head(head, pos, list_field);                                 \
         pos = qlist_next_entry(pos, list_field))

#define qlist_foreach_entry_safe(head, pos, pos_n, list_field)                        \
    for (pos = qlist_first_entry(typeof(*pos), list_field, head),                     \
         pos_n = qlist_next_entry(pos, list_field);                                   \
         !qlist_entry_is_head(head, pos, list_field);                                 \
         pos = pos_n,                                                                 \
         pos_n = qlist_next_entry(pos_n, list_field))

#endif /* SRV_QLIST_H */
