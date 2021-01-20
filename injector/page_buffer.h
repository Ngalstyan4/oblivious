#ifndef PAGE_BUFFER_H
#define PAGE_BUFFER_H

#include <linux/mm_types.h>

void add_page(struct page* page);

void my_add_page_to_buffer(struct page *page);
void my_add_page_to_buffer_delay(struct page *page);

#endif /*PAGE_BUFFER_H*/
