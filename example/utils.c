// #include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mm_types.h>

pte_t *addr2pte(unsigned long addr, struct mm_struct *mm)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return pte;
	pud = pud_offset(pgd, addr);
	if (pud_none(*pud) || pud_bad(*pud))
		return pte;
	pmd = pmd_offset(pud, addr);
	if (pmd_none(*(pmd)) || pud_large(*(pud)))
		return pte;
	pte = pte_offset_map(pmd, addr);

	// printk(KERN_WARNING "page walk ahead %d %s %lx", i,
	//        !pte_none(*vm_entry.pte) &&
	// 		       pte_present(*vm_entry.pte) ?
	// 	       "PRESENT" :
	// 	       "GONE",
	//        addr & PAGE_ADDR_MASK);
	
	return pte;
}
